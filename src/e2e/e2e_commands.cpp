#include "e2e/e2e_commands.hpp"

#include "destinations/vertical_settings.hpp"
#include "dock/vertical_dock.hpp"
#include "dock/vertical_preview.hpp"
#include "streaming/streaming_engine.hpp"
#include "vertical_canvas/vertical_canvas.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace multistream {

E2eCommands::E2eCommands(std::string path, StreamingEngine &engine, VerticalCanvas &canvas, VerticalDock *dock)
	: path_(std::move(path)),
	  engine_(engine),
	  canvas_(canvas),
	  dock_(dock)
{
	connect(&timer_, &QTimer::timeout, this, &E2eCommands::poll);
	timer_.start(100);
}

void E2eCommands::poll()
{
	std::ifstream file(path_);
	if (!file) {
		return;
	}

	std::string line;
	while (std::getline(file, line)) {
		std::istringstream fields(line);
		std::string command;
		std::string argument;
		fields >> command;
		std::getline(fields >> std::ws, argument);
		obs_log(LOG_INFO, "e2e command: %s %s", command.c_str(), argument.c_str());
		run(command, argument);
	}
	file.close();
	std::remove(path_.c_str());
}

void E2eCommands::run(const std::string &command, const std::string &argument)
{
	if (command == "start_stream") {
		obs_frontend_streaming_start();
	} else if (command == "stop_stream") {
		obs_frontend_streaming_stop();
	} else if (command == "start") {
		engine_.start({argument});
	} else if (command == "stop") {
		engine_.stop({argument});
	} else if (command == "dump") {
		dump_vertical();
	} else if (command == "add_scene") {
		canvas_.add_scene(argument);
	} else if (command == "switch_scene") {
		obs_log(LOG_INFO, "e2e switch_scene: %d", canvas_.switch_scene(argument));
	} else if (command == "remove_scene") {
		obs_log(LOG_INFO, "e2e remove_scene: %d", canvas_.remove_scene(argument));
	} else if (command == "rename_scene") {
		std::istringstream names(argument);
		std::string from;
		std::string to;
		names >> from >> to;
		obs_log(LOG_INFO, "e2e rename_scene: %d", canvas_.rename_scene(from, to));
	} else if (command == "add_source") {
		obs_log(LOG_INFO, "e2e add_source: %d", canvas_.add_source(argument));
	} else if (command == "top_item") {
		// One verb per action on the topmost item, enough for the checks.
		const auto items = canvas_.items();
		if (items.empty()) {
			return;
		}
		const std::int64_t id = items.front().id;
		if (argument == "fill") {
			canvas_.fill_item(id);
		} else if (argument == "fit") {
			canvas_.fit_item(id);
		} else if (argument == "hide") {
			canvas_.set_item_visible(id, false);
		} else if (argument == "show") {
			canvas_.set_item_visible(id, true);
		} else if (argument == "down") {
			canvas_.move_item(id, VerticalCanvas::Move::Down);
		} else if (argument == "remove") {
			canvas_.remove_item(id);
		}
	} else if (command == "resolution") {
		std::istringstream sides(argument);
		int width = 0;
		int height = 0;
		sides >> width >> height;
		const ResolutionChange decision = decide_resolution_change(canvas_.settings(), width, height,
									   engine_.video_in_use(VideoSource::Vertical));
		const bool applied = decision == ResolutionChange::Apply && canvas_.apply_resolution(width, height);
		obs_log(LOG_INFO, "e2e resolution: decision %d applied %d", static_cast<int>(decision), applied);
	} else if (command == "set_collection") {
		obs_frontend_set_current_scene_collection(argument.c_str());
	} else if (command == "set_profile") {
		obs_frontend_set_current_profile(argument.c_str());
	} else if (command == "show_dock") {
		// The dock's parent is the QDockWidget that OBS made for it.
		if (QWidget *parent = dock_->parentWidget()) {
			parent->show();
		}
	} else if (command == "preview_mouse") {
		preview_mouse(argument);
	} else if (command == "preview_key") {
		preview_key(argument);
	} else if (command == "dump_items") {
		dump_items();
	} else if (command == "preview_stats") {
		obs_log(LOG_INFO, "e2e preview frames: %llu",
			static_cast<unsigned long long>(canvas_.preview_target()->rendered_frames()));
	}
}

