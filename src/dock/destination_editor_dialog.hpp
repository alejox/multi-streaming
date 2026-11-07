// Editor for one destination. The key field is never filled with a stored key: it only
// takes a new one, and blank means unchanged. Secret store calls never run on the UI
// thread: the Twitch token read goes through the SecretWorker, and Save waits for the
// handler's answer with its buttons disabled.
#pragma once

#include "destinations/destination.hpp"
#include "destinations/validation.hpp"

#include <QDialog>
#include <QString>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QDialogButtonBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

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
	QLabel *add_field(QFormLayout *form, const char *label_key, QWidget *field);
	Destination read_destination() const;
	void clear_errors();
	void show_error(QLabel *label, const QString &message);
	void update_custom_fields();
	void service_picked();
	void update_service_hint();
	void update_connect_visibility();
	void connect_twitch();
	void start_connect_flow(const std::optional<std::string> &refresh_token);
	void finish_save(const std::optional<QString> &failure);
	void set_saving(bool saving);
	void on_connect_succeeded(const QString &stream_key, const QString &channel_login, const QString &refresh_token);
	void on_connect_failed(const QString &message);
	void cleanup_flow();
	void show_connect_status(const QString &message);

	Destination initial_;
	bool is_new_;
	SaveHandler save_;
	SecretWorker *secrets_ = nullptr;
	bool saving_ = false;
	bool reading_token_ = false;
	// Whether a connect started from a stored refresh token, which Twitch then rotated.
	bool had_stored_token_ = false;
	QDialogButtonBox *buttons_ = nullptr;

	QLineEdit *name_ = nullptr;
	QComboBox *service_ = nullptr;
	QLabel *service_hint_ = nullptr;
	bool service_picked_ = false;
	QLineEdit *server_url_ = nullptr;
	QLineEdit *key_ = nullptr;
	QCheckBox *show_key_ = nullptr;
	QPushButton *connect_twitch_ = nullptr;
	QLabel *connect_status_ = nullptr;
	TwitchConnectFlow *flow_ = nullptr;
	// The refresh token from the last successful Twitch connect. It is written to the
	// secret store when the dialog saves; on Cancel only when it replaces a stored one
	// (keep_refresh_token_on_cancel), so a cancelled new destination leaves nothing.
	std::optional<std::string> pending_refresh_token_;
	QComboBox *video_source_ = nullptr;
	QComboBox *encoder_mode_ = nullptr;
	QComboBox *encoder_id_ = nullptr;
	QSpinBox *bitrate_ = nullptr;
	QSpinBox *audio_track_ = nullptr;
	QCheckBox *enabled_ = nullptr;
	QCheckBox *sync_with_main_ = nullptr;
	QSpinBox *reconnect_retries_ = nullptr;
	QSpinBox *reconnect_delay_ = nullptr;

	std::map<DestinationField, QLabel *> field_errors_;
	QLabel *key_error_ = nullptr;
	QLabel *save_error_ = nullptr;
};

} // namespace multistream
