#include "dock/vertical_controller.hpp"

#include "dock/icons.hpp"
#include "dock/vertical_preview.hpp"
#include "vertical_canvas/source_icons.hpp"

#include <obs-module.h>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <vector>

namespace multistream {
namespace {

// Sources can vanish through OBS's own docks, without any call into this plugin.
constexpr int kChangeCheckMilliseconds = 500;

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

std::optional<std::int64_t> as_selection(std::int64_t id)
{
	return id != 0 ? std::make_optional(id) : std::nullopt;
}

bool confirm(QWidget *parent, const QString &title, const QString &question)
{
	QMessageBox box(QMessageBox::Question, title, question, QMessageBox::Yes | QMessageBox::No, parent);
	box.setDefaultButton(QMessageBox::No);
#if defined(MULTI_STREAM_E2E_SECRETS)
	// A native alert has no Qt widget for the test hook to answer.
	box.setOption(QMessageBox::Option::DontUseNativeDialog);
#endif
	return box.exec() == QMessageBox::Yes;
}

std::string data_icon_for(const std::string &type_id)
{
	return source_icon_file(source_icon_for_type(type_id));
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

	connect(dock, &VerticalDock::scene_selected, this, [this](const QString &name) {
		canvas_.switch_scene(name.toStdString());
		canvas_.save_collection();
	});
	connect(dock, &VerticalDock::add_scene_requested, this, &VerticalController::add_scene);
	connect(dock, &VerticalDock::rename_scene_requested, this, &VerticalController::rename_scene);
	connect(dock, &VerticalDock::remove_scene_requested, this, &VerticalController::remove_scene);
	connect(dock, &VerticalDock::move_scene_requested, this,
		[this](const QString &name, bool up) { canvas_.move_scene(name.toStdString(), up); });
	connect(dock, &VerticalDock::add_source_menu_requested, this, &VerticalController::show_add_source_menu);
	connect(dock, &VerticalDock::remove_item_requested, this, &VerticalController::remove_item);
	connect(dock, &VerticalDock::remove_hidden_audio_requested, this, &VerticalController::remove_hidden_audio);
	connect(dock, &VerticalDock::item_selected, this, [this](std::int64_t id) {
		if (dock_) {
			dock_->preview()->set_selected_item(as_selection(id));
		}
	});
	connect(dock->preview(), &VerticalPreview::selection_changed, this, [this](std::int64_t id) {
		if (dock_) {
			dock_->select_item(as_selection(id));
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
	connect(dock, &VerticalDock::item_lock_requested, this,
		[this](std::int64_t id, bool locked) { canvas_.set_item_locked(id, locked); });
	connect(dock, &VerticalDock::item_properties_requested, this,
		[this](std::int64_t id) { canvas_.open_item_properties(id); });
	connect(dock, &VerticalDock::resolution_settings_requested, this, &VerticalController::edit_resolution);

	connect(&change_timer_, &QTimer::timeout, this, &VerticalController::refresh_if_changed);
	change_timer_.start(kChangeCheckMilliseconds);

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

std::string VerticalController::snapshot() const
{
	std::string result = canvas_.active_scene().value_or("");
	for (const std::string &scene : canvas_.scenes()) {
		result += '\n' + scene;
	}
	for (const VerticalItem &item : canvas_.items()) {
		result += '\n' + std::to_string(item.id) + (item.visible ? "v" : "h") + (item.locked ? "l" : "u") + item.name;
	}
	return result;
}

void VerticalController::refresh_if_changed()
{
	if (dock_ && dock_->isVisible() && snapshot() != shown_snapshot_) {
		refresh();
	}
}

void VerticalController::refresh()
{
	if (!dock_) {
		return;
	}
	shown_snapshot_ = snapshot();

	dock_->set_available(canvas_.available());

	std::vector<QString> scenes;
	for (const std::string &scene : canvas_.scenes()) {
		scenes.push_back(QString::fromStdString(scene));
	}
	dock_->set_scenes(scenes, QString::fromStdString(canvas_.active_scene().value_or(std::string())));

	std::vector<VerticalItemView> items;
	int hidden_audio = 0;
	for (const VerticalItem &item : canvas_.items()) {
		if (!item.has_video) {
			++hidden_audio;
			continue;
		}
		items.push_back({item.id, QString::fromStdString(item.name), data_icon_for(item.type_id), item.visible,
				 item.locked});
	}
	dock_->set_items(items);
	dock_->select_item(dock_->preview()->selected_item());
	dock_->set_hidden_audio_items(hidden_audio);
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

void VerticalController::show_add_source_menu(const QPoint &global_position)
{
	if (!dock_) {
		return;
	}

	QMenu menu(dock_);
	for (const InputType &type : canvas_.creatable_input_types()) {
		QAction *action =
			menu.addAction(themed_icon(data_icon_for(type.unversioned_id).c_str(), dock_->palette()),
				       QString::fromStdString(type.display_name));
		connect(action, &QAction::triggered, this, [this, type] { add_new_source(type); });
	}
	menu.addSeparator();
	menu.addAction(text("Vertical.AddExisting"), this, &VerticalController::add_existing_source);
	menu.addSeparator();
	QAction *note = menu.addAction(text("Vertical.AudioMenuNote"));
	note->setEnabled(false);
	menu.exec(global_position);
}

void VerticalController::select_new_item(std::int64_t id)
{
	if (dock_) {
		dock_->preview()->set_selected_item(id);
		dock_->select_item(id);
	}
	canvas_.save_collection();
}

void VerticalController::add_new_source(const InputType &type)
{
	if (!dock_) {
		return;
	}

	std::vector<std::string> taken = canvas_.main_sources();
	const std::string suggestion = unique_scene_name(type.display_name, taken);
	bool accepted = false;
	const QString name = QInputDialog::getText(dock_, text("Vertical.NewSourceTitle"), text("Vertical.SourceName"),
						   QLineEdit::Normal, QString::fromStdString(suggestion), &accepted)
				     .trimmed();
	if (!accepted || name.isEmpty() || !dock_) {
		return;
	}
	if (canvas_.source_name_taken(name.toStdString())) {
		dock_->show_message(text("Vertical.SourceNameTaken"));
		return;
	}

	const std::optional<std::int64_t> added = canvas_.create_source(type.id, name.toStdString());
	if (!added) {
		dock_->show_message(text("Vertical.SourceCreateFailed"));
		return;
	}
	select_new_item(*added);
	canvas_.open_item_properties(*added);
}

void VerticalController::add_existing_source()
{
	if (!dock_) {
		return;
	}

	QDialog dialog(dock_);
	dialog.setWindowTitle(text("Vertical.AddExistingTitle"));
	auto *search = new QLineEdit(&dialog);
	search->setPlaceholderText(text("Vertical.SearchSources"));
	search->setClearButtonEnabled(true);
	auto *list = new QListWidget(&dialog);
	auto *empty = new QLabel(text("Vertical.NoExistingSources"), &dialog);
	empty->setWordWrap(true);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);

	const std::vector<std::string> names = canvas_.main_sources();
	const auto fill = [&, list, empty, buttons](const QString &query) {
		list->clear();
		for (const std::string &name : filter_names(names, query.toStdString())) {
			list->addItem(QString::fromStdString(name));
		}
		if (list->count() > 0) {
			list->setCurrentRow(0);
		}
		empty->setVisible(list->count() == 0);
		buttons->button(QDialogButtonBox::Ok)->setEnabled(list->count() > 0);
	};
	fill(QString());
	connect(search, &QLineEdit::textChanged, &dialog, fill);
	connect(list, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	auto *layout = new QVBoxLayout(&dialog);
	layout->addWidget(search);
	layout->addWidget(list, 1);
	layout->addWidget(empty);
	layout->addWidget(buttons);

	if (dialog.exec() == QDialog::Accepted && list->currentItem() != nullptr) {
		if (const auto added = canvas_.add_source(list->currentItem()->text().toStdString())) {
			select_new_item(*added);
		}
	}
}

void VerticalController::remove_item(std::int64_t id)
{
	const std::vector<VerticalItem> items = canvas_.items();
	const auto found = std::find_if(items.begin(), items.end(), [id](const VerticalItem &item) { return item.id == id; });
	if (found == items.end()) {
		return;
	}
	const QString question = text("Vertical.RemoveSourceQuestion").arg(QString::fromStdString(found->name));
	if (confirm(dock_, text("Vertical.RemoveSourceTitle"), question)) {
		canvas_.remove_item(id);
		canvas_.save_collection();
	}
}

void VerticalController::remove_hidden_audio()
{
	for (const VerticalItem &item : canvas_.items()) {
		if (!item.has_video) {
			canvas_.remove_item(item.id);
		}
	}
	canvas_.save_collection();
}

void VerticalController::add_scene()
{
	bool accepted = false;
	const QString name = QInputDialog::getText(dock_, text("Vertical.AddScene"), text("Vertical.SceneName"),
						   QLineEdit::Normal, text("Vertical.DefaultScene"), &accepted);
	if (accepted) {
		if (const auto added = canvas_.add_scene(name.toStdString())) {
			canvas_.switch_scene(*added);
			canvas_.save_collection();
		}
	}
}

void VerticalController::rename_scene(const QString &name, const QString &new_name)
{
	if (new_name != name && !canvas_.rename_scene(name.toStdString(), new_name.toStdString()) && dock_) {
		dock_->show_message(text("Vertical.RenameRefused"));
	}
	// Later, because the list is still editing: this also puts back a refused name.
	QTimer::singleShot(0, this, &VerticalController::refresh);
	canvas_.save_collection();
}

void VerticalController::remove_scene(const QString &name)
{
	if (!can_remove_scene(canvas_.scenes().size())) {
		if (dock_) {
			dock_->show_message(text("Vertical.LastScene"));
		}
		return;
	}
	const QString question = text("Vertical.RemoveSceneQuestion").arg(name);
	if (!confirm(dock_, text("Vertical.RemoveSceneTitle"), question)) {
		return;
	}
	if (!canvas_.remove_scene(name.toStdString()) && dock_) {
		dock_->show_message(text("Vertical.LastScene"));
	}
	canvas_.save_collection();
}

void VerticalController::edit_resolution()
{
	if (!dock_) {
		return;
	}

	QDialog dialog(dock_);
	dialog.setWindowTitle(text("Vertical.Resolution"));
	auto *width = new QSpinBox(&dialog);
	auto *height = new QSpinBox(&dialog);
	for (QSpinBox *side : {width, height}) {
		side->setRange(2, 8192);
		side->setSingleStep(2);
	}
	width->setValue(canvas_.settings().width);
	height->setValue(canvas_.settings().height);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	auto *form = new QFormLayout(&dialog);
	form->addRow(text("Vertical.ResolutionWidth"), width);
	form->addRow(text("Vertical.ResolutionHeight"), height);
	form->addRow(buttons);

	if (dialog.exec() == QDialog::Accepted) {
		change_resolution(width->value(), height->value());
	}
}

void VerticalController::change_resolution(int width, int height)
{
	switch (decide_resolution_change(canvas_.settings(), width, height, engine_.video_in_use(VideoSource::Vertical))) {
	case ResolutionChange::Apply:
		if (canvas_.apply_resolution(width, height)) {
			if (dock_) {
				dock_->show_message({});
			}
		} else if (dock_) {
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
			dock_->show_message(text("Vertical.ResolutionRefused"));
		}
		break;
	}
}

} // namespace multistream
