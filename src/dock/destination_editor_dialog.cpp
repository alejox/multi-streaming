#include "dock/destination_editor_dialog.hpp"

#include "destinations/destination_editing.hpp"
#include "dock/icons.hpp"
#include "oauth/twitch_client_id.hpp"
#include "oauth/twitch_connect_flow.hpp"
#include "oauth/twitch_token_store.hpp"
#include "secrets/secret_worker.hpp"
#include "streaming/localized_text.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

namespace multistream {
namespace {

constexpr int kMaxBitrateKbps = 500000;
constexpr int kAudioTracks = 6;
constexpr int kMaxReconnectRetries = 10000;
constexpr int kMaxReconnectDelaySeconds = 900;
constexpr int kMinimumWidth = 480;

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

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

// OBS theme utility class (Yami: text-danger, text-muted, ...). A change after the widget
// was shown only takes effect once the style polishes it again.
void set_theme_class(QWidget *widget, const char *css_class)
{
	if (widget->property("class").toString() == QLatin1String(css_class)) {
		return;
	}
	widget->setProperty("class", QString::fromLatin1(css_class));
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

QFormLayout *new_form()
{
	auto *form = new QFormLayout();
	// macOS defaults to fields at their size hint; OBS's settings stretch them.
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setRowWrapPolicy(QFormLayout::DontWrapRows);
	form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
	return form;
}

QGroupBox *new_section(const char *title_key, QFormLayout *form, QWidget *parent)
{
	auto *box = new QGroupBox(text(title_key), parent);
	box->setLayout(form);
	return box;
}

} // namespace

DestinationEditorDialog::DestinationEditorDialog(const Destination &initial, bool is_new,
						 const std::vector<EncoderChoice> &encoders, SaveHandler save,
						 SecretWorker *secrets, QWidget *parent)
	: QDialog(parent),
	  initial_(initial),
	  is_new_(is_new),
	  save_(std::move(save)),
	  secrets_(secrets)
{
	setWindowTitle(text(is_new ? "Editor.AddTitle" : "Editor.EditTitle"));
	setMinimumWidth(kMinimumWidth);

	// Widgets are created in visual order, which is also their tab order.

	// Service and name, above the sections, like OBS's own Stream settings.
	auto *identity_form = new_form();
	service_ = new QComboBox(this);
	for (const Platform platform : kAllPlatforms) {
		service_->addItem(themed_icon(platform_icon_file(platform).c_str(), palette()),
				  text(platform_text_key("Platform.", platform).c_str()), static_cast<int>(platform));
	}
	service_->setCurrentIndex(
		service_->findData(static_cast<int>(effective_platform(initial.platform, initial.server_url))));
	add_row(identity_form, "Editor.Service", service_);
	connect(service_, &QComboBox::activated, this, &DestinationEditorDialog::service_picked);

	name_ = new QLineEdit(QString::fromStdString(initial.name), this);
	name_follows_service_ = is_new && initial.name.empty();
	field_rows_[DestinationField::Name] = add_field(identity_form, "Editor.Name", name_);
	connect(name_, &QLineEdit::textEdited, this, [this] {
		name_follows_service_ = false;
		clear_error(field_rows_[DestinationField::Name]);
	});

	// Connection: where the stream goes.
	connection_form_ = new_form();
	server_url_ = new QLineEdit(QString::fromStdString(initial.server_url), this);
	field_rows_[DestinationField::ServerUrl] = add_field(connection_form_, "Editor.ServerUrl", server_url_);
	connect(server_url_, &QLineEdit::textEdited, this,
		[this] { clear_error(field_rows_[DestinationField::ServerUrl]); });

	custom_server_ = new QCheckBox(text("Editor.CustomServer"), this);
	connection_form_->addRow(QString(), custom_server_);
	if (const auto preset = preset_ingest_url(current_platform())) {
		custom_server_->setChecked(!initial.server_url.empty() && initial.server_url != *preset);
	}
	connect(custom_server_, &QCheckBox::toggled, this, [this](bool custom) {
		if (!custom) {
			if (const auto preset = preset_ingest_url(current_platform())) {
				server_url_->setText(QString::fromStdString(*preset));
			}
			clear_error(field_rows_[DestinationField::ServerUrl]);
		}
		update_server_url_mode();
		if (custom) {
			server_url_->setFocus();
			server_url_->selectAll();
		}
	});

	service_hint_ = add_note(connection_form_, "text-small text-muted");

	key_ = new QLineEdit(this);
	key_->setEchoMode(QLineEdit::Password);
	key_->setPlaceholderText(text(is_new ? "Editor.KeyRequired" : "Editor.KeyUnchanged"));
	show_key_ = key_->addAction(themed_icon("icons/ui/visible.svg", palette()), QLineEdit::TrailingPosition);
	show_key_->setText(text("Editor.ShowKey"));
	show_key_->setToolTip(text("Editor.ShowKey"));
	connect(show_key_, &QAction::triggered, this, &DestinationEditorDialog::toggle_key_visibility);
	key_row_ = add_field(connection_form_, "Editor.Key", key_);
	connect(key_, &QLineEdit::textEdited, this, [this] { clear_error(key_row_); });

	key_link_ = add_note(connection_form_, "text-small");
	key_link_->setTextFormat(Qt::RichText);
	key_link_->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
	key_link_->setOpenExternalLinks(false);
	connect(key_link_, &QLabel::linkActivated, this,
		[](const QString &link) { QDesktopServices::openUrl(QUrl(link)); });

	connect_twitch_ = new QPushButton(text("Editor.ConnectTwitch"), this);
	{
		// Twitch brand button: white logo and text on Twitch purple.
		const QColor purple = brand_color(Platform::Twitch);
		connect_twitch_->setIcon(QIcon(colored_pixmap("icons/platforms/twitch.svg", Qt::white, 16,
							      devicePixelRatioF())));
		connect_twitch_->setIconSize(QSize(16, 16));
		connect_twitch_->setCursor(Qt::PointingHandCursor);
		connect_twitch_->setStyleSheet(
			// Same box as OBS's Yami buttons (1 px border, 4 px radius, 3 px x 16 px padding at
			// the default density), so it lines up with the dialog's other buttons.
			QStringLiteral("QPushButton { background-color: %1; color: white; border: 1px solid %3;"
				       " border-radius: 4px; padding: 3px 16px; font-weight: 600; }"
				       "QPushButton:hover { background-color: %2; }"
				       "QPushButton:pressed { background-color: %3; }"
				       "QPushButton:disabled { background-color: %4; border-color: %4;"
				       " color: rgba(255,255,255,0.6); }")
				.arg(purple.name(), purple.lighter(115).name(), purple.darker(115).name(),
				     purple.darker(160).name()));
	}
	connect(connect_twitch_, &QPushButton::clicked, this, &DestinationEditorDialog::connect_twitch);
	connect_status_ = new QLabel(this);
	connect_status_->setWordWrap(true);
	connect_status_->setTextFormat(Qt::PlainText);
	connect_status_->hide();

	connect_row_ = new QWidget(this);
	auto *connect_layout = new QHBoxLayout(connect_row_);
	connect_layout->setContentsMargins(0, 0, 0, 0);
	connect_layout->addWidget(connect_twitch_);
	connect_layout->addWidget(connect_status_, 1);
	connection_form_->addRow(QString(), connect_row_);

	// Output: what the stream carries.
	output_form_ = new_form();
	video_source_ = new QComboBox(this);
	video_source_->addItem(text("Source.Main"), static_cast<int>(VideoSource::Main));
	video_source_->addItem(text("Source.Vertical"), static_cast<int>(VideoSource::Vertical));
	video_source_->setCurrentIndex(video_source_->findData(static_cast<int>(initial.video_source)));
	add_row(output_form_, "Editor.VideoSource", video_source_);

	encoder_mode_ = new QComboBox(this);
	encoder_mode_->addItem(text("Editor.EncoderShare"), static_cast<int>(EncoderMode::Share));
	encoder_mode_->addItem(text("Editor.EncoderCustom"), static_cast<int>(EncoderMode::Custom));
	encoder_mode_->setCurrentIndex(encoder_mode_->findData(static_cast<int>(initial.encoder.mode)));
	add_row(output_form_, "Editor.EncoderMode", encoder_mode_);

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
	field_rows_[DestinationField::VideoEncoder] = add_field(output_form_, "Editor.Encoder", encoder_id_);
	connect(encoder_id_, &QComboBox::currentIndexChanged, this,
		[this] { clear_error(field_rows_[DestinationField::VideoEncoder]); });

	bitrate_ = new_spin_box(0, kMaxBitrateKbps, initial.encoder.video_bitrate_kbps, this);
	bitrate_->setSuffix(QStringLiteral(" ") + text("Unit.Kbps"));
	field_rows_[DestinationField::VideoBitrate] = add_field(output_form_, "Editor.Bitrate", bitrate_);
	connect(bitrate_, &QSpinBox::valueChanged, this,
		[this] { clear_error(field_rows_[DestinationField::VideoBitrate]); });

	// When to go live: both choices stay in view, since most users set them.
	auto *start_form = new_form();
	enabled_ = new QCheckBox(text("Editor.Enabled"), this);
	enabled_->setChecked(initial.enabled);
	enabled_->setToolTip(text("Editor.EnabledHint"));
	start_form->addRow(enabled_);

	sync_with_main_ = new QCheckBox(text("Editor.SyncWithMain"), this);
	sync_with_main_->setChecked(initial.sync_with_main);
	sync_with_main_->setToolTip(text("Editor.SyncWithMainHint"));
	start_form->addRow(sync_with_main_);
	// Following the OBS stream only applies to destinations in Start all.
	sync_with_main_->setEnabled(enabled_->isChecked());
	connect(enabled_, &QCheckBox::toggled, sync_with_main_, &QWidget::setEnabled);

	// Advanced: rarely changed, collapsed by default.
	advanced_toggle_ = new QToolButton(this);
	advanced_toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	advanced_toggle_->setAutoRaise(true);
	advanced_toggle_->setCursor(Qt::PointingHandCursor);
	connect(advanced_toggle_, &QToolButton::clicked, this,
		[this] { set_advanced_expanded(!advanced_expanded_); });

	auto *advanced_form = new_form();
	audio_track_ = new QComboBox(this);
	for (int track = 1; track <= kAudioTracks; ++track) {
		audio_track_->addItem(text("Editor.AudioTrackItem").arg(track), track);
	}
	if (audio_track_->findData(initial.audio_track) < 0) {
		// Kept as stored so an edit does not silently change it. Saving reports it.
		audio_track_->addItem(text("Editor.AudioTrackItem").arg(initial.audio_track), initial.audio_track);
	}
	audio_track_->setCurrentIndex(audio_track_->findData(initial.audio_track));
	field_rows_[DestinationField::AudioTrack] = add_field(advanced_form, "Editor.AudioTrack", audio_track_, true);
	connect(audio_track_, &QComboBox::currentIndexChanged, this,
		[this] { clear_error(field_rows_[DestinationField::AudioTrack]); });

	reconnect_retries_ = new_spin_box(0, kMaxReconnectRetries, initial.reconnect.max_retries(), this);
	reconnect_retries_->setToolTip(text("Editor.ReconnectRetriesHint"));
	add_row(advanced_form, "Editor.ReconnectRetries", reconnect_retries_);

	reconnect_delay_ = new_spin_box(1, kMaxReconnectDelaySeconds, initial.reconnect.base_delay_seconds(), this);
	reconnect_delay_->setSuffix(QStringLiteral(" ") + text("Unit.Seconds"));
	reconnect_delay_->setToolTip(text("Editor.ReconnectDelayHint"));
	add_row(advanced_form, "Editor.ReconnectDelay", reconnect_delay_);

	save_error_ = new QLabel(this);
	save_error_->setWordWrap(true);
	save_error_->setTextFormat(Qt::PlainText);
	save_error_->setProperty("class", QStringLiteral("text-danger"));
	save_error_->hide();

	buttons_ = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
	if (QPushButton *save_button = buttons_->button(QDialogButtonBox::Save)) {
		save_button->setDefault(true);
		save_button->setProperty("class", QStringLiteral("button-primary"));
	}
	connect(buttons_, &QDialogButtonBox::accepted, this, &DestinationEditorDialog::accept);
	connect(buttons_, &QDialogButtonBox::rejected, this, &DestinationEditorDialog::reject);

	auto *layout = new QVBoxLayout(this);
	layout->addLayout(identity_form);
	layout->addWidget(new_section("Editor.SectionConnection", connection_form_, this));
	layout->addWidget(new_section("Editor.SectionOutput", output_form_, this));
	layout->addWidget(new_section("Editor.SectionStart", start_form, this));
	layout->addWidget(advanced_toggle_, 0, Qt::AlignLeft);
	advanced_box_ = new_section("Editor.SectionAdvanced", advanced_form, this);
	layout->addWidget(advanced_box_);
	layout->addStretch(1);
	layout->addWidget(save_error_);
	layout->addWidget(buttons_);

	connect(encoder_mode_, &QComboBox::currentIndexChanged, this, &DestinationEditorDialog::update_custom_fields);

	// Group boxes took their widgets after creation, so the tab order is set explicitly.
	const std::vector<QWidget *> tab_order = {
		service_, name_, server_url_, custom_server_, key_, key_link_, connect_twitch_,
		video_source_, encoder_mode_, encoder_id_, bitrate_, enabled_, sync_with_main_,
		advanced_toggle_, audio_track_, reconnect_retries_, reconnect_delay_, buttons_};
	for (std::size_t index = 1; index < tab_order.size(); ++index) {
		setTabOrder(tab_order[index - 1], tab_order[index]);
	}

	update_server_url_mode();
	update_key_link();
	update_connect_visibility();
	update_custom_fields();
	set_advanced_expanded(false);
}

void DestinationEditorDialog::accept()
{
	if (saving_) {
		return;
	}
	clear_errors();

	const Destination destination = read_destination();
	std::vector<DestinationField> invalid;
	for (const ValidationIssue &issue : validate_destination(destination)) {
		const QString message = QString::fromStdString(localize(issue.message));
		const auto row = field_rows_.find(issue.field);
		if (row == field_rows_.end() || !show_field_error(row->second, message)) {
			show_error(save_error_, message);
		} else {
			invalid.push_back(issue.field);
		}
	}

	const std::string entered_key = key_->text().trimmed().toStdString();
	const auto key_problem = validate_stream_key_entry(entered_key, is_new_);
	if (key_problem) {
		show_field_error(key_row_, QString::fromStdString(localize(*key_problem)));
	}

	// Focus goes to the first invalid field in visual order.
	const auto is_invalid = [&invalid](DestinationField field) {
		return std::find(invalid.begin(), invalid.end(), field) != invalid.end();
	};
	QWidget *first_invalid = nullptr;
	for (const DestinationField field : {DestinationField::Name, DestinationField::ServerUrl}) {
		if (!first_invalid && is_invalid(field)) {
			first_invalid = field_rows_[field].field;
		}
	}
	if (!first_invalid && key_problem) {
		first_invalid = key_;
	}
	for (const DestinationField field :
	     {DestinationField::VideoEncoder, DestinationField::VideoBitrate, DestinationField::AudioTrack}) {
		if (!first_invalid && is_invalid(field)) {
			first_invalid = field_rows_[field].field;
		}
	}
	const bool valid = invalid.empty() && !key_problem && save_error_->isHidden();
	fit_to_contents();
	if (first_invalid) {
		first_invalid->setFocus(Qt::OtherFocusReason);
	}
	if (!valid) {
		return;
	}

	const std::optional<std::string> new_key = entered_key.empty() ? std::nullopt : std::make_optional(entered_key);
	set_saving(true);
	save_(destination, new_key, pending_refresh_token_,
	      [dialog = QPointer<DestinationEditorDialog>(this)](std::optional<QString> failure) {
		      if (dialog) {
			      dialog->finish_save(failure);
		      }
	      });
}

void DestinationEditorDialog::finish_save(const std::optional<QString> &failure)
{
	set_saving(false);
	if (failure) {
		show_error(save_error_, *failure);
		return;
	}
	pending_refresh_token_.reset();
	QDialog::accept();
}

void DestinationEditorDialog::set_saving(bool saving)
{
	saving_ = saving;
	buttons_->setEnabled(!saving);
	connect_twitch_->setEnabled(!saving && !flow_ && !reading_token_);
}

void DestinationEditorDialog::reject()
{
	if (saving_) {
		return;
	}
	if (secrets_ && keep_refresh_token_on_cancel(is_new_, had_stored_token_, pending_refresh_token_.has_value())) {
		secrets_->run<std::optional<SecretError>>(
			[id = initial_.id, token = *pending_refresh_token_](SecretStore &secrets) {
				std::optional<SecretError> error = TwitchTokenStore(secrets).put(id, token);
				if (error) {
					obs_log(LOG_WARNING,
						"could not store the rotated Twitch refresh token; the next connect opens the browser");
				}
				return error;
			},
			[](std::optional<SecretError>) {});
		pending_refresh_token_.reset();
	}
	QDialog::reject();
}

QLabel *DestinationEditorDialog::add_row(QFormLayout *form, const char *label_key, QWidget *field)
{
	auto *label = new QLabel(text(label_key), this);
	label->setBuddy(field);
	form->addRow(label, field);
	return label;
}

DestinationEditorDialog::FieldRow DestinationEditorDialog::add_field(QFormLayout *form, const char *label_key,
								      QWidget *field, bool advanced)
{
	add_row(form, label_key, field);
	FieldRow row;
	row.form = form;
	row.field = field;
	row.error = add_note(form, "text-danger");
	row.advanced = advanced;
	return row;
}

QLabel *DestinationEditorDialog::add_note(QFormLayout *form, const char *css_class)
{
	auto *note = new QLabel(this);
	note->setWordWrap(true);
	note->setTextFormat(Qt::PlainText);
	note->setProperty("class", QString::fromLatin1(css_class));
	note->hide();
	form->addRow(QString(), note);
	return note;
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
	destination.audio_track = audio_track_->currentData().toInt();
	destination.sync_with_main = sync_with_main_->isChecked();

	// The spin box ranges match the policy's limits, so this always succeeds.
	if (const auto policy = ReconnectPolicy::create(reconnect_retries_->value(), reconnect_delay_->value())) {
		destination.reconnect = *policy;
	}
	return destination;
}

Platform DestinationEditorDialog::current_platform() const
{
	return static_cast<Platform>(service_->currentData().toInt());
}

void DestinationEditorDialog::clear_errors()
{
	for (auto &[field, row] : field_rows_) {
		clear_error(row);
	}
	clear_error(key_row_);
	save_error_->hide();
}

void DestinationEditorDialog::clear_error(FieldRow &row)
{
	if (row.error->isHidden()) {
		return;
	}
	row.error->hide();
	row.field->setAccessibleDescription(QString());
}

bool DestinationEditorDialog::show_field_error(FieldRow &row, const QString &message)
{
	if (row.advanced) {
		set_advanced_expanded(true);
	}
	if (!row.form->isRowVisible(row.field)) {
		return false;
	}
	show_error(row.error, message);
	row.field->setAccessibleDescription(message);
	return true;
}

void DestinationEditorDialog::show_error(QLabel *label, const QString &message)
{
	label->setText(message);
	label->show();
}

void DestinationEditorDialog::service_picked()
{
	service_picked_ = true;
	const Platform platform = current_platform();
	server_url_->setText(
		QString::fromStdString(url_after_picking(platform, server_url_->text().trimmed().toStdString())));
	{
		const QSignalBlocker blocker(custom_server_);
		custom_server_->setChecked(false);
	}
	if (name_follows_service_) {
		name_->setText(platform == Platform::Custom ? QString()
							   : text(platform_text_key("Platform.", platform).c_str()));
	}
	clear_error(field_rows_[DestinationField::ServerUrl]);
	update_server_url_mode();
	update_key_link();
	update_connect_visibility();
}

void DestinationEditorDialog::update_server_url_mode()
{
	const bool has_preset = preset_ingest_url(current_platform()).has_value();
	const bool editable = !has_preset || custom_server_->isChecked();
	custom_server_->setVisible(has_preset);
	server_url_->setReadOnly(!editable);
	server_url_->setPlaceholderText(editable ? text("Editor.ServerUrlHint") : QString());
	update_service_hint();
	fit_to_contents();
}

void DestinationEditorDialog::update_service_hint()
{
	// Shown only while the user has to find a URL: services without a preset, or one with
	// a preset when the user chose a custom server.
	const Platform platform = current_platform();
	const bool typing_url = !preset_ingest_url(platform) || custom_server_->isChecked();
	const QString hint = platform == Platform::Custom
				     ? QString()
				     : text(platform_text_key("Editor.ServiceHint.", platform).c_str());
	// obs_module_text answers with the key itself when a service has no hint.
	const bool has_hint = !hint.isEmpty() && !hint.startsWith(QLatin1String("Editor."));
	service_hint_->setText(has_hint ? hint : QString());
	service_hint_->setVisible(has_hint && typing_url);
}

void DestinationEditorDialog::update_key_link()
{
	const Platform platform = current_platform();
	const auto page = stream_key_page_url(platform);
	if (!page) {
		key_link_->clear();
		key_link_->hide();
		fit_to_contents();
		return;
	}
	const QString label = text("Editor.GetKey").arg(text(platform_text_key("Platform.", platform).c_str()));
	key_link_->setText(QStringLiteral("<a href=\"%1\">%2</a>")
				   .arg(QString::fromStdString(*page).toHtmlEscaped(), label.toHtmlEscaped()));
	key_link_->setToolTip(QString::fromStdString(*page));
	key_link_->setAccessibleName(label);
	key_link_->show();
	fit_to_contents();
}

void DestinationEditorDialog::update_custom_fields()
{
	const bool custom = static_cast<EncoderMode>(encoder_mode_->currentData().toInt()) == EncoderMode::Custom;
	output_form_->setRowVisible(encoder_id_, custom);
	output_form_->setRowVisible(bitrate_, custom);
	if (!custom) {
		clear_error(field_rows_[DestinationField::VideoEncoder]);
		clear_error(field_rows_[DestinationField::VideoBitrate]);
	}
	fit_to_contents();
}

void DestinationEditorDialog::update_connect_visibility()
{
	const bool twitch = current_platform() == Platform::Twitch;
	connection_form_->setRowVisible(connect_row_, twitch);
	if (!twitch) {
		connect_status_->hide();
	}
	fit_to_contents();
}

void DestinationEditorDialog::toggle_key_visibility()
{
	key_shown_ = !key_shown_;
	key_->setEchoMode(key_shown_ ? QLineEdit::Normal : QLineEdit::Password);
	show_key_->setIcon(themed_icon(key_shown_ ? "icons/ui/hidden.svg" : "icons/ui/visible.svg", palette()));
	const QString action = text(key_shown_ ? "Editor.HideKey" : "Editor.ShowKey");
	// The action's text is its accessible name in the line edit's tool button.
	show_key_->setText(action);
	show_key_->setToolTip(action);
}

void DestinationEditorDialog::set_advanced_expanded(bool expanded)
{
	advanced_expanded_ = expanded;
	advanced_box_->setVisible(expanded);
	advanced_toggle_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
	advanced_toggle_->setText(text(expanded ? "Editor.HideAdvanced" : "Editor.ShowAdvanced"));
	fit_to_contents();
}

void DestinationEditorDialog::fit_to_contents()
{
	// Grows and shrinks with the rows shown, keeping any width the user gave it.
	if (QLayout *layout = this->layout()) {
		layout->activate();
		const int width = isVisible() ? std::max(this->width(), sizeHint().width()) : sizeHint().width();
		resize(width, heightForWidth(width) > 0 ? heightForWidth(width) : sizeHint().height());
	}
}

void DestinationEditorDialog::connect_twitch()
{
	if (!secrets_) {
		show_connect_status(text("Editor.SecretsUnavailable"), true);
		return;
	}
	if (flow_ || reading_token_ || saving_) {
		return;
	}

	// A token from an earlier connect in this dialog is newer than the stored one: Twitch
	// rotates refresh tokens on use. A new destination has no stored token to read.
	if (pending_refresh_token_ || is_new_) {
		start_connect_flow(pending_refresh_token_);
		return;
	}

	reading_token_ = true;
	connect_twitch_->setEnabled(false);
	show_connect_status(obs_module_text("Editor.ConnectTwitchWaiting"));
	secrets_->run<SecretLookup>(
		[id = initial_.id](SecretStore &secrets) { return TwitchTokenStore(secrets).get(id); },
		[dialog = QPointer<DestinationEditorDialog>(this)](SecretLookup stored) {
			if (!dialog) {
				return;
			}
			dialog->reading_token_ = false;
			std::optional<std::string> refresh_token;
			if (stored.secret && !stored.secret->empty()) {
				dialog->had_stored_token_ = true;
				refresh_token = std::move(stored.secret);
			}
			dialog->start_connect_flow(refresh_token);
		});
}

void DestinationEditorDialog::start_connect_flow(const std::optional<std::string> &refresh_token)
{
	flow_ = new TwitchConnectFlow(std::string(kTwitchClientId), twitch_redirect_uris(), this);
	connect(flow_, &TwitchConnectFlow::succeeded, this, &DestinationEditorDialog::on_connect_succeeded);
	connect(flow_, &TwitchConnectFlow::failed, this, &DestinationEditorDialog::on_connect_failed);

	connect_twitch_->setEnabled(false);
	show_connect_status(obs_module_text("Editor.ConnectTwitchWaiting"));
	flow_->start(refresh_token);
}

void DestinationEditorDialog::on_connect_succeeded(const QString &stream_key, const QString &channel_login,
						   const QString &refresh_token)
{
	obs_log(LOG_INFO, "Twitch connect succeeded as %s", channel_login.toUtf8().constData());
	cleanup_flow();
	key_->setText(stream_key);
	if (!refresh_token.isEmpty()) {
		pending_refresh_token_ = refresh_token.toStdString();
	}

	show_connect_status(QString::fromUtf8(obs_module_text("Editor.ConnectTwitchDone")).arg(channel_login));
}

void DestinationEditorDialog::on_connect_failed(const QString &message)
{
	obs_log(LOG_WARNING, "Twitch connect failed: %s", message.toUtf8().constData());
	cleanup_flow();
	show_connect_status(message, true);
}

void DestinationEditorDialog::cleanup_flow()
{
	if (flow_) {
		flow_->deleteLater();
		flow_ = nullptr;
	}
	connect_twitch_->setEnabled(!saving_);
}

void DestinationEditorDialog::show_connect_status(const QString &message, bool failure)
{
	set_theme_class(connect_status_, failure ? "text-danger" : "text-small text-muted");
	connect_status_->setText(message);
	connect_status_->setVisible(!message.isEmpty());
}

} // namespace multistream
