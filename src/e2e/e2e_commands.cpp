#include "e2e/e2e_commands.hpp"

#include "destinations/vertical_settings.hpp"
#include "dock/destinations_dock.hpp"
#include "dock/vertical_controller.hpp"
#include "dock/vertical_dock.hpp"
#include "dock/vertical_preview.hpp"
#include "streaming/streaming_engine.hpp"
#include "vertical_canvas/vertical_canvas.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QImageReader>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QToolButton>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace multistream {

E2eCommands::E2eCommands(std::string path, StreamingEngine &engine, VerticalCanvas &canvas, VerticalDock *dock,
			 DestinationsDock *destinations_dock, const std::unique_ptr<VerticalController> &controller)
	: path_(std::move(path)),
	  engine_(engine),
	  canvas_(canvas),
	  dock_(dock),
	  destinations_dock_(destinations_dock),
	  controller_(controller)
{
	connect(&timer_, &QTimer::timeout, this, &E2eCommands::poll);
	timer_.start(100);
	connect(&answer_timer_, &QTimer::timeout, this, [this] {
		if (auto_answer_) {
			answer_dialogs();
		}
	});
	answer_timer_.start(150);
}

void E2eCommands::poll()
{
	std::ifstream file(path_);
	if (!file) {
		return;
	}

	std::vector<std::string> lines;
	for (std::string line; std::getline(file, line);) {
		lines.push_back(line);
	}
	file.close();
	std::remove(path_.c_str());

	for (const std::string &line : lines) {
		std::istringstream fields(line);
		std::string command;
		std::string argument;
		fields >> command;
		std::getline(fields >> std::ws, argument);
		// Queued, because a command can sit in a dialog's event loop, and macOS does not
		// fire timers in a loop nested inside a timer's own callback.
		QMetaObject::invokeMethod(
			this,
			[this, command, argument] {
				obs_log(LOG_INFO, "e2e command: %s %s", command.c_str(), argument.c_str());
				run(command, argument);
			},
			Qt::QueuedConnection);
	}
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
		obs_log(LOG_INFO, "e2e add_source: %d", canvas_.add_source(argument).has_value());
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
		} else if (argument == "lock") {
			canvas_.set_item_locked(id, true);
		} else if (argument == "unlock") {
			canvas_.set_item_locked(id, false);
		} else if (argument == "center") {
			canvas_.center_item(id);
		} else if (argument == "reset") {
			canvas_.reset_item_transform(id);
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
	} else if (command == "show_destinations") {
		if (destinations_dock_ && destinations_dock_->parentWidget() != nullptr) {
			destinations_dock_->parentWidget()->show();
		}
	} else if (command == "resize_dock") {
		std::istringstream fields(argument);
		std::string which;
		int width = 0;
		int height = 0;
		fields >> which >> width >> height;
		QWidget *widget = which == "vertical" ? static_cast<QWidget *>(dock_) : destinations_dock_.data();
		if (widget != nullptr && widget->parentWidget() != nullptr) {
			widget->parentWidget()->resize(width, height);
		}
	} else if (command == "auto_answer") {
		auto_answer_ = argument == "on";
	} else if (command == "grab") {
		grab(argument);
	} else if (command == "overlay_probe") {
		VerticalPreview *preview = dock_->preview();
		const qreal ratio = preview->devicePixelRatioF();
		const int width = static_cast<int>(preview->width() * ratio);
		const int height = static_cast<int>(preview->height() * ratio);
		const CanvasSize size = canvas_.canvas_size();
		const PreviewMapping mapping(static_cast<int>(size.width), static_cast<int>(size.height), width, height);
		const auto selected = preview->selected_item();
		const std::optional<PreviewItem> item = selected ? canvas_.preview_item(*selected) : std::nullopt;
		if (item) {
			const Vec2 first = mapping.to_window(item->box.origin);
			const Vec2 last = mapping.to_window(box_point(item->box, 1.0, 1.0));
			obs_log(LOG_INFO, "e2e overlay expected box in window pixels: %.1f,%.1f-%.1f,%.1f ratio %.1f window %dx%d",
				first.x, first.y, last.x, last.y, ratio, width, height);
		}
		obs_log(LOG_INFO, "e2e overlay red pixels: %s",
			canvas_.preview_target()
				->probe_overlay(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height))
				.c_str());
	} else if (command == "float_dock") {
		if (auto *parent = qobject_cast<QDockWidget *>(dock_->parentWidget())) {
			parent->setFloating(true);
		}
	} else if (command == "source_list_select") {
		// As a click in the list does.
		auto *list = dock_->findChild<QListWidget *>("vertical-sources");
		list->setCurrentRow(std::stoi(argument));
		obs_log(LOG_INFO, "e2e list row %s selects in preview: %lld", argument.c_str(),
			static_cast<long long>(dock_->preview()->selected_item().value_or(0)));
	} else if (command == "dock_widgets") {
		log_dock_widgets();
	} else if (command == "svg_formats") {
		obs_log(LOG_INFO, "e2e svg image format available: %d",
			QImageReader::supportedImageFormats().contains("svg"));
	} else if (command == "list_types") {
		for (const InputType &type : canvas_.creatable_input_types()) {
			obs_log(LOG_INFO, "e2e type %s (%s)", type.id.c_str(), type.display_name.c_str());
		}
		for (const std::string &name : canvas_.main_sources()) {
			obs_log(LOG_INFO, "e2e existing video source %s", name.c_str());
		}
	} else if (command == "new_source") {
		// The Add source menu's own action. A new source is named by its dialog.
		if (controller_) {
			for (const InputType &type : canvas_.creatable_input_types()) {
				if (type.unversioned_id == argument) {
					controller_->add_new_source(type);
				}
			}
		}
	} else if (command == "scene_list_rename") {
		std::istringstream names(argument);
		std::string from;
		std::string to;
		names >> from >> to;
		emit dock_->rename_scene_requested(QString::fromStdString(from), QString::fromStdString(to));
	} else if (command == "remove_scene_requested") {
		emit dock_->remove_scene_requested(QString::fromStdString(argument));
	} else if (command == "resolution_ui") {
		std::istringstream sides(argument);
		int width = 0;
		int height = 0;
		sides >> width >> height;
		if (controller_) {
			controller_->change_resolution(width, height);
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

// "<vertical|destinations> <file>". QWidget::grab of this plugin's own docks only. The
// preview is a native surface, so it comes out blank.
void E2eCommands::grab(const std::string &argument)
{
	std::istringstream fields(argument);
	std::string which;
	std::string file;
	fields >> which;
	std::getline(fields >> std::ws, file);

	QWidget *widget = which == "vertical" ? static_cast<QWidget *>(dock_) : destinations_dock_.data();
	if (widget == nullptr) {
		return;
	}
	obs_log(LOG_INFO, "e2e grab %s: %dx%d saved=%d", which.c_str(), widget->width(), widget->height(),
		widget->grab().save(QString::fromStdString(file)));
}

// Says yes to this plugin's dialogs, so that their real code runs.
void E2eCommands::answer_dialogs()
{
	QWidget *modal = QApplication::activeModalWidget();
	if (auto *box = qobject_cast<QMessageBox *>(modal)) {
		obs_log(LOG_INFO, "e2e dialog question: %s", box->text().toUtf8().constData());
		if (QAbstractButton *yes = box->button(QMessageBox::Yes)) {
			yes->click();
		} else {
			box->accept();
		}
	} else if (auto *input = qobject_cast<QInputDialog *>(modal)) {
		obs_log(LOG_INFO, "e2e dialog name: %s", input->textValue().toUtf8().constData());
		input->accept();
	}
}

void E2eCommands::log_dock_widgets()
{
	for (const QToolButton *button : dock_->findChildren<QToolButton *>()) {
		obs_log(LOG_INFO, "e2e tool: accessible='%s' tooltip='%s' icon=%d enabled=%d",
			button->accessibleName().toUtf8().constData(), button->toolTip().toUtf8().constData(),
			!button->icon().isNull(), button->isEnabled());
	}
	for (const QListWidget *list : dock_->findChildren<QListWidget *>()) {
		obs_log(LOG_INFO, "e2e list rows=%d current=%d size=%dx%d", list->count(), list->currentRow(),
			list->width(), list->height());
	}
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
