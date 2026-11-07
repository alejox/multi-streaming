// Presentational: shows the vertical canvas's preview, scenes, and sources, and emits what
// the user asked for. It knows neither libobs objects nor where its data comes from.
#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class QLabel;
class QListWidget;
class QSplitter;
class QToolButton;

namespace multistream {

class VerticalPreview;

struct VerticalItemView {
	std::int64_t id = 0;
	QString name;
	// Relative to the plugin's data directory.
	std::string icon_file;
	bool visible = true;
	bool locked = false;
};

class VerticalDock final : public QWidget {
	Q_OBJECT

public:
	explicit VerticalDock(QWidget *parent = nullptr);

	VerticalPreview *preview() const { return preview_; }

	void set_available(bool available);
	void set_scenes(const std::vector<QString> &scenes, const QString &active);
	void set_items(const std::vector<VerticalItemView> &items);
	void select_item(std::optional<std::int64_t> id);
	void set_resolution(int width, int height);
	void set_hidden_audio_items(int count);
	void show_message(const QString &message);

signals:
	void scene_selected(const QString &name);
	void add_scene_requested();
	void rename_scene_requested(const QString &name, const QString &new_name);
	void remove_scene_requested(const QString &name);
	void move_scene_requested(const QString &name, bool up);
	void add_source_menu_requested(const QPoint &global_position);
	void remove_item_requested(std::int64_t id);
	void move_item_up_requested(std::int64_t id);
	void move_item_down_requested(std::int64_t id);
	void item_selected(std::int64_t id);
	void item_visibility_requested(std::int64_t id, bool visible);
	void item_lock_requested(std::int64_t id, bool locked);
	void item_properties_requested(std::int64_t id);
	void remove_hidden_audio_requested();
	void resolution_settings_requested();

protected:
	void changeEvent(QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	struct IconButton {
		QToolButton *button;
		// Used only when the theme has no icon for the button's class.
		const char *fallback_icon_file;
	};

	QToolButton *icon_button(const char *css_class, const char *fallback_icon_file, const char *tooltip_key);
	// Delete (and Backspace on macOS, like OBS) while the list itself has the focus.
	void add_delete_shortcut(QListWidget *list, QToolButton *remove);
	QWidget *list_panel(const char *title_key, QListWidget *list, const std::vector<QToolButton *> &tools);
	QWidget *item_row(const VerticalItemView &view);
	void update_row(QWidget *row, const VerticalItemView &view);
	void apply_icons();
	std::optional<std::int64_t> selected_item() const;
	QString selected_scene() const;

	VerticalPreview *preview_;
	QWidget *controls_;
	QLabel *message_;
	QLabel *empty_hint_;
	QLabel *resolution_;
	QLabel *audio_note_;
	QSplitter *lists_;
	QListWidget *scenes_;
	QListWidget *items_;
	std::vector<IconButton> icon_buttons_;
	std::vector<VerticalItemView> shown_items_;
	bool updating_ = false;
};

} // namespace multistream
