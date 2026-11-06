#include "dock/vertical_dock.hpp"

#include "dock/vertical_preview.hpp"

#include <obs-module.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace multistream {
namespace {

constexpr int kItemIdRole = Qt::UserRole;

QString text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

QPushButton *button(const char *key)
{
	return new QPushButton(text(key));
}

} // namespace

VerticalDock::VerticalDock(QWidget *parent)
	: QWidget(parent),
	  preview_(new VerticalPreview()),
	  controls_(new QWidget()),
	  message_(new QLabel()),
	  scenes_(new QComboBox()),
	  items_(new QListWidget()),
	  main_sources_(new QComboBox()),
	  width_(new QSpinBox()),
	  height_(new QSpinBox())
{
	preview_->setMinimumHeight(240);
	message_->setWordWrap(true);
	message_->hide();

	for (QSpinBox *side : {width_, height_}) {
		side->setRange(2, 8192);
		side->setSingleStep(2);
	}

	auto *add_scene = button("Vertical.AddScene");
	auto *rename_scene = button("Vertical.RenameScene");
	auto *remove_scene = button("Vertical.RemoveScene");
	auto *add_source = button("Vertical.AddSource");
	auto *remove_item = button("Vertical.RemoveSource");
	auto *move_up = button("Vertical.MoveUp");
	auto *move_down = button("Vertical.MoveDown");
	auto *fit = button("Vertical.Fit");
	auto *fill = button("Vertical.Fill");
	auto *apply = button("Vertical.ApplyResolution");

	auto *scene_row = new QHBoxLayout();
	scene_row->addWidget(scenes_, 1);
	scene_row->addWidget(add_scene);
	scene_row->addWidget(rename_scene);
	scene_row->addWidget(remove_scene);

	auto *source_row = new QHBoxLayout();
	source_row->addWidget(main_sources_, 1);
	source_row->addWidget(add_source);

	auto *item_row = new QHBoxLayout();
	item_row->addWidget(remove_item);
	item_row->addWidget(move_up);
	item_row->addWidget(move_down);
	item_row->addWidget(fit);
	item_row->addWidget(fill);

	auto *resolution_row = new QHBoxLayout();
	resolution_row->addWidget(new QLabel(text("Vertical.Resolution")));
	resolution_row->addWidget(width_);
	resolution_row->addWidget(height_);
	resolution_row->addWidget(apply);

	auto *controls = new QVBoxLayout(controls_);
	controls->setContentsMargins(0, 0, 0, 0);
	controls->addLayout(scene_row);
	controls->addWidget(items_, 1);
	controls->addLayout(item_row);
	controls->addLayout(source_row);
	controls->addLayout(resolution_row);

	auto *layout = new QVBoxLayout(this);
	layout->addWidget(preview_, 2);
	layout->addWidget(message_);
	layout->addWidget(controls_, 1);

	connect(scenes_, &QComboBox::currentTextChanged, this, [this](const QString &name) {
		if (!updating_ && !name.isEmpty()) {
			emit scene_selected(name);
		}
	});
	connect(add_scene, &QPushButton::clicked, this, [this] { emit add_scene_requested(); });
	connect(rename_scene, &QPushButton::clicked, this, [this] { emit rename_scene_requested(scenes_->currentText()); });
	connect(remove_scene, &QPushButton::clicked, this, [this] { emit remove_scene_requested(scenes_->currentText()); });
	connect(add_source, &QPushButton::clicked, this, [this] {
		if (!main_sources_->currentText().isEmpty()) {
			emit add_source_requested(main_sources_->currentText());
		}
	});

	const auto on_selected = [this](auto signal) {
		return [this, signal] {
			if (const auto id = selected_item()) {
				emit(this->*signal)(*id);
			}
		};
	};
	connect(remove_item, &QPushButton::clicked, this, on_selected(&VerticalDock::remove_item_requested));
	connect(move_up, &QPushButton::clicked, this, on_selected(&VerticalDock::move_item_up_requested));
	connect(move_down, &QPushButton::clicked, this, on_selected(&VerticalDock::move_item_down_requested));
	connect(fit, &QPushButton::clicked, this, on_selected(&VerticalDock::fit_item_requested));
	connect(fill, &QPushButton::clicked, this, on_selected(&VerticalDock::fill_item_requested));

	connect(items_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		if (!updating_) {
			emit item_visibility_requested(item->data(kItemIdRole).toLongLong(),
						       item->checkState() == Qt::Checked);
		}
	});
	connect(apply, &QPushButton::clicked, this, [this] { emit resolution_requested(width_->value(), height_->value()); });
}

void VerticalDock::set_available(bool available)
{
	controls_->setEnabled(available);
	if (available) {
		message_->hide();
	} else {
		show_message(text("Vertical.Unavailable"));
	}
}

void VerticalDock::set_scenes(const std::vector<QString> &scenes, const QString &active)
{
	updating_ = true;
	scenes_->clear();
	for (const QString &scene : scenes) {
		scenes_->addItem(scene);
	}
	scenes_->setCurrentText(active);
	updating_ = false;
}

void VerticalDock::set_items(const std::vector<VerticalItemView> &items)
{
	const auto selected = selected_item();

	updating_ = true;
	items_->clear();
	for (const VerticalItemView &view : items) {
		auto *item = new QListWidgetItem(view.name, items_);
		item->setData(kItemIdRole, static_cast<qlonglong>(view.id));
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(view.visible ? Qt::Checked : Qt::Unchecked);
		if (selected == view.id) {
			item->setSelected(true);
			items_->setCurrentItem(item);
		}
	}
	updating_ = false;
}

void VerticalDock::set_main_sources(const std::vector<QString> &sources)
{
	const QString current = main_sources_->currentText();
	main_sources_->clear();
	for (const QString &source : sources) {
		main_sources_->addItem(source);
	}
	main_sources_->setCurrentText(current);
}

void VerticalDock::set_resolution(int width, int height)
{
	width_->setValue(width);
	height_->setValue(height);
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

} // namespace multistream
