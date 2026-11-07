#include "dock/vertical_dock.hpp"

#include "dock/icons.hpp"
#include "dock/vertical_preview.hpp"

#include <obs-module.h>

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QResizeEvent>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace multistream {
namespace {

constexpr int kItemIdRole = Qt::UserRole;
constexpr int kSceneNameRole = Qt::UserRole + 1;
constexpr int kStackBelowWidth = 380;
constexpr int kRowIconSize = 16;

constexpr const char *kVisibleIcon = "icons/ui/visible.svg";
constexpr const char *kHiddenIcon = "icons/ui/hidden.svg";
constexpr const char *kLockedIcon = "icons/ui/locked.svg";
constexpr const char *kUnlockedIcon = "icons/ui/unlocked.svg";
constexpr const char *kIconLabelName = "icon";
constexpr const char *kNameLabelName = "name";
constexpr const char *kVisibleButtonName = "visible";
constexpr const char *kLockButtonName = "lock";

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

class SceneList final : public QListWidget {
public:
	bool editing() const { return state() == QAbstractItemView::EditingState; }
};

void set_toggle_look(QToolButton *button, bool on, const char *on_icon, const char *off_icon, const char *on_key,
		     const char *off_key)
{
	const QSignalBlocker blocker(button);
	button->setChecked(on);
	button->setIcon(themed_icon(on ? on_icon : off_icon, button->palette()));
	const QString label = text(on ? on_key : off_key);
	button->setToolTip(label);
	button->setAccessibleName(label);
}

} // namespace

VerticalDock::VerticalDock(QWidget *parent)
	: QWidget(parent),
	  preview_(new VerticalPreview()),
	  controls_(new QWidget()),
	  message_(new QLabel()),
	  empty_hint_(new QLabel(text("Vertical.EmptyHint"))),
	  resolution_(new QLabel()),
	  audio_note_(new QLabel()),
	  lists_(new QSplitter(Qt::Horizontal)),
	  scenes_(new SceneList()),
	  items_(new QListWidget())
{
	preview_->setMinimumHeight(160);
	message_->setWordWrap(true);
	message_->setTextFormat(Qt::PlainText);
	message_->hide();
	empty_hint_->setWordWrap(true);
	empty_hint_->hide();
	audio_note_->setWordWrap(true);
	audio_note_->setTextFormat(Qt::RichText);
	audio_note_->setProperty("class", "text-small text-muted");
	audio_note_->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
	audio_note_->hide();

	scenes_->setObjectName("vertical-scenes");
	items_->setObjectName("vertical-sources");
	scenes_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
	items_->setEditTriggers(QAbstractItemView::NoEditTriggers);

	auto *add_scene = icon_button("btn-tool icon-plus", "icons/ui/add.svg", "Vertical.AddScene");
	auto *remove_scene = icon_button("btn-tool icon-minus", "icons/ui/remove.svg", "Vertical.RemoveScene");
	auto *scene_up = icon_button("btn-tool icon-up", "icons/ui/up.svg", "Vertical.MoveSceneUp");
	auto *scene_down = icon_button("btn-tool icon-down", "icons/ui/down.svg", "Vertical.MoveSceneDown");
	auto *add_source = icon_button("btn-tool icon-plus", "icons/ui/add.svg", "Vertical.AddSource");
	auto *remove_item = icon_button("btn-tool icon-minus", "icons/ui/remove.svg", "Vertical.RemoveSource");
	auto *item_up = icon_button("btn-tool icon-up", "icons/ui/up.svg", "Vertical.MoveUp");
	auto *item_down = icon_button("btn-tool icon-down", "icons/ui/down.svg", "Vertical.MoveDown");
	auto *settings = icon_button("btn-tool icon-gear", "icons/ui/settings.svg", "Vertical.ResolutionSettings");

	lists_->addWidget(list_panel("Vertical.Scenes", scenes_, {add_scene, remove_scene, scene_up, scene_down}));
	lists_->addWidget(list_panel("Vertical.Sources", items_, {add_source, remove_item, item_up, item_down}));
	lists_->setStretchFactor(0, 1);
	lists_->setStretchFactor(1, 2);
	lists_->setChildrenCollapsible(false);

	auto *status_row = new QHBoxLayout();
	status_row->setContentsMargins(0, 0, 0, 0);
	status_row->addStretch();
	status_row->addWidget(resolution_);
	status_row->addWidget(settings);

	auto *controls = new QVBoxLayout(controls_);
	controls->setContentsMargins(0, 0, 0, 0);
	controls->addLayout(status_row);
	controls->addWidget(empty_hint_);
	controls->addWidget(message_);
	controls->addWidget(lists_, 1);
	controls->addWidget(audio_note_);

	auto *outer = new QSplitter(Qt::Vertical);
	outer->addWidget(preview_);
	outer->addWidget(controls_);
	outer->setStretchFactor(0, 3);
	outer->setStretchFactor(1, 2);
	outer->setChildrenCollapsible(false);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(outer);

	connect(scenes_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item, QListWidgetItem *) {
		if (!updating_ && item != nullptr) {
			emit scene_selected(item->data(kSceneNameRole).toString());
		}
	});
	connect(scenes_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		if (!updating_) {
			emit rename_scene_requested(item->data(kSceneNameRole).toString(), item->text().trimmed());
		}
	});
	connect(add_scene, &QToolButton::clicked, this, [this] { emit add_scene_requested(); });
	connect(remove_scene, &QToolButton::clicked, this, [this] {
		if (!selected_scene().isEmpty()) {
			emit remove_scene_requested(selected_scene());
		}
	});
	connect(scene_up, &QToolButton::clicked, this, [this] {
		if (!selected_scene().isEmpty()) {
			emit move_scene_requested(selected_scene(), true);
		}
	});
	connect(scene_down, &QToolButton::clicked, this, [this] {
		if (!selected_scene().isEmpty()) {
			emit move_scene_requested(selected_scene(), false);
		}
	});
	connect(add_source, &QToolButton::clicked, this, [this, add_source] {
		emit add_source_menu_requested(add_source->mapToGlobal(QPoint(0, add_source->height())));
	});

	const auto on_selected = [this](auto signal) {
		return [this, signal] {
			if (const auto id = selected_item()) {
				emit(this->*signal)(*id);
			}
		};
	};
	connect(remove_item, &QToolButton::clicked, this, on_selected(&VerticalDock::remove_item_requested));
	connect(item_up, &QToolButton::clicked, this, on_selected(&VerticalDock::move_item_up_requested));
	connect(item_down, &QToolButton::clicked, this, on_selected(&VerticalDock::move_item_down_requested));

	connect(items_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item, QListWidgetItem *) {
		if (!updating_) {
			emit item_selected(item != nullptr ? item->data(kItemIdRole).toLongLong() : 0);
		}
	});
	connect(items_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
		emit item_properties_requested(item->data(kItemIdRole).toLongLong());
	});
	connect(audio_note_, &QLabel::linkActivated, this, [this] { emit remove_hidden_audio_requested(); });
	connect(settings, &QToolButton::clicked, this, [this] { emit resolution_settings_requested(); });

	add_delete_shortcut(scenes_, remove_scene);
	add_delete_shortcut(items_, remove_item);
	apply_icons();
}

