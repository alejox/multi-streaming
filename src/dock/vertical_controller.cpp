#include "dock/vertical_controller.hpp"

#include "dock/vertical_preview.hpp"

#include <obs-module.h>

#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>

#include <algorithm>
#include <optional>
#include <vector>

namespace multistream {
namespace {

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

VerticalController::VerticalController(VerticalCanvas &canvas, StreamingEngine &engine, VerticalDock *dock)
	: canvas_(canvas),
	  engine_(engine),
	  dock_(dock)
{
	dock->preview()->set_target(canvas_.preview_target());
	dock->preview()->set_canvas(&canvas_);
	canvas_.set_changed_listener([this] { refresh(); });

	connect(dock, &VerticalDock::scene_selected, this,
		[this](const QString &name) { canvas_.switch_scene(name.toStdString()); });
	connect(dock, &VerticalDock::add_scene_requested, this, &VerticalController::add_scene);
	connect(dock, &VerticalDock::rename_scene_requested, this, &VerticalController::rename_scene);
	connect(dock, &VerticalDock::remove_scene_requested, this, &VerticalController::remove_scene);
	connect(dock, &VerticalDock::add_source_requested, this,
		[this](const QString &name) { canvas_.add_source(name.toStdString()); });
	connect(dock, &VerticalDock::remove_item_requested, this, &VerticalController::remove_item);
	connect(dock, &VerticalDock::item_selected, this, [this](std::int64_t id) {
		if (dock_) {
			dock_->preview()->set_selected_item(id != 0 ? std::make_optional(id) : std::nullopt);
		}
	});
	connect(dock->preview(), &VerticalPreview::selection_changed, this, [this](std::int64_t id) {
		if (dock_) {
			dock_->select_item(id != 0 ? std::make_optional(id) : std::nullopt);
		}
	});
	connect(dock->preview(), &VerticalPreview::context_menu_requested, this, &VerticalController::show_item_menu);
	connect(dock->preview(), &VerticalPreview::remove_requested, this, &VerticalController::remove_item);
	connect(dock->preview(), &VerticalPreview::item_edited, this, [this] { canvas_.save_collection(); });
	connect(dock, &VerticalDock::move_item_up_requested, this,
		[this](std::int64_t id) { canvas_.move_item(id, VerticalCanvas::Move::Up); });
	connect(dock, &VerticalDock::move_item_down_requested, this,
		[this](std::int64_t id) { canvas_.move_item(id, VerticalCanvas::Move::Down); });
	connect(dock, &VerticalDock::item_visibility_requested, this,
		[this](std::int64_t id, bool visible) { canvas_.set_item_visible(id, visible); });
	connect(dock, &VerticalDock::fit_item_requested, this, [this](std::int64_t id) { canvas_.fit_item(id); });
	connect(dock, &VerticalDock::fill_item_requested, this, [this](std::int64_t id) { canvas_.fill_item(id); });
	connect(dock, &VerticalDock::resolution_requested, this, &VerticalController::change_resolution);

	refresh();
}

VerticalController::~VerticalController()
{
	canvas_.set_changed_listener(nullptr);
	if (dock_) {
		dock_->preview()->set_canvas(nullptr);
		dock_->preview()->set_target(nullptr);
	}
}

void VerticalController::refresh()
{
	if (!dock_) {
		return;
	}

	dock_->set_available(canvas_.available());

	std::vector<QString> scenes;
	for (const std::string &scene : canvas_.scenes()) {
		scenes.push_back(QString::fromStdString(scene));
	}
	dock_->set_scenes(scenes, QString::fromStdString(canvas_.active_scene().value_or(std::string())));

	std::vector<VerticalItemView> items;
	for (const VerticalItem &item : canvas_.items()) {
		items.push_back({item.id, QString::fromStdString(item.name), item.visible});
	}
	dock_->set_items(items);

	std::vector<QString> sources;
	for (const std::string &source : canvas_.main_sources()) {
		sources.push_back(QString::fromStdString(source));
	}
	dock_->set_main_sources(sources);

	dock_->set_resolution(canvas_.settings().width, canvas_.settings().height);
}

void VerticalController::show_item_menu(std::int64_t id, const QPoint &global_position)
{
	QMenu menu(dock_);
	menu.addAction(text("Vertical.Fit"), this, [this, id] { canvas_.fit_item(id); });
	menu.addAction(text("Vertical.Fill"), this, [this, id] { canvas_.fill_item(id); });
	menu.addAction(text("Vertical.Center"), this, [this, id] { canvas_.center_item(id); });
	menu.addAction(text("Vertical.ResetTransform"), this, [this, id] { canvas_.reset_item_transform(id); });
	QMenu *order = menu.addMenu(text("Vertical.Order"));
	order->addAction(text("Vertical.OrderTop"), this, [this, id] { canvas_.move_item(id, VerticalCanvas::Move::Top); });
	order->addAction(text("Vertical.MoveUp"), this, [this, id] { canvas_.move_item(id, VerticalCanvas::Move::Up); });
	order->addAction(text("Vertical.MoveDown"), this, [this, id] { canvas_.move_item(id, VerticalCanvas::Move::Down); });
	order->addAction(text("Vertical.OrderBottom"), this,
			 [this, id] { canvas_.move_item(id, VerticalCanvas::Move::Bottom); });
	menu.addSeparator();
	menu.addAction(text("Vertical.RemoveSource"), this, [this, id] { remove_item(id); });
	menu.exec(global_position);
	canvas_.save_collection();
}

void VerticalController::remove_item(std::int64_t id)
{
	const std::vector<VerticalItem> items = canvas_.items();
	const auto found = std::find_if(items.begin(), items.end(), [id](const VerticalItem &item) { return item.id == id; });
	if (found == items.end()) {
		return;
	}
	const QString question = text("Vertical.RemoveSourceQuestion").arg(QString::fromStdString(found->name));
	if (QMessageBox::question(dock_, text("Vertical.RemoveSourceTitle"), question) == QMessageBox::Yes) {
		canvas_.remove_item(id);
		canvas_.save_collection();
	}
}

void VerticalController::add_scene()
{
	bool accepted = false;
	const QString name = QInputDialog::getText(dock_, text("Vertical.AddScene"), text("Vertical.SceneName"),
						   QLineEdit::Normal, text("Vertical.DefaultScene"), &accepted);
	if (accepted) {
		if (const auto added = canvas_.add_scene(name.toStdString())) {
			canvas_.switch_scene(*added);
		}
	}
}

void VerticalController::rename_scene(const QString &name)
{
	bool accepted = false;
	const QString new_name = QInputDialog::getText(dock_, text("Vertical.RenameScene"), text("Vertical.SceneName"),
						       QLineEdit::Normal, name, &accepted);
	if (accepted && new_name != name && !canvas_.rename_scene(name.toStdString(), new_name.toStdString()) && dock_) {
		dock_->show_message(text("Vertical.RenameRefused"));
	}
}

void VerticalController::remove_scene(const QString &name)
{
	if (!canvas_.remove_scene(name.toStdString()) && dock_) {
		dock_->show_message(text("Vertical.LastScene"));
	}
}

void VerticalController::change_resolution(int width, int height)
{
	switch (decide_resolution_change(canvas_.settings(), width, height, engine_.video_in_use(VideoSource::Vertical))) {
	case ResolutionChange::Apply:
		if (!canvas_.apply_resolution(width, height) && dock_) {
			dock_->show_message(text("Vertical.ResolutionFailed"));
		}
		break;
	case ResolutionChange::Unchanged:
		break;
	case ResolutionChange::Invalid:
		if (dock_) {
			dock_->show_message(text("Vertical.ResolutionInvalid"));
		}
		break;
	case ResolutionChange::RefusedWhileLive:
		if (dock_) {
			dock_->set_resolution(canvas_.settings().width, canvas_.settings().height);
			dock_->show_message(text("Vertical.ResolutionRefused"));
		}
		break;
	}
}

} // namespace multistream
