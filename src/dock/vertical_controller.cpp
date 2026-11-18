#include "dock/vertical_controller.hpp"

#include "dock/icons.hpp"
#include "destinations/vertical_resolution.hpp"
#include "dock/vertical_preview.hpp"
#include "vertical_canvas/source_icons.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/config-file.h>

#include <QComboBox>
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
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

namespace multistream {
namespace {

constexpr int kResolutionInputLimit = 16384;

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
	// The canvas also reports changes made through OBS's own docks, from libobs signals.
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
	connect(dock, &VerticalDock::import_menu_requested, this, &VerticalController::show_import_menu);
	connect(dock, &VerticalDock::monitor_requested, this, &VerticalController::show_monitor);
	connect(dock, &VerticalDock::twitch_help_requested, this, &VerticalController::show_twitch_help);
	// OBS has no event for its stream settings changing, so the Twitch line is polled.
	if (twitch_poll_ == nullptr) {
		twitch_poll_ = new QTimer(this);
		twitch_poll_->setInterval(2000);
		connect(twitch_poll_, &QTimer::timeout, this, &VerticalController::refresh_twitch_dual_format);
		twitch_poll_->start();
	}
	refresh_twitch_dual_format();
	connect(dock, &VerticalDock::link_layout_changed, this, [this](int layout, double focus) {
		if (layout >= 0 && layout < static_cast<int>(std::size(kAllTwinLayouts))) {
			canvas_.set_active_link_layout(kAllTwinLayouts[layout], focus);
			canvas_.save_collection();
		}
	});
	connect(dock, &VerticalDock::relayout_requested, this, [this](bool include_manual) {
		canvas_.relayout_active(include_manual);
		canvas_.save_collection();
	});
	connect(dock, &VerticalDock::unlink_requested, this, [this] {
		if (confirm(dock_, text("Vertical.Unlink"), text("Vertical.UnlinkQuestion"))) {
			canvas_.unlink_active();
			canvas_.save_collection();
		}
	});
	connect(dock, &VerticalDock::safe_zones_toggled, this, [this](bool shown) {
		if (dock_) {
			dock_->preview()->set_safe_zones(shown);
		}
	});

