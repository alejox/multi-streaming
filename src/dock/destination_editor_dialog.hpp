// Editor for one destination. The key field is never filled with a stored key: it only
// takes a new one, and blank means unchanged.
#pragma once

#include "destinations/destination.hpp"
#include "destinations/secret_store.hpp"
#include "destinations/validation.hpp"

#include <QDialog>
#include <QString>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace multistream {

class TwitchConnectFlow;

struct EncoderChoice {
	QString id;
	QString label;
};

class DestinationEditorDialog final : public QDialog {
	Q_OBJECT

public:
	// Returns why saving failed, or nothing to close the dialog.
	using SaveHandler = std::function<std::optional<QString>(const Destination &, const std::optional<std::string> &)>;

	DestinationEditorDialog(const Destination &initial, bool is_new, const std::vector<EncoderChoice> &encoders,
				SaveHandler save, SecretStore *secrets, QWidget *parent);

	void accept() override;

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
	void on_connect_succeeded(const QString &stream_key, const QString &channel_login, const QString &refresh_token);
	void on_connect_failed(const QString &message);
	void cleanup_flow();
	void show_connect_status(const QString &message);

	Destination initial_;
	bool is_new_;
	SaveHandler save_;
	SecretStore *secrets_ = nullptr;

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
