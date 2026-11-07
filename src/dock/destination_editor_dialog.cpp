#include "dock/destination_editor_dialog.hpp"

#include "destinations/destination_editing.hpp"
#include "dock/icons.hpp"
#include "oauth/twitch_client_id.hpp"
#include "oauth/twitch_connect_flow.hpp"
#include "oauth/twitch_token_store.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <string>

namespace multistream {
namespace {

constexpr int kMaxBitrateKbps = 500000;
constexpr int kAudioTracks = 6;
constexpr int kMaxReconnectRetries = 10000;
constexpr int kMaxReconnectDelaySeconds = 900;

QSpinBox *new_spin_box(int minimum, int maximum, int value, QWidget *parent)
{
	auto *spin_box = new QSpinBox(parent);
	spin_box->setRange(minimum, maximum);
	spin_box->setValue(value);
	return spin_box;
}

std::string platform_icon_file(Platform platform)
{
	return std::string("icons/platforms/") + platform_id(platform) + ".svg";
}

std::string platform_text_key(const char *prefix, Platform platform)
{
	return std::string(prefix) + platform_id(platform);
}

} // namespace

DestinationEditorDialog::DestinationEditorDialog(const Destination &initial, bool is_new,
						 const std::vector<EncoderChoice> &encoders, SaveHandler save,
						 SecretStore *secrets, QWidget *parent)
	: QDialog(parent),
	  initial_(initial),
	  is_new_(is_new),
	  save_(std::move(save)),
	  secrets_(secrets)
{
	setWindowTitle(obs_module_text(is_new ? "Editor.AddTitle" : "Editor.EditTitle"));

	auto *form = new QFormLayout();

	name_ = new QLineEdit(QString::fromStdString(initial.name), this);
	field_errors_[DestinationField::Name] = add_field(form, "Editor.Name", name_);

	service_ = new QComboBox(this);
	for (const Platform platform : kAllPlatforms) {
		service_->addItem(themed_icon(platform_icon_file(platform).c_str(), palette()),
				  obs_module_text(platform_text_key("Platform.", platform).c_str()),
				  static_cast<int>(platform));
	}
	service_->setCurrentIndex(
		service_->findData(static_cast<int>(effective_platform(initial.platform, initial.server_url))));
	add_field(form, "Editor.Service", service_);

	server_url_ = new QLineEdit(QString::fromStdString(initial.server_url), this);
	server_url_->setPlaceholderText(obs_module_text("Editor.ServerUrlHint"));
	field_errors_[DestinationField::ServerUrl] = add_field(form, "Editor.ServerUrl", server_url_);

	service_hint_ = new QLabel(this);
	service_hint_->setWordWrap(true);
	service_hint_->setTextFormat(Qt::PlainText);
	form->addRow(QString(), service_hint_);
	update_service_hint();
	connect(service_, &QComboBox::activated, this, &DestinationEditorDialog::service_picked);

	key_ = new QLineEdit(this);
	key_->setEchoMode(QLineEdit::Password);
	key_->setPlaceholderText(obs_module_text(is_new ? "Editor.KeyRequired" : "Editor.KeyUnchanged"));
	key_error_ = add_field(form, "Editor.Key", key_);

	show_key_ = new QCheckBox(obs_module_text("Editor.ShowKey"), this);
	connect(show_key_, &QCheckBox::toggled, this, [this](bool show) {
		key_->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
	});
	form->addRow(QString(), show_key_);

	connect_twitch_ = new QPushButton(obs_module_text("Editor.ConnectTwitch"), this);
	connect(connect_twitch_, &QPushButton::clicked, this, &DestinationEditorDialog::connect_twitch);
	connect_status_ = new QLabel(this);
	connect_status_->setWordWrap(true);
	connect_status_->setTextFormat(Qt::PlainText);
	connect_status_->hide();

	auto *connect_row = new QHBoxLayout();
	connect_row->addWidget(connect_twitch_);
	connect_row->addWidget(connect_status_, 1);
	form->addRow(QString(), connect_row);

	video_source_ = new QComboBox(this);
	video_source_->addItem(obs_module_text("Source.Main"), static_cast<int>(VideoSource::Main));
	video_source_->addItem(obs_module_text("Source.Vertical"), static_cast<int>(VideoSource::Vertical));
	video_source_->setCurrentIndex(video_source_->findData(static_cast<int>(initial.video_source)));
	add_field(form, "Editor.VideoSource", video_source_);

	encoder_mode_ = new QComboBox(this);
	encoder_mode_->addItem(obs_module_text("Editor.EncoderShare"), static_cast<int>(EncoderMode::Share));
	encoder_mode_->addItem(obs_module_text("Editor.EncoderCustom"), static_cast<int>(EncoderMode::Custom));
	encoder_mode_->setCurrentIndex(encoder_mode_->findData(static_cast<int>(initial.encoder.mode)));
	connect(encoder_mode_, &QComboBox::currentIndexChanged, this,
		&DestinationEditorDialog::update_custom_fields);
	add_field(form, "Editor.EncoderMode", encoder_mode_);

	encoder_id_ = new QComboBox(this);
	for (const EncoderChoice &choice : encoders) {
		encoder_id_->addItem(choice.label, choice.id);
	}
	const QString initial_encoder = QString::fromStdString(initial.encoder.video_encoder_id);
	if (!initial_encoder.isEmpty() && encoder_id_->findData(initial_encoder) < 0) {
		// Kept visible so that an edit does not silently change it. Starting reports it.
		encoder_id_->addItem(initial_encoder, initial_encoder);
	}
	if (!initial_encoder.isEmpty()) {
		encoder_id_->setCurrentIndex(encoder_id_->findData(initial_encoder));
	}
	field_errors_[DestinationField::VideoEncoder] = add_field(form, "Editor.Encoder", encoder_id_);

	bitrate_ = new_spin_box(0, kMaxBitrateKbps, initial.encoder.video_bitrate_kbps, this);
	bitrate_->setSuffix(" kbps");
	field_errors_[DestinationField::VideoBitrate] = add_field(form, "Editor.Bitrate", bitrate_);

	audio_track_ = new_spin_box(1, kAudioTracks, initial.audio_track, this);
	field_errors_[DestinationField::AudioTrack] = add_field(form, "Editor.AudioTrack", audio_track_);

	enabled_ = new QCheckBox(obs_module_text("Editor.Enabled"), this);
	enabled_->setChecked(initial.enabled);
	form->addRow(QString(), enabled_);

	sync_with_main_ = new QCheckBox(obs_module_text("Editor.SyncWithMain"), this);
	sync_with_main_->setChecked(initial.sync_with_main);
	form->addRow(QString(), sync_with_main_);

	reconnect_retries_ = new_spin_box(0, kMaxReconnectRetries, initial.reconnect.max_retries(), this);
	reconnect_retries_->setToolTip(obs_module_text("Editor.ReconnectRetriesHint"));
	add_field(form, "Editor.ReconnectRetries", reconnect_retries_);

	reconnect_delay_ = new_spin_box(1, kMaxReconnectDelaySeconds, initial.reconnect.base_delay_seconds(), this);
	reconnect_delay_->setSuffix(" s");
	reconnect_delay_->setToolTip(obs_module_text("Editor.ReconnectDelayHint"));
	add_field(form, "Editor.ReconnectDelay", reconnect_delay_);

	save_error_ = new QLabel(this);
	save_error_->setWordWrap(true);
	save_error_->setTextFormat(Qt::PlainText);
	save_error_->hide();

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	connect(buttons, &QDialogButtonBox::accepted, this, &DestinationEditorDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto *layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(save_error_);
	layout->addWidget(buttons);

	update_custom_fields();
	update_connect_visibility();
}

void DestinationEditorDialog::accept()
{
	clear_errors();

	bool valid = true;
	const Destination destination = read_destination();
	for (const ValidationIssue &issue : validate_destination(destination)) {
		const auto label = field_errors_.find(issue.field);
		if (label != field_errors_.end()) {
			show_error(label->second, QString::fromStdString(issue.message));
		} else {
			show_error(save_error_, QString::fromStdString(issue.message));
		}
		valid = false;
	}

	const std::string entered_key = key_->text().trimmed().toStdString();
	if (const auto problem = validate_stream_key_entry(entered_key, is_new_)) {
		show_error(key_error_, QString::fromStdString(*problem));
		valid = false;
	}
	if (!valid) {
		return;
	}

	const std::optional<std::string> new_key = entered_key.empty() ? std::nullopt : std::make_optional(entered_key);
	if (const auto failure = save_(destination, new_key)) {
		show_error(save_error_, *failure);
		return;
	}
	QDialog::accept();
}

QLabel *DestinationEditorDialog::add_field(QFormLayout *form, const char *label_key, QWidget *field)
{
	auto *error = new QLabel(this);
	error->setWordWrap(true);
	error->setTextFormat(Qt::PlainText);
	error->hide();

	auto *column = new QVBoxLayout();
	column->setContentsMargins(0, 0, 0, 0);
	column->addWidget(field);
	column->addWidget(error);
	form->addRow(obs_module_text(label_key), column);
	return error;
}

Destination DestinationEditorDialog::read_destination() const
{
	Destination destination = initial_;
	destination.name = name_->text().trimmed().toStdString();
	destination.server_url = server_url_->text().trimmed().toStdString();
	// Stays unset until someone picks a service, so the URL keeps deciding.
	if (service_picked_) {
		destination.platform = static_cast<Platform>(service_->currentData().toInt());
	}
	destination.enabled = enabled_->isChecked();
	destination.video_source = static_cast<VideoSource>(video_source_->currentData().toInt());
	destination.encoder.mode = static_cast<EncoderMode>(encoder_mode_->currentData().toInt());
	destination.encoder.video_bitrate_kbps = bitrate_->value();
	destination.encoder.video_encoder_id = encoder_id_->currentData().toString().toStdString();
	destination.audio_track = audio_track_->value();
	destination.sync_with_main = sync_with_main_->isChecked();

	// The spin box ranges match the policy's limits, so this always succeeds.
	if (const auto policy = ReconnectPolicy::create(reconnect_retries_->value(), reconnect_delay_->value())) {
		destination.reconnect = *policy;
	}
	return destination;
}

void DestinationEditorDialog::clear_errors()
{
	for (auto &[field, label] : field_errors_) {
		label->hide();
	}
	key_error_->hide();
	save_error_->hide();
}

void DestinationEditorDialog::show_error(QLabel *label, const QString &message)
{
	label->setText(message);
	label->show();
}

void DestinationEditorDialog::service_picked()
{
	service_picked_ = true;
	const auto platform = static_cast<Platform>(service_->currentData().toInt());
	server_url_->setText(
		QString::fromStdString(url_after_picking(platform, server_url_->text().trimmed().toStdString())));
	update_service_hint();
	update_connect_visibility();
}

void DestinationEditorDialog::update_service_hint()
{
	const auto platform = static_cast<Platform>(service_->currentData().toInt());
	const QString hint = obs_module_text(platform_text_key("Editor.ServiceHint.", platform).c_str());
	service_hint_->setText(hint);
	service_hint_->setVisible(!hint.isEmpty() && !preset_ingest_url(platform) && platform != Platform::Custom);
}

void DestinationEditorDialog::update_custom_fields()
{
	const bool custom = static_cast<EncoderMode>(encoder_mode_->currentData().toInt()) == EncoderMode::Custom;
	encoder_id_->setEnabled(custom);
	bitrate_->setEnabled(custom);
}

void DestinationEditorDialog::update_connect_visibility()
{
	const auto platform = static_cast<Platform>(service_->currentData().toInt());
	connect_twitch_->setVisible(platform == Platform::Twitch);
	if (platform != Platform::Twitch) {
		connect_status_->hide();
	}
}

void DestinationEditorDialog::connect_twitch()
{
	if (!secrets_) {
		show_connect_status(QStringLiteral("Stream key storage is unavailable"));
		return;
	}
	if (flow_) {
		return;
	}

	TwitchTokenStore token_store(*secrets_);
	const SecretLookup stored = token_store.get(initial_.id);
	const std::optional<std::string> refresh_token =
		(stored.secret && !stored.secret->empty()) ? stored.secret : std::nullopt;

	flow_ = new TwitchConnectFlow(std::string(kTwitchClientId), std::string(kTwitchRedirectUri), this);
	connect(flow_, &TwitchConnectFlow::succeeded, this, &DestinationEditorDialog::on_connect_succeeded);
	connect(flow_, &TwitchConnectFlow::failed, this, &DestinationEditorDialog::on_connect_failed);

	connect_twitch_->setEnabled(false);
	show_connect_status(obs_module_text("Editor.ConnectTwitchWaiting"));
	flow_->start(refresh_token);
}

void DestinationEditorDialog::on_connect_succeeded(const QString &stream_key, const QString &channel_login,
						   const QString &refresh_token)
{
	blog(LOG_INFO, "[obs-multi-stream] Twitch connect succeeded as %s", channel_login.toUtf8().constData());
	cleanup_flow();
	key_->setText(stream_key);

	if (secrets_) {
		TwitchTokenStore token_store(*secrets_);
		token_store.put(initial_.id, refresh_token.toStdString());
	}

	show_connect_status(QString::fromUtf8(obs_module_text("Editor.ConnectTwitchDone")).arg(channel_login));
}

void DestinationEditorDialog::on_connect_failed(const QString &message)
{
	blog(LOG_WARNING, "[obs-multi-stream] Twitch connect failed: %s", message.toUtf8().constData());
	cleanup_flow();
	show_connect_status(message);
}

void DestinationEditorDialog::cleanup_flow()
{
	if (flow_) {
		flow_->deleteLater();
		flow_ = nullptr;
	}
	connect_twitch_->setEnabled(true);
}

void DestinationEditorDialog::show_connect_status(const QString &message)
{
	connect_status_->setText(message);
	connect_status_->setVisible(!message.isEmpty());
}

} // namespace multistream