	refresh();
}

VerticalController::~VerticalController()
{
	canvas_.set_changed_listener(nullptr);
	// The monitor window has no parent; it goes before OBS shuts graphics down.
	delete monitor_.data();
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
	int hidden_audio = 0;
	for (const VerticalItem &item : canvas_.items()) {
		if (!item.has_video) {
			++hidden_audio;
			continue;
		}
		items.push_back({item.id, QString::fromStdString(item.name), data_icon_for(item.type_id), item.visible,
				 item.locked, item.manual});
	}
	dock_->set_items(items);
	dock_->select_item(dock_->preview()->selected_item());
	dock_->set_hidden_audio_items(hidden_audio);
	dock_->set_resolution(canvas_.settings().width, canvas_.settings().height);

	std::optional<VerticalLinkView> link;
	if (const std::optional<ActiveLink> active = canvas_.active_link()) {
		const auto *found = std::find(std::begin(kAllTwinLayouts), std::end(kAllTwinLayouts), active->layout);
		link = VerticalLinkView{QString::fromStdString(active->main_name),
					static_cast<int>(found - std::begin(kAllTwinLayouts)), active->focus,
					active->automatic};
	}
	dock_->set_link(link);
}

void VerticalController::show_import_menu(const QPoint &global_position)
{
	if (!dock_) {
		return;
	}
	QMenu menu(dock_);
	const std::vector<MainSceneChoice> choices = canvas_.main_scene_choices();
	const std::optional<std::string> active = canvas_.active_scene();

	// Existing vertical scene: follow a main scene as it is, nothing imported.
	if (active) {
		QMenu *follow = menu.addMenu(text("Vertical.LinkTo"));
		follow->setToolTipsVisible(true);
		follow->setToolTip(text("Vertical.LinkToHint"));
		QAction *section = follow->addAction(text("Vertical.LinkSection"));
		section->setEnabled(false);
		for (const MainSceneChoice &choice : choices) {
			const QString name = QString::fromStdString(choice.name);
			const bool mine = choice.vertical_scene && *choice.vertical_scene == *active;
			const QString label = choice.vertical_scene && !mine
						      ? text("Vertical.LinkTaken")
								.arg(name, QString::fromStdString(*choice.vertical_scene))
						      : name;
			QAction *action = follow->addAction(label);
			action->setCheckable(true);
			action->setChecked(mine);
			const std::string uuid = choice.uuid;
			connect(action, &QAction::triggered, this, [this, uuid] {
				canvas_.link_active_to(uuid);
				canvas_.save_collection();
			});
		}
		menu.addSeparator();
	}

	QAction *title = menu.addAction(text("Vertical.ImportSection"));
	title->setEnabled(false);
	for (const MainSceneChoice &choice : choices) {
		const QString name = QString::fromStdString(choice.name);
		if (choice.vertical_scene) {
			// Already linked: go to it.
			const std::string vertical = *choice.vertical_scene;
			menu.addAction(text("Vertical.ImportLinked").arg(name), this, [this, vertical] {
				canvas_.switch_scene(vertical);
				canvas_.save_collection();
			});
			continue;
		}
		const std::string uuid = choice.uuid;
		menu.addAction(name, this, [this, uuid] {
			if (!canvas_.import_main_scene(uuid, TwinLayout::CameraTop)) {
				if (dock_) {
					dock_->show_message(text("Vertical.ImportFailed"));
				}
				return;
			}
			if (dock_) {
				dock_->show_message({});
			}
			canvas_.save_collection();
		});
	}
	menu.exec(global_position);
}

void VerticalController::show_item_menu(std::int64_t id, const QPoint &global_position)
{
	QMenu menu(dock_);
	const std::vector<VerticalItem> items = canvas_.items();
	const auto found = std::find_if(items.begin(), items.end(), [id](const VerticalItem &item) { return item.id == id; });
	if (found != items.end() && found->linked) {
		QAction *back = menu.addAction(text("Vertical.BackToAuto"), this, [this, id] { canvas_.reset_item_to_auto(id); });
		back->setEnabled(found->manual);
		QAction *follow = menu.addAction(text("Vertical.FollowVisibility"));
		follow->setCheckable(true);
		follow->setChecked(canvas_.item_visibility_linked(id));
		connect(follow, &QAction::toggled, this, [this, id](bool on) { canvas_.set_item_visibility_linked(id, on); });
		menu.addSeparator();
	}
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

	const std::vector<ResolutionPreset> &presets = vertical_resolution_presets();
	auto *preset = new QComboBox(&dialog);
	for (const ResolutionPreset &size : presets) {
		preset->addItem(text("Vertical.ResolutionPresetItem")
					.arg(size.width)
					.arg(size.height)
					.arg(QString::fromStdString(aspect_ratio_text(size.width, size.height))));
	}
	preset->addItem(text("Vertical.ResolutionCustom"));
	const int custom_index = static_cast<int>(presets.size());

	auto *width = new QSpinBox(&dialog);
	auto *height = new QSpinBox(&dialog);
	for (QSpinBox *side : {width, height}) {
		// Wider than the valid range, so a wrong size is explained instead of clamped.
		side->setRange(1, kResolutionInputLimit);
		side->setSingleStep(2);
	}
	width->setValue(canvas_.settings().width);
	height->setValue(canvas_.settings().height);

	auto *aspect = new QLabel(&dialog);
	aspect->setProperty("class", "text-small text-muted");
	auto *problem = new QLabel(&dialog);
	problem->setProperty("class", "text-small text-danger");
	problem->setWordWrap(true);
	problem->hide();

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

	const auto update = [=] {
		const int w = width->value();
		const int h = height->value();
		{
			const QSignalBlocker blocker(preset);
			const auto found = preset_index_for(w, h);
			preset->setCurrentIndex(found ? static_cast<int>(*found) : custom_index);
		}
		const std::string ratio = aspect_ratio_text(w, h);
		aspect->setText(text("Vertical.ResolutionAspect").arg(QString::fromStdString(ratio)));

		const ResolutionProblem found_problem = check_vertical_resolution(w, h);
		switch (found_problem) {
		case ResolutionProblem::NotEven:
			problem->setText(text("Vertical.ResolutionInvalid"));
			break;
		case ResolutionProblem::OutOfRange:
			problem->setText(text("Vertical.ResolutionOutOfRange"));
			break;
		case ResolutionProblem::None:
			problem->clear();
			break;
		}
		problem->setVisible(found_problem != ResolutionProblem::None);
		const QString description = problem->text();
		width->setAccessibleDescription(description);
		height->setAccessibleDescription(description);
		buttons->button(QDialogButtonBox::Ok)->setEnabled(found_problem == ResolutionProblem::None);
	};
	connect(preset, &QComboBox::currentIndexChanged, &dialog, [=, &presets](int index) {
		if (index < 0 || index >= custom_index) {
			return;
		}
		const ResolutionPreset &size = presets[static_cast<std::size_t>(index)];
		const QSignalBlocker width_blocker(width);
		const QSignalBlocker height_blocker(height);
		width->setValue(size.width);
		height->setValue(size.height);
		update();
	});
	connect(width, &QSpinBox::valueChanged, &dialog, update);
	connect(height, &QSpinBox::valueChanged, &dialog, update);
	update();

	auto *form = new QFormLayout(&dialog);
	form->addRow(text("Vertical.ResolutionPreset"), preset);
	form->addRow(text("Vertical.ResolutionWidth"), width);
	form->addRow(text("Vertical.ResolutionHeight"), height);
	form->addRow(QString(), aspect);
	form->addRow(QString(), problem);
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

void VerticalController::refresh_twitch_dual_format()
{
	if (!dock_) {
		return;
	}
	TwitchDualFormatFacts facts;
	if (obs_service_t *service = obs_frontend_get_streaming_service()) {
		OBSDataAutoRelease settings = obs_service_get_settings(service);
		const char *name = obs_data_get_string(settings, "service");
		facts.service_is_twitch = name != nullptr && std::string_view(name) == "Twitch";
	}
	if (config_t *profile = obs_frontend_get_profile_config()) {
		facts.enhanced_broadcasting = config_get_bool(profile, "Stream1", "EnableMultitrackVideo");
		const char *extra = config_get_string(profile, "Stream1", "MultitrackExtraCanvas");
		facts.additional_canvas_uuid = extra != nullptr ? extra : "";
	}
	facts.vertical_canvas_uuid = canvas_.canvas_uuid();
	facts.streaming = obs_frontend_streaming_active();
	dock_->set_twitch_dual_format(twitch_dual_format(facts));
}

void VerticalController::show_twitch_help()
{
	if (!dock_) {
		return;
	}
	QMessageBox box(dock_);
	box.setWindowTitle(text("Vertical.TwitchHelpTitle"));
	box.setTextFormat(Qt::RichText);
	box.setText(text("Vertical.TwitchHelpBody"));
	box.setStandardButtons(QMessageBox::Ok);
	box.exec();
}

// A window of its own showing the canvas as it goes out, for keeping an eye on it while live.
void VerticalController::show_monitor()
{
	if (!dock_) {
		return;
	}
	if (!monitor_) {
		auto *window = new QWidget(nullptr, Qt::Window);
		window->setAttribute(Qt::WA_DeleteOnClose);
		window->setWindowTitle(text("Vertical.MonitorTitle"));
		auto *layout = new QVBoxLayout(window);
		layout->setContentsMargins(0, 0, 0, 0);
		auto *view = new VerticalPreview(window);
		view->set_monitor(true);
		view->set_target(canvas_.preview_target());
		layout->addWidget(view);
		const CanvasSize size = canvas_.canvas_size();
		const int height = 760;
		const int width = size.height > 0 ? static_cast<int>(height * size.width / size.height) : 428;
		window->resize(width, height);
		monitor_ = window;
	}
	monitor_->show();
	monitor_->raise();
	monitor_->activateWindow();
}

} // namespace multistream
