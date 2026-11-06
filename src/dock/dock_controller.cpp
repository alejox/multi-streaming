#include "dock/dock_controller.hpp"

#include "destinations/destination_editing.hpp"
#include "destinations/destination_removal.hpp"
#include "destinations/session_selection.hpp"
#include "dock/destination_editor_dialog.hpp"
#include "streaming/main_stream_encoders.hpp"

#include <obs-module.h>

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

QString state_text(SessionState state)
{
	switch (state) {
	case SessionState::Idle:
		return text("State.Idle");
	case SessionState::Starting:
		return text("State.Starting");
	case SessionState::Live:
		return text("State.Live");
	case SessionState::Reconnecting:
		return text("State.Reconnecting");
	case SessionState::Stopping:
		return text("State.Stopping");
	case SessionState::Failed:
		return text("State.Failed");
	}
	return {};
}

DestinationRowStatus row_status(const DestinationStatus &status)
{
	const RowActions actions = row_actions(status.state);

	DestinationRowStatus row;
	row.state = state_text(status.state);
	row.detail = QString::fromStdString(status.failure_reason);
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

DockController::DockController(DestinationsConfig config, ConfigRepository &repository, SecretStore &secrets,
			       StreamingEngine &engine, DestinationsDock *dock)
	: config_(std::move(config)),
	  repository_(repository),
	  secrets_(secrets),
	  engine_(engine),
	  dock_(dock)
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
	connect(dock, &DestinationsDock::start_all_requested, this, [this] { engine_.start_all(); });
	connect(dock, &DestinationsDock::stop_all_requested, this, [this] { engine_.stop_all(); });
	connect(dock, &DestinationsDock::start_requested, this,
		[this](const QString &id) { engine_.start({id.toStdString()}); });
	connect(dock, &DestinationsDock::stop_requested, this,
		[this](const QString &id) { engine_.stop({id.toStdString()}); });

	connect(&stats_timer_, &QTimer::timeout, this, [this] { engine_.sample_stats(); });
	stats_timer_.start(kStatsIntervalMilliseconds);

	refresh_rows();
}

DockController::~DockController()
{
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

	const RemovalResult result = remove_destination(config_, repository_, secrets_, id.toStdString());
	if (result.config_error && dock_) {
		dock_->show_problem(QString::fromStdString(*result.config_error));
	}
	if (result.secret_error && dock_) {
		dock_->show_problem(text("Dock.KeyNotDeleted").arg(QString::fromStdString(result.secret_error->message)));
	}
	if (result.removed) {
		engine_.set_destinations(config_.destinations);
		refresh_rows();
	}
}

bool DockController::open_editor(const Destination &initial, bool is_new)
{
	std::vector<EncoderChoice> encoders;
	for (const VideoEncoderOption &option : available_video_encoders()) {
		encoders.push_back({QString::fromStdString(option.id), QString::fromStdString(option.display_name)});
	}

	DestinationEditorDialog dialog(
		initial, is_new, encoders,
		[this](const Destination &destination, const std::optional<std::string> &new_key) {
			return save(destination, new_key);
		},
		dock_);
	return dialog.exec() == QDialog::Accepted;
}

std::optional<QString> DockController::save(const Destination &destination, const std::optional<std::string> &new_key)
{
	const SaveResult result = save_destination(config_, repository_, secrets_, destination, new_key);
	if (result.config_error) {
		return QString::fromStdString(*result.config_error);
	}

	engine_.set_destinations(config_.destinations);
	refresh_rows();

	if (result.secret_error) {
		return text("Editor.KeyNotStored").arg(QString::fromStdString(result.secret_error->message));
	}
	return std::nullopt;
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
