#include "dock/dock_controller.hpp"

#include "destinations/destination_editing.hpp"
#include "destinations/destination_ordering.hpp"
#include "destinations/destination_removal.hpp"
#include "destinations/session_selection.hpp"
#include "dock/destination_editor_dialog.hpp"
#include "dock/icons.hpp"
#include "oauth/twitch_token_store.hpp"
#include "streaming/localized_text.hpp"
#include "streaming/main_stream_encoders.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QLocale>
#include <QMessageBox>

#include <algorithm>
#include <vector>

namespace multistream {
namespace {

constexpr int kStatsIntervalMilliseconds = 1000;

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QString state_text(const DestinationStatus &status)
{
	switch (status.state) {
	case SessionState::Idle:
		return text("State.Idle");
	case SessionState::Starting:
		return text("State.Starting");
	case SessionState::Live:
		return text("State.Live");
	case SessionState::Reconnecting:
		return status.reconnect_attempt > 0
			       ? text("State.ReconnectingAttempt").arg(status.reconnect_attempt)
			       : text("State.Reconnecting");
	case SessionState::Stopping:
		return text("State.Stopping");
	case SessionState::Failed:
		return text("State.Failed");
	}
	return {};
}

RowTone tone_of(SessionState state)
{
	switch (state) {
	case SessionState::Idle:
		return RowTone::Idle;
	case SessionState::Starting:
	case SessionState::Stopping:
		return RowTone::Busy;
	case SessionState::Live:
		return RowTone::Live;
	case SessionState::Reconnecting:
		return RowTone::Warning;
	case SessionState::Failed:
		return RowTone::Failed;
	}
	return RowTone::Idle;
}

DestinationRowStatus row_status(const DestinationStatus &status)
{
	const RowActions actions = row_actions(status.state);

	DestinationRowStatus row;
	row.state = state_text(status);
	row.tone = tone_of(status.state);
	row.detail = QString::fromStdString(status.failure_reason);
	if (status.flapping) {
		row.warning = text("Dock.FlappingWarning");
	}
	row.can_start = actions.can_start;
	row.can_stop = actions.can_stop;
	row.can_edit = actions.can_edit;
	if (status.state == SessionState::Live || status.state == SessionState::Reconnecting) {
		const QString dropped = QLocale().toString(static_cast<qulonglong>(status.dropped_frames));
		row.stats = text("Dock.Stats")
				    .arg(QLocale().toString(status.stats.bitrate_kbps, 'f', 0), dropped,
					 QLocale().toString(status.stats.total_drop_percent, 'f', 1));
	}
	return row;
}

} // namespace

DockController::DockController(DestinationsConfig config, ConfigRepository &repository,
			       std::shared_ptr<SecretStore> secrets, StreamingEngine &engine, DestinationsDock *dock)
	: config_(std::move(config)),
	  repository_(repository),
	  engine_(engine),
	  dock_(dock),
	  secret_worker_(std::move(secrets), [this](std::function<void()> work) {
		  QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection);
	  })
{
	engine_.set_destinations(config_.destinations);
	engine_.set_status_listener([this](const DestinationStatus &status) {
		if (dock_) {
			dock_->update_status(QString::fromStdString(status.id), row_status(status));
		}
	});

	connect(dock, &DestinationsDock::add_requested, this, &DockController::add_destination);
	connect(dock, &DestinationsDock::edit_requested, this, &DockController::edit_destination);
	connect(dock, &DestinationsDock::delete_requested, this, &DockController::delete_destination);
	connect(dock, &DestinationsDock::move_up_requested, this,
		[this](const QString &id) { move_destination(id, true); });
	connect(dock, &DestinationsDock::move_down_requested, this,
		[this](const QString &id) { move_destination(id, false); });
	connect(dock, &DestinationsDock::move_to_requested, this, &DockController::move_destination_to);
	connect(dock, &DestinationsDock::color_icons_toggled, this, &DockController::set_color_icons);
	connect(dock, &DestinationsDock::start_all_requested, this, [this] { engine_.start_all(); });
	connect(dock, &DestinationsDock::stop_all_requested, this, [this] { engine_.stop_all(); });
	connect(dock, &DestinationsDock::start_requested, this,
		[this](const QString &id) { engine_.start({id.toStdString()}); });
	connect(dock, &DestinationsDock::stop_requested, this,
		[this](const QString &id) { engine_.stop({id.toStdString()}); });

	connect(&stats_timer_, &QTimer::timeout, this, [this] { engine_.sample_stats(); });
	stats_timer_.start(kStatsIntervalMilliseconds);

	dock->set_color_icons(config_.color_icons);
	refresh_rows();
}

