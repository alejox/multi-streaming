// Presentational: renders rows it is given and emits what the user asked for. It knows
// neither the engine nor libobs objects.
#pragma once

#include "destinations/stream_stats.hpp"

#include <QColor>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <map>
#include <optional>
#include <string>
#include <vector>

class QAbstractButton;
class QAction;
class QFrame;
class QLabel;
class QMenu;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace multistream {

enum class RowTone { Idle, Busy, Live, Warning, Failed };

struct DestinationRowStatus {
	QString state;
	RowTone tone = RowTone::Idle;
	// Why it failed. Empty hides it.
	QString detail;
	// A warning that does not stop anything, short; its explanation goes in the tooltip.
	// Empty hides it.
	QString warning;
	QString warning_hint;
	// Only while on air. Empty hides them.
	QString bitrate;
	QString dropped;
	DropLevel drop_level = DropLevel::Normal;
	bool can_start = false;
	bool can_stop = false;
	bool can_edit = false;
};

// The update notice at the top of the dock.
enum class UpdateBannerKind {
	Hidden,
	// Quiet: muted text, no action.
	Downloading,
	// Installs when OBS closes.
	Ready,
	// Newer version that must be downloaded by hand: offers the release page.
	Available,
	// Offers a retry.
	Failed,
};

struct DestinationRowView {
	QString id;
	QString name;
	QString source;
	// Read by screen readers for the platform icon.
	QString platform_name;
	// Relative to the plugin's data directory.
	std::string icon_file;
	// The platform's brand color; invalid when it has none. Used only when color icons are on.
	QColor brand_color;
	DestinationRowStatus status;
};

class DestinationsDock final : public QWidget {
	Q_OBJECT

public:
	explicit DestinationsDock(QWidget *parent = nullptr);

	void set_rows(const std::vector<DestinationRowView> &rows);
	void update_status(const QString &id, const DestinationRowStatus &status);
	void show_problem(const QString &problem);
	void set_color_icons(bool on);
	// Whether Start all and Stop all have anything to act on.
	void set_bulk_actions(bool can_start_all, bool can_stop_all);
	// `version` is shown as "v<version>"; unused for Hidden and Failed.
	void set_update_banner(UpdateBannerKind kind, const QString &version);
	void set_auto_update(bool on);

signals:
	void add_requested();
	void start_all_requested();
	void stop_all_requested();
	void start_requested(const QString &id);
	void stop_requested(const QString &id);
	void edit_requested(const QString &id);
	void delete_requested(const QString &id);
	void move_up_requested(const QString &id);
	void move_down_requested(const QString &id);
	// Drag and drop: put this destination right before the row now at insert_position
	// (the row count means the end).
	void move_to_requested(const QString &id, int insert_position);
	void color_icons_toggled(bool on);
	void auto_update_toggled(bool on);
	void update_download_requested();
	void update_retry_requested();

protected:
	void changeEvent(QEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	struct RowWidgets {
		QWidget *row = nullptr;
		QString name;
		QLabel *handle = nullptr;
		QLabel *icon = nullptr;
		QLabel *dot = nullptr;
		QLabel *state = nullptr;
		QLabel *detail = nullptr;
		QLabel *warning = nullptr;
		QLabel *bitrate = nullptr;
		QLabel *dropped = nullptr;
		QToolButton *toggle = nullptr;
		QMenu *menu = nullptr;
		QToolButton *more = nullptr;
		QAction *edit = nullptr;
		QAction *move_up = nullptr;
		QAction *move_down = nullptr;
		QAction *remove = nullptr;
		std::string icon_file;
		QColor brand_color;
		bool stops = false;
		RowTone tone = RowTone::Idle;
	};

	QWidget *create_row(const DestinationRowView &view, int position);
	void apply_status(RowWidgets &widgets, const DestinationRowStatus &status);
	void apply_row_icons(RowWidgets &widgets);
	void apply_row_icons_all();
	void apply_theme_icons();
	void start_drag(QWidget *handle, const QString &id);
	int drop_position(int y) const;
	void show_drop_indicator(int position);

	QFrame *problem_frame_ = nullptr;
	QLabel *problem_ = nullptr;
	QToolButton *dismiss_problem_ = nullptr;
	QFrame *update_frame_ = nullptr;
	QLabel *update_text_ = nullptr;
	QToolButton *update_action_ = nullptr;
	QToolButton *dismiss_update_ = nullptr;
	UpdateBannerKind update_kind_ = UpdateBannerKind::Hidden;
	QWidget *empty_ = nullptr;
	QLabel *empty_icon_ = nullptr;
	QLabel *empty_text_ = nullptr;
	QScrollArea *scroll_ = nullptr;
	QVBoxLayout *rows_layout_ = nullptr;
	QWidget *rows_container_ = nullptr;
	QFrame *drop_indicator_ = nullptr;
	QPushButton *start_all_ = nullptr;
	QPushButton *stop_all_ = nullptr;
	QToolButton *settings_ = nullptr;
	QMenu *settings_menu_ = nullptr;
	QAction *color_icons_action_ = nullptr;
	QAction *auto_update_action_ = nullptr;
	bool color_icons_on_ = false;
	std::map<QString, RowWidgets> rows_;
	int row_count_ = 0;
	// Drag handles to their destination id; rebuilt with the rows.
	std::map<QObject *, QString> handle_ids_;
	QObject *pressed_handle_ = nullptr;
	QPoint drag_start_;
	// Set by a drop and emitted once the drag returns, so the rows are rebuilt outside it.
	std::optional<std::pair<QString, int>> pending_move_;
};

} // namespace multistream