void VerticalDock::add_delete_shortcut(QListWidget *list, QToolButton *remove)
{
	auto *action = new QAction(remove->toolTip(), list);
#if defined(Q_OS_MACOS)
	action->setShortcuts({QKeySequence(QKeySequence::Delete), QKeySequence(Qt::Key_Backspace)});
#else
	action->setShortcut(QKeySequence::Delete);
#endif
	// Only while the list has the focus: never while a scene name is being typed.
	action->setShortcutContext(Qt::WidgetShortcut);
	list->addAction(action);
	connect(action, &QAction::triggered, remove, &QToolButton::click);
}

QToolButton *VerticalDock::icon_button(const char *css_class, const char *fallback_icon_file, const char *tooltip_key)
{
	auto *button = new QToolButton();
	button->setAutoRaise(true);
	button->setProperty("class", css_class);
	button->setIconSize(QSize(kRowIconSize, kRowIconSize));
	button->setToolTip(text(tooltip_key));
	button->setAccessibleName(text(tooltip_key));
	icon_buttons_.push_back({button, fallback_icon_file});
	return button;
}

QWidget *VerticalDock::list_panel(const char *title_key, QListWidget *list, const std::vector<QToolButton *> &tools)
{
	auto *panel = new QWidget();
	auto *title = new QLabel(text(title_key));
	title->setProperty("class", "subtitle");
	auto *toolbar = new QHBoxLayout();
	toolbar->setContentsMargins(0, 0, 0, 0);
	toolbar->setSpacing(0);
	for (QToolButton *tool : tools) {
		toolbar->addWidget(tool);
	}
	toolbar->addStretch();

	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	layout->addWidget(title);
	layout->addWidget(list, 1);
	layout->addLayout(toolbar);
	return panel;
}