// "press|move|release <canvas x> <canvas y> [shift] [nosnap]": real Qt events, delivered to
// the widget the way a mouse would, at the spot that shows this canvas point.
void E2eCommands::preview_mouse(const std::string &argument)
{
	VerticalPreview *preview = dock_->preview();
	std::istringstream fields(argument);
	std::string action;
	double x = 0.0;
	double y = 0.0;
	fields >> action >> x >> y;

	Qt::KeyboardModifiers modifiers;
	std::string flag;
	while (fields >> flag) {
		if (flag == "shift") {
			modifiers |= Qt::ShiftModifier;
		} else if (flag == "nosnap") {
			modifiers |= Qt::ControlModifier;
		}
	}

	const double ratio = preview->devicePixelRatioF();
	const CanvasSize size = canvas_.canvas_size();
	const PreviewMapping mapping(static_cast<int>(size.width), static_cast<int>(size.height),
				     static_cast<int>(preview->width() * ratio),
				     static_cast<int>(preview->height() * ratio));
	const Vec2 window = mapping.to_window({x, y});
	const QPointF local(window.x / ratio, window.y / ratio);
	const QPointF global = preview->mapToGlobal(local);

	QEvent::Type type = QEvent::MouseMove;
	Qt::MouseButton button = Qt::NoButton;
	Qt::MouseButtons buttons = Qt::LeftButton;
	if (action == "press") {
		type = QEvent::MouseButtonPress;
		button = Qt::LeftButton;
	} else if (action == "release") {
		type = QEvent::MouseButtonRelease;
		button = Qt::LeftButton;
		buttons = Qt::NoButton;
	} else if (action == "hover") {
		buttons = Qt::NoButton;
	}
	QMouseEvent event(type, local, global, button, buttons, modifiers);
	QCoreApplication::sendEvent(preview, &event);
	obs_log(LOG_INFO, "e2e preview_mouse %s: selected=%lld cursor=%d", action.c_str(),
		static_cast<long long>(preview->selected_item().value_or(0)), static_cast<int>(preview->cursor().shape()));
}

// "<left|right|up|down|delete> [shift]". Sends the shortcut check first, as Qt does.
void E2eCommands::preview_key(const std::string &argument)
{
	VerticalPreview *preview = dock_->preview();
	std::istringstream fields(argument);
	std::string name;
	std::string flag;
	fields >> name >> flag;

	int key = Qt::Key_Left;
	if (name == "right") {
		key = Qt::Key_Right;
	} else if (name == "up") {
		key = Qt::Key_Up;
	} else if (name == "down") {
		key = Qt::Key_Down;
	} else if (name == "delete") {
		key = Qt::Key_Delete;
	}
	const Qt::KeyboardModifiers modifiers = flag == "shift" ? Qt::ShiftModifier : Qt::NoModifier;

	QKeyEvent shortcut(QEvent::ShortcutOverride, key, modifiers);
	shortcut.ignore();
	QCoreApplication::sendEvent(preview, &shortcut);
	QKeyEvent press(QEvent::KeyPress, key, modifiers);
	QCoreApplication::sendEvent(preview, &press);
	obs_log(LOG_INFO, "e2e preview_key %s: claimed shortcut=%d", name.c_str(), shortcut.isAccepted());
}

void E2eCommands::dump_items()
{
	for (const VerticalItem &view : canvas_.items()) {
		const std::optional<PreviewItem> item = canvas_.preview_item(view.id);
		if (!item) {
			obs_log(LOG_INFO, "e2e item %lld %s type=%s video=%d: no box", static_cast<long long>(view.id),
				view.name.c_str(), view.type_id.c_str(), view.has_video);
			continue;
		}
		obs_log(LOG_INFO,
			"e2e item %lld %s type=%s video=%d visible=%d locked=%d box origin=%.2f,%.2f size=%.2fx%.2f rot=%.1f",
			static_cast<long long>(view.id), view.name.c_str(), view.type_id.c_str(), view.has_video,
			item->visible, item->locked, item->box.origin.x, item->box.origin.y, item->box.width,
			item->box.height, item->box.rotation_degrees);
	}
}

void E2eCommands::dump_vertical()
{
	std::string scenes;
	for (const std::string &scene : canvas_.scenes()) {
		scenes += scene + ",";
	}
	std::string items;
	for (const VerticalItem &item : canvas_.items()) {
		items += item.name + (item.visible ? "(shown)," : "(hidden),");
	}
	obs_log(LOG_INFO, "e2e vertical: available=%d %dx%d active=%s scenes=[%s] items=[%s]", canvas_.available(),
		canvas_.settings().width, canvas_.settings().height, canvas_.active_scene().value_or("").c_str(),
		scenes.c_str(), items.c_str());
}

} // namespace multistream