DockController::~DockController()
{
	// Results already posted to this object are dropped with it.
	secret_worker_.close();
	engine_.set_status_listener(nullptr);
}

void DockController::add_destination()
{
	Destination destination;
	destination.id = generate_destination_id();
	open_editor(destination, true);
}

void DockController::edit_destination(const QString &id)
{
	const Destination *destination = find(id);
	if (destination != nullptr && row_actions(state_of(id)).can_edit) {
		const Destination copy = *destination;
		open_editor(copy, false);
	}
}

void DockController::delete_destination(const QString &id)
{
	const Destination *destination = find(id);
	if (destination == nullptr || !row_actions(state_of(id)).can_edit) {
		return;
	}

	const QString question = text("Dock.DeleteQuestion").arg(QString::fromStdString(destination->name));
	if (QMessageBox::question(dock_, text("Dock.DeleteTitle"), question) != QMessageBox::Yes) {
		return;
	}

	// The entry goes at once, so its row and its Delete action are gone before the secret
	// store is reached and a second delete cannot be submitted.
	const std::string destination_id = id.toStdString();
	const RemovalResult result = remove_destination_entry(config_, repository_, destination_id);
	if (result.config_error && dock_) {
		dock_->show_problem(QString::fromStdString(localize(*result.config_error)));
	}
	if (!result.removed) {
		return;
	}
	engine_.set_destinations(config_.destinations);
	refresh_rows();

	secret_worker_.run<std::optional<SecretError>>(
		[destination_id](SecretStore &secrets) {
			std::optional<SecretError> error = secrets.remove(destination_id);
			const std::optional<SecretError> token_error = TwitchTokenStore(secrets).remove(destination_id);
			return error ? error : token_error;
		},
		[this](std::optional<SecretError> error) {
			if (error && dock_) {
				dock_->show_problem(
					text("Dock.KeyNotDeleted").arg(QString::fromStdString(localize(*error))));
			}
		});
}

void DockController::move_destination(const QString &id, bool up)
{
	const std::string wanted = id.toStdString();
	const auto found = std::find_if(config_.destinations.begin(), config_.destinations.end(),
					[&](const Destination &destination) { return destination.id == wanted; });
	if (found == config_.destinations.end()) {
		return;
	}

	const int index = static_cast<int>(std::distance(config_.destinations.begin(), found));
	// One up lands before the previous row; one down lands before the row after the next.
	move_destination_to(id, up ? index - 1 : index + 2);
}

void DockController::move_destination_to(const QString &id, int insert_position)
{
	DestinationsConfig updated = config_;
	if (!move_destination_before(updated.destinations, id.toStdString(), insert_position)) {
		return;
	}

	const auto error = repository_.save(updated);
	if (error) {
		if (dock_) {
			dock_->show_problem(QString::fromStdString(localize(*error)));
		}
		return;
	}

	config_ = std::move(updated);
	refresh_rows();
}

void DockController::set_color_icons(bool on)
{
	if (config_.color_icons == on) {
		return;
	}

	DestinationsConfig updated = config_;
	updated.color_icons = on;
	const auto error = repository_.save(updated);
	if (error) {
		if (dock_) {
			dock_->show_problem(QString::fromStdString(localize(*error)));
			dock_->set_color_icons(config_.color_icons);
		}
		return;
	}

	config_ = std::move(updated);
}

