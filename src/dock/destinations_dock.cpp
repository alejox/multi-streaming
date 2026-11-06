#include "dock/destinations_dock.hpp"

#include "dock/icons.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace multistream {
namespace {

constexpr int kPlatformIconSize = 20;
constexpr int kDotSize = 10;
constexpr int kButtonIconSize = 16;

QColor tone_color(RowTone tone, const QPalette &palette)
{
	switch (tone) {
	case RowTone::Idle:
		return palette.color(QPalette::Disabled, QPalette::WindowText);
	case RowTone::Busy:
		return QColor(0xE0, 0xB0, 0x20);
	case RowTone::Live:
		return QColor(0x30, 0xB0, 0x50);
	case RowTone::Warning:
		return QColor(0xE8, 0x80, 0x20);
	case RowTone::Failed:
		return QColor(0xE0, 0x40, 0x40);
	}
	return palette.color(QPalette::WindowText);
}

QPixmap status_dot(RowTone tone, const QPalette &palette, qreal ratio)
{
	const int pixels = qRound(kDotSize * ratio);
	QPixmap pixmap(pixels, pixels);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(tone_color(tone, palette));
	painter.drawEllipse(pixmap.rect());
	painter.end();
	pixmap.setDevicePixelRatio(ratio);
	return pixmap;
}

QLabel *wrapping_label(QWidget *parent)
{
	auto *label = new QLabel(parent);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	label->hide();
	return label;
}

} // namespace

DestinationsDock::DestinationsDock(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);

	problem_ = new QLabel(this);
	problem_->setWordWrap(true);
	problem_->setTextFormat(Qt::PlainText);
	problem_->setTextInteractionFlags(Qt::TextSelectableByMouse);
	problem_->hide();
	dismiss_problem_ = new QPushButton(obs_module_text("Dock.Dismiss"), this);
	dismiss_problem_->hide();
	connect(dismiss_problem_, &QPushButton::clicked, this, [this] {
		problem_->hide();
		dismiss_problem_->hide();
	});
	layout->addWidget(problem_);
	layout->addWidget(dismiss_problem_);

	empty_ = new QLabel(obs_module_text("Dock.Empty"), this);
	empty_->setWordWrap(true);
	layout->addWidget(empty_);

	auto *rows_container = new QWidget(this);
	rows_layout_ = new QVBoxLayout(rows_container);
	rows_layout_->setContentsMargins(0, 0, 0, 0);
	rows_layout_->addStretch();

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setWidget(rows_container);
	layout->addWidget(scroll, 1);

	auto *add = new QPushButton(obs_module_text("Dock.Add"), this);
	auto *start_all = new QPushButton(obs_module_text("Dock.StartAll"), this);
	auto *stop_all = new QPushButton(obs_module_text("Dock.StopAll"), this);
	connect(add, &QPushButton::clicked, this, &DestinationsDock::add_requested);
	connect(start_all, &QPushButton::clicked, this, &DestinationsDock::start_all_requested);
	connect(stop_all, &QPushButton::clicked, this, &DestinationsDock::stop_all_requested);

	color_icons_check_ = new QCheckBox(obs_module_text("Dock.ColorIcons"), this);
	color_icons_check_->setToolTip(obs_module_text("Dock.ColorIconsHint"));
	connect(color_icons_check_, &QCheckBox::toggled, this, [this](bool on) {
		color_icons_on_ = on;
		apply_row_icons_all();
		emit color_icons_toggled(on);
	});

	auto *buttons = new QHBoxLayout();
	buttons->addWidget(add);
	buttons->addWidget(color_icons_check_);
	buttons->addStretch();
	buttons->addWidget(start_all);
	buttons->addWidget(stop_all);
	layout->addLayout(buttons);
}

void DestinationsDock::set_rows(const std::vector<DestinationRowView> &rows)
{
	rows_.clear();
	while (rows_layout_->count() > 1) {
		// Later, because this can run from a click on a button inside the row.
		QLayoutItem *item = rows_layout_->takeAt(0);
		item->widget()->hide();
		item->widget()->deleteLater();
		delete item;
	}

	int position = 0;
	for (const DestinationRowView &view : rows) {
		rows_layout_->insertWidget(position++, create_row(view));
	}
	empty_->setVisible(rows.empty());
}

void DestinationsDock::update_status(const QString &id, const DestinationRowStatus &status)
{
	const auto found = rows_.find(id);
	if (found != rows_.end()) {
		apply_status(found->second, status);
	}
}

void DestinationsDock::show_problem(const QString &problem)
{
	problem_->setText(problem);
	problem_->show();
	dismiss_problem_->show();
}

