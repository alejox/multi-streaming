// Presentational: shows the vertical canvas's preview, scenes, and sources, and emits what
// the user asked for. It knows neither libobs objects nor where its data comes from.
#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <vector>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace multistream {

class VerticalPreview;

struct VerticalItemView {
	std::int64_t id = 0;
	QString name;
	bool visible = true;
};

class VerticalDock final : public QWidget {
	Q_OBJECT

public:
	explicit VerticalDock(QWidget *parent = nullptr);

	VerticalPreview *preview() const { return preview_; }

	void set_available(bool available);
	void set_scenes(const std::vector<QString> &scenes, const QString &active);
	void set_items(const std::vector<VerticalItemView> &items);
	void set_main_sources(const std::vector<QString> &sources);
	void set_resolution(int width, int height);
	void show_message(const QString &message);

signals:
	void scene_selected(const QString &name);
	void add_scene_requested();
	void rename_scene_requested(const QString &name);
	void remove_scene_requested(const QString &name);
	void add_source_requested(const QString &name);
	void remove_item_requested(std::int64_t id);
	void move_item_up_requested(std::int64_t id);
	void move_item_down_requested(std::int64_t id);
	void item_visibility_requested(std::int64_t id, bool visible);
	void fit_item_requested(std::int64_t id);
	void fill_item_requested(std::int64_t id);
	void resolution_requested(int width, int height);

private:
	std::optional<std::int64_t> selected_item() const;

	VerticalPreview *preview_;
	QWidget *controls_;
	QLabel *message_;
	QComboBox *scenes_;
	QListWidget *items_;
	QComboBox *main_sources_;
	QSpinBox *width_;
	QSpinBox *height_;
	bool updating_ = false;
};

} // namespace multistream