bool DockController::open_editor(const Destination &initial, bool is_new)
{
	std::vector<EncoderChoice> encoders;
	for (const VideoEncoderOption &option : available_video_encoders()) {
		encoders.push_back({QString::fromStdString(option.id), QString::fromStdString(option.display_name)});
	}

	DestinationEditorDialog dialog(
		initial, is_new, encoders,
		[this](const Destination &destination, const std::optional<std::string> &new_key,
		       const std::optional<std::string> &refresh_token, DestinationEditorDialog::SaveDone done) {
			save(destination, new_key, refresh_token, std::move(done));
		},
		&secret_worker_, dock_);
	return dialog.exec() == QDialog::Accepted;
}

void DockController::save(const Destination &destination, const std::optional<std::string> &new_key,
			  const std::optional<std::string> &refresh_token, DestinationEditorDialog::SaveDone done)
{
	if (const auto error = save_destination_entry(config_, repository_, destination)) {
		done(QString::fromStdString(localize(*error)));
		return;
	}

	engine_.set_destinations(config_.destinations);
	refresh_rows();

	if (!new_key && !refresh_token) {
		done(std::nullopt);
		return;
	}

	// A key that cannot be stored is reported after saving: starting that destination
	// then says what is missing. A refresh token that cannot be stored only costs a
	// browser login on the next connect.
	const std::string destination_id = destination.id;
	secret_worker_.run<std::optional<SecretError>>(
		[destination_id, new_key, refresh_token](SecretStore &secrets) {
			std::optional<SecretError> key_error;
			if (new_key) {
				key_error = secrets.put(destination_id, *new_key);
			}
			if (refresh_token && TwitchTokenStore(secrets).put(destination_id, *refresh_token)) {
				obs_log(LOG_WARNING,
					"could not store the Twitch refresh token; the next connect opens the browser");
			}
			return key_error;
		},
		[done = std::move(done)](std::optional<SecretError> error) {
			if (error) {
				done(text("Editor.KeyNotStored").arg(QString::fromStdString(localize(*error))));
			} else {
				done(std::nullopt);
			}
		});
}

const Destination *DockController::find(const QString &id) const
{
	const std::string wanted = id.toStdString();
	const auto found = std::find_if(config_.destinations.begin(), config_.destinations.end(),
					[&](const Destination &destination) { return destination.id == wanted; });
	return found == config_.destinations.end() ? nullptr : &*found;
}

SessionState DockController::state_of(const QString &id) const
{
	const std::string wanted = id.toStdString();
	for (const DestinationStatus &status : engine_.statuses()) {
		if (status.id == wanted) {
			return status.state;
		}
	}
	return SessionState::Idle;
}

void DockController::refresh_rows()
{
	if (!dock_) {
		return;
	}

	const std::vector<DestinationStatus> statuses = engine_.statuses();
	std::vector<DestinationRowView> rows;
	for (const Destination &destination : config_.destinations) {
		DestinationRowView row;
		row.id = QString::fromStdString(destination.id);
		row.name = QString::fromStdString(destination.name);
		row.source = text(destination.video_source == VideoSource::Vertical ? "Source.Vertical" : "Source.Main");
		row.icon_file = std::string("icons/platforms/") +
				platform_id(effective_platform(destination.platform, destination.server_url)) + ".svg";
		row.brand_color = brand_color(effective_platform(destination.platform, destination.server_url));

		const auto status = std::find_if(statuses.begin(), statuses.end(), [&](const DestinationStatus &candidate) {
			return candidate.id == destination.id;
		});
		if (status != statuses.end()) {
			row.status = row_status(*status);
		}
		rows.push_back(std::move(row));
	}
	dock_->set_rows(rows);
}

} // namespace multistream
