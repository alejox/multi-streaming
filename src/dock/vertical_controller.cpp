#include "dock/vertical_controller.hpp"

#include "dock/vertical_preview.hpp"

#include <obs-module.h>

#include <QInputDialog>

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
	canvas_.set_changed_listener([this] { refresh(); });

	connect(dock, &VerticalDock::scene_selected, this,
		[this](const QString &name) { canvas_.switch_scene(name.toStdString()); });
	connect(dock, &VerticalDock::add_scene_requested, this, &VerticalController::add_scene);
	connect(dock, &VerticalDock::rename_scene_requested, this, &VerticalController::rename_scene);
	connect(dock, &VerticalDock::remove_scene_requested, this, &VerticalController::remove_scene);
	connect(dock, &VerticalDock::add_source_requested, this,
		[this](const QString &name) { canvas_.add_source(name.toStdString()); });
	connect(dock, &VerticalDock::remove_item_requested, this, [this](std::int64_t id) { canvas_.remove_item(id); });
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
