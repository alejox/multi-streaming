// Container for the vertical dock: connects what the user asks for to the canvas. It also
// asks the engine before changing the resolution, because libobs would otherwise drop
// video that a live destination's encoder is using.
#pragma once

#include "dock/vertical_dock.hpp"
#include "streaming/streaming_engine.hpp"
#include "vertical_canvas/vertical_canvas.hpp"

#include <QObject>
#include <QPointer>

#include <string>
#include <vector>

namespace multistream {

class VerticalController final : public QObject {
	Q_OBJECT

public:
	// The dock belongs to OBS's main window and can outlive this controller.
	VerticalController(VerticalCanvas &canvas, StreamingEngine &engine, VerticalDock *dock);
	~VerticalController() override;

	// Asks for a name, adds a new source of that type, and opens its properties.
	void add_new_source(const InputType &type);
	// Asks which of the existing sources to add, with a search box.
	void add_existing_source();
	void change_resolution(int width, int height);

private:
	void refresh();
	void show_item_menu(std::int64_t id, const QPoint &global_position);
	void show_add_source_menu(const QPoint &global_position);
	void select_new_item(std::int64_t id);
	void remove_item(std::int64_t id);
	void remove_hidden_audio();
	void add_scene();
	void rename_scene(const QString &name, const QString &new_name);
	void remove_scene(const QString &name);
	void edit_resolution();

	VerticalCanvas &canvas_;
	StreamingEngine &engine_;
	QPointer<VerticalDock> dock_;
};

} // namespace multistream
