// Editor for one destination. The key field is never filled with a stored key: it only
// takes a new one, and blank means unchanged. Secret store calls never run on the UI
// thread: the Twitch token read goes through the SecretWorker, and Save waits for the
// handler's answer with its buttons disabled.
#pragma once

#include "destinations/destination.hpp"
#include "destinations/platform.hpp"
#include "destinations/validation.hpp"

#include <QDialog>
#include <QString>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

class QAction;
class QCheckBox;
class QDialogButtonBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;
class QWidget;

namespace multistream {

class SecretWorker;
class TwitchConnectFlow;

struct EncoderChoice {
	QString id;
	QString label;
};

class DestinationEditorDialog final : public QDialog {
	Q_OBJECT

public:
	// Called once on the UI thread with why saving failed, or nothing to close the dialog.
	using SaveDone = std::function<void(std::optional<QString>)>;
	// Saves the destination and stores the new key and refresh token, if any. May answer
	// before returning or later.
	using SaveHandler = std::function<void(const Destination &, const std::optional<std::string> &new_key,
					       const std::optional<std::string> &refresh_token, SaveDone)>;

	// secrets may be null when no secret store is available.
	DestinationEditorDialog(const Destination &initial, bool is_new, const std::vector<EncoderChoice> &encoders,
				SaveHandler save, SecretWorker *secrets, QWidget *parent);

	void accept() override;
	// Refused while a save is in flight, which already wrote the config.
	void reject() override;

private:
	// One form row's field and the error line under it.
	struct FieldRow {
		QFormLayout *form = nullptr;
		QWidget *field = nullptr;
		QLabel *error = nullptr;
		bool advanced = false;
	};

	QLabel *add_row(QFormLayout *form, const char *label_key, QWidget *field);
	FieldRow add_field(QFormLayout *form, const char *label_key, QWidget *field, bool advanced = false);
	QLabel *add_note(QFormLayout *form, const char *css_class);
	Destination read_destination() const;
	Platform current_platform() const;
	void clear_errors();
	void clear_error(FieldRow &row);
	// Shows the message under the row's field; returns whether it could (the row is visible).
	bool show_field_error(FieldRow &row, const QString &message);
	void show_error(QLabel *label, const QString &message);
	void update_custom_fields();
	void service_picked();
	void update_server_url_mode();
	void update_service_hint();
	void update_key_link();
	void update_connect_visibility();
	void toggle_key_visibility();
	void set_advanced_expanded(bool expanded);
	void fit_to_contents();
	void connect_twitch();
	void start_connect_flow(const std::optional<std::string> &refresh_token);
	void finish_save(const std::optional<QString> &failure);
	void set_saving(bool saving);
	void on_connect_succeeded(const QString &stream_key, const QString &channel_login, const QString &refresh_token);
	void on_connect_failed(const QString &message);
	void cleanup_flow();
	void show_connect_status(const QString &message, bool failure = false);

	Destination initial_;
	bool is_new_;
	SaveHandler save_;
	SecretWorker *secrets_ = nullptr;
	bool saving_ = false;
	bool reading_token_ = false;
	// Whether a connect started from a stored refresh token, which Twitch then rotated.
	bool had_stored_token_ = false;
	QDialogButtonBox *buttons_ = nullptr;

	QComboBox *service_ = nullptr;
	bool service_picked_ = false;
	QLineEdit *name_ = nullptr;
	// A new destination takes the service's name until the user types one.
	bool name_follows_service_ = false;
	QFormLayout *connection_form_ = nullptr;
	QLineEdit *server_url_ = nullptr;
	QCheckBox *custom_server_ = nullptr;
	QLabel *service_hint_ = nullptr;
	QLineEdit *key_ = nullptr;
	QAction *show_key_ = nullptr;
	bool key_shown_ = false;
	QLabel *key_link_ = nullptr;
	QWidget *connect_row_ = nullptr;
	QPushButton *connect_twitch_ = nullptr;
	QLabel *connect_status_ = nullptr;
	TwitchConnectFlow *flow_ = nullptr;
	// The refresh token from the last successful Twitch connect. It is written to the
	// secret store when the dialog saves; on Cancel only when it replaces a stored one
	// (keep_refresh_token_on_cancel), so a cancelled new destination leaves nothing.
	std::optional<std::string> pending_refresh_token_;
	QFormLayout *output_form_ = nullptr;
	QComboBox *video_source_ = nullptr;
	QComboBox *encoder_mode_ = nullptr;
	QComboBox *encoder_id_ = nullptr;
	QSpinBox *bitrate_ = nullptr;
	QCheckBox *enabled_ = nullptr;
	QCheckBox *sync_with_main_ = nullptr;
	QToolButton *advanced_toggle_ = nullptr;
	QWidget *advanced_box_ = nullptr;
	bool advanced_expanded_ = false;
	QComboBox *audio_track_ = nullptr;
	QSpinBox *reconnect_retries_ = nullptr;
	QSpinBox *reconnect_delay_ = nullptr;

	// In visual order, so the first invalid one gets the focus.
	std::map<DestinationField, FieldRow> field_rows_;
	FieldRow key_row_;
	QLabel *save_error_ = nullptr;
};

} // namespace multistream