QWidget *VerticalDock::item_row(const VerticalItemView &view)
{
	auto *row = new QWidget();
	auto *icon = new QLabel(row);
	icon->setObjectName(kIconLabelName);
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	auto *name = new QLabel(row);
	name->setObjectName(kNameLabelName);
	name->setTextFormat(Qt::PlainText);
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

	auto *visible = new QToolButton(row);
	visible->setObjectName(kVisibleButtonName);
	visible->setCheckable(true);
	visible->setAutoRaise(true);
	visible->setIconSize(QSize(kRowIconSize, kRowIconSize));
	auto *lock = new QToolButton(row);
	lock->setObjectName(kLockButtonName);
	lock->setCheckable(true);
	lock->setAutoRaise(true);
	lock->setIconSize(QSize(kRowIconSize, kRowIconSize));

	const std::int64_t id = view.id;
	connect(visible, &QToolButton::toggled, this, [this, id](bool on) { emit item_visibility_requested(id, on); });
	connect(lock, &QToolButton::toggled, this, [this, id](bool on) { emit item_lock_requested(id, on); });

	auto *layout = new QHBoxLayout(row);
	layout->setContentsMargins(4, 0, 2, 0);
	layout->setSpacing(4);
	layout->addWidget(icon);
	layout->addWidget(name, 1);
	layout->addWidget(visible);
	layout->addWidget(lock);

	update_row(row, view);
	return row;
}

void VerticalDock::update_row(QWidget *row, const VerticalItemView &view)
{
	row->findChild<QLabel *>(kIconLabelName)
		->setPixmap(themed_pixmap(view.icon_file.c_str(), palette(), kRowIconSize, devicePixelRatioF()));
	auto *name = row->findChild<QLabel *>(kNameLabelName);
	name->setText(view.name);
	name->setToolTip(view.name);
	name->setEnabled(view.visible);
	set_toggle_look(row->findChild<QToolButton *>(kVisibleButtonName), view.visible, kVisibleIcon, kHiddenIcon,
			"Vertical.HideSource", "Vertical.ShowSource");
	set_toggle_look(row->findChild<QToolButton *>(kLockButtonName), view.locked, kLockedIcon, kUnlockedIcon,
			"Vertical.UnlockSource", "Vertical.LockSource");
}

void VerticalDock::apply_icons()
{
	for (const IconButton &entry : icon_buttons_) {
		apply_class_icon(entry.button, themed_icon(entry.fallback_icon_file, palette()));
	}
	for (int row = 0; row < items_->count() && row < static_cast<int>(shown_items_.size()); ++row) {
		update_row(items_->itemWidget(items_->item(row)), shown_items_[static_cast<std::size_t>(row)]);
	}
}

void VerticalDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange) {
		apply_icons();
	}
}

void VerticalDock::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	const Qt::Orientation wanted = event->size().width() < kStackBelowWidth ? Qt::Vertical : Qt::Horizontal;
	if (lists_->orientation() != wanted) {
		lists_->setOrientation(wanted);
	}
}

void VerticalDock::set_available(bool available)
{
	controls_->setEnabled(available);
	if (available) {
		if (message_->text() == text("Vertical.Unavailable")) {
			message_->hide();
		}
	} else {
		show_message(text("Vertical.Unavailable"));
	}
}

void VerticalDock::set_scenes(const std::vector<QString> &scenes, const QString &active)
{
	// A refresh must not throw away a name being typed.
	if (static_cast<SceneList *>(scenes_)->editing()) {
		return;
	}

	updating_ = true;
	scenes_->clear();
	for (const QString &scene : scenes) {
		auto *item = new QListWidgetItem(scene, scenes_);
		item->setFlags(item->flags() | Qt::ItemIsEditable);
		item->setData(kSceneNameRole, scene);
		if (scene == active) {
			scenes_->setCurrentItem(item);
		}
	}
	updating_ = false;
}

void VerticalDock::set_items(const std::vector<VerticalItemView> &items)
{
	const auto selected = selected_item();
	const bool same_rows = std::equal(items.begin(), items.end(), shown_items_.begin(), shown_items_.end(),
					  [](const VerticalItemView &left, const VerticalItemView &right) {
						  return left.id == right.id;
					  });

	updating_ = true;
	shown_items_ = items;
	if (same_rows) {
		// In place: a toggle can land here from a click on one of the row's own buttons.
		for (int row = 0; row < items_->count(); ++row) {
			update_row(items_->itemWidget(items_->item(row)), items[static_cast<std::size_t>(row)]);
		}
	} else {
		items_->clear();
		for (const VerticalItemView &view : items) {
			auto *item = new QListWidgetItem(items_);
			item->setData(kItemIdRole, static_cast<qlonglong>(view.id));
			QWidget *row = item_row(view);
			item->setSizeHint(row->sizeHint());
			items_->setItemWidget(item, row);
			if (selected == view.id) {
				items_->setCurrentItem(item);
			}
		}
	}
	updating_ = false;
	empty_hint_->setVisible(items.empty());
}

void VerticalDock::select_item(std::optional<std::int64_t> id)
{
	updating_ = true;
	items_->setCurrentItem(nullptr);
	for (int row = 0; id && row < items_->count(); ++row) {
		if (items_->item(row)->data(kItemIdRole).toLongLong() == *id) {
			items_->setCurrentRow(row);
			break;
		}
	}
	updating_ = false;
}

void VerticalDock::set_resolution(int width, int height)
{
	resolution_->setText(text("Vertical.ResolutionValue").arg(width).arg(height));
}

void VerticalDock::set_hidden_audio_items(int count)
{
	if (count > 0) {
		// The text holds its own link markup, so it is set as is.
		audio_note_->setText(text("Vertical.AudioNoteHtml"));
	}
	audio_note_->setVisible(count > 0);
}

void VerticalDock::show_message(const QString &message)
{
	message_->setText(message);
	message_->setVisible(!message.isEmpty());
}

std::optional<std::int64_t> VerticalDock::selected_item() const
{
	const QListWidgetItem *item = items_->currentItem();
	if (item == nullptr) {
		return std::nullopt;
	}
	return item->data(kItemIdRole).toLongLong();
}

QString VerticalDock::selected_scene() const
{
	const QListWidgetItem *item = scenes_->currentItem();
	return item != nullptr ? item->data(kSceneNameRole).toString() : QString();
}

} // namespace multistream