QWidget *DestinationsDock::create_row(const DestinationRowView &view)
{
	auto *row = new QFrame();
	row->setFrameShape(QFrame::StyledPanel);

	RowWidgets widgets;
	widgets.icon_file = view.icon_file;
	widgets.brand_color = view.brand_color;
	widgets.icon = new QLabel(row);

	auto *name = new QLabel(view.name, row);
	name->setTextFormat(Qt::PlainText);
	name->setToolTip(view.name);
	name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

	auto *source = new QLabel(view.source, row);
	source->setStyleSheet("border: 1px solid palette(text); border-radius: 3px; padding: 0 4px;");

	widgets.dot = new QLabel(row);
	widgets.state = new QLabel(row);
	widgets.state->setTextFormat(Qt::PlainText);
	widgets.state->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	widgets.stats = new QLabel(row);
	widgets.stats->setTextFormat(Qt::PlainText);
	widgets.stats->setWordWrap(true);
	widgets.stats->hide();
	widgets.detail = wrapping_label(row);
	widgets.warning = wrapping_label(row);
	widgets.warning->setStyleSheet("color: #e88020;");

	widgets.toggle = new QToolButton(row);
	widgets.toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	widgets.toggle->setIconSize(QSize(kButtonIconSize, kButtonIconSize));

	auto *menu = new QMenu(row);
	widgets.move_up = menu->addAction(themed_icon("icons/ui/up.svg", palette()), obs_module_text("Dock.MoveUp"));
	widgets.move_down =
		menu->addAction(themed_icon("icons/ui/down.svg", palette()), obs_module_text("Dock.MoveDown"));
	menu->addSeparator();
	widgets.edit = menu->addAction(obs_module_text("Dock.Edit"));
	menu->addSeparator();
	widgets.remove = menu->addAction(obs_module_text("Dock.Delete"));
	widgets.more = new QToolButton(row);
	widgets.more->setAutoRaise(true);
	widgets.more->setPopupMode(QToolButton::InstantPopup);
	widgets.more->setStyleSheet("QToolButton::menu-indicator { image: none; }");
	widgets.more->setIconSize(QSize(kButtonIconSize, kButtonIconSize));
	widgets.more->setToolTip(obs_module_text("Dock.More"));
	widgets.more->setAccessibleName(obs_module_text("Dock.More"));
	widgets.more->setMenu(menu);

	const QString id = view.id;
	connect(widgets.toggle, &QToolButton::clicked, this, [this, id] {
		const auto found = rows_.find(id);
		if (found != rows_.end()) {
			if (found->second.stops) {
				emit stop_requested(id);
			} else {
				emit start_requested(id);
			}
		}
	});
	connect(widgets.edit, &QAction::triggered, this, [this, id] { emit edit_requested(id); });
	connect(widgets.remove, &QAction::triggered, this, [this, id] { emit delete_requested(id); });
	connect(widgets.move_up, &QAction::triggered, this, [this, id] { emit move_up_requested(id); });
	connect(widgets.move_down, &QAction::triggered, this, [this, id] { emit move_down_requested(id); });

	auto *title_line = new QHBoxLayout();
	title_line->setSpacing(6);
	title_line->addWidget(widgets.icon);
	title_line->addWidget(name, 1);
	title_line->addWidget(source);

	auto *status_line = new QHBoxLayout();
	status_line->setSpacing(4);
	status_line->addWidget(widgets.dot);
	status_line->addWidget(widgets.state, 1);
	status_line->addWidget(widgets.toggle);
	status_line->addWidget(widgets.more);

	auto *layout = new QVBoxLayout(row);
	layout->setContentsMargins(6, 4, 6, 4);
	layout->setSpacing(2);
	layout->addLayout(title_line);
	layout->addLayout(status_line);
	layout->addWidget(widgets.stats);
	layout->addWidget(widgets.detail);
	layout->addWidget(widgets.warning);

	apply_status(widgets, view.status);
	apply_row_icons(widgets);
	rows_.emplace(view.id, widgets);
	return row;
}

void DestinationsDock::apply_status(RowWidgets &widgets, const DestinationRowStatus &status)
{
	widgets.state->setText(status.state);
	widgets.state->setToolTip(status.detail);
	widgets.detail->setText(status.detail);
	widgets.detail->setVisible(!status.detail.isEmpty());
	widgets.warning->setText(status.warning);
	widgets.warning->setVisible(!status.warning.isEmpty());
	widgets.stats->setText(status.stats);
	widgets.stats->setVisible(!status.stats.isEmpty());

	const bool changed = widgets.stops != status.can_stop || widgets.tone != status.tone;
	widgets.stops = status.can_stop;
	widgets.tone = status.tone;
	const char *const label = status.can_stop ? "Dock.Stop" : "Dock.Start";
	widgets.toggle->setText(obs_module_text(label));
	widgets.toggle->setAccessibleName(obs_module_text(label));
	widgets.toggle->setEnabled(status.can_start || status.can_stop);
	widgets.edit->setEnabled(status.can_edit);
	widgets.remove->setEnabled(status.can_edit);
	if (changed) {
		apply_row_icons(widgets);
	}
}

void DestinationsDock::apply_row_icons(RowWidgets &widgets)
{
	const qreal ratio = devicePixelRatioF();
	const QColor platform_color = color_icons_on_ && widgets.brand_color.isValid()
					      ? widgets.brand_color
					      : palette().color(QPalette::Active, QPalette::WindowText);
	widgets.icon->setPixmap(colored_pixmap(widgets.icon_file.c_str(), platform_color, kPlatformIconSize, ratio));
	widgets.dot->setPixmap(status_dot(widgets.tone, palette(), ratio));
	widgets.toggle->setIcon(themed_icon(widgets.stops ? "icons/ui/stop.svg" : "icons/ui/start.svg", palette()));
	widgets.more->setIcon(themed_icon("icons/ui/more.svg", palette()));
}

void DestinationsDock::set_color_icons(bool on)
{
	color_icons_on_ = on;
	if (color_icons_check_) {
		const QSignalBlocker blocker(color_icons_check_);
		color_icons_check_->setChecked(on);
	}
	apply_row_icons_all();
}

void DestinationsDock::apply_row_icons_all()
{
	for (auto &[id, widgets] : rows_) {
		apply_row_icons(widgets);
	}
}

void DestinationsDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange) {
		apply_row_icons_all();
	}
}

} // namespace multistream
