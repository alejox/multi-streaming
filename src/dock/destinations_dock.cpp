#include "dock/destinations_dock.hpp"

#include "dock/icons.hpp"

#include <obs-module.h>

#include <QApplication>
#include <QCheckBox>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
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
constexpr int kHandleIconSize = 16;
constexpr char kDestinationMime[] = "application/x-obs-multi-stream-destination";

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
	rows_container_ = rows_container;
	rows_container_->setAcceptDrops(true);
	rows_container_->installEventFilter(this);
	drop_indicator_ = new QFrame(rows_container);
	drop_indicator_->setStyleSheet("background-color: palette(highlight);");
	drop_indicator_->hide();

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
	handle_ids_.clear();
	pressed_handle_ = nullptr;
	drop_indicator_->hide();
	row_count_ = static_cast<int>(rows.size());
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

	widgets.handle = new QLabel(row);
	widgets.handle->setCursor(Qt::OpenHandCursor);
	widgets.handle->setToolTip(obs_module_text("Dock.DragHint"));
	widgets.handle->installEventFilter(this);
	handle_ids_[widgets.handle] = view.id;

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

	auto *outer = new QHBoxLayout(row);
	outer->setContentsMargins(2, 4, 6, 4);
	outer->setSpacing(4);
	outer->addWidget(widgets.handle, 0, Qt::AlignVCenter);
	auto *layout = new QVBoxLayout();
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(2);
	outer->addLayout(layout, 1);
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
	widgets.handle->setPixmap(themed_pixmap("icons/ui/drag.svg", palette(), kHandleIconSize, ratio));
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

bool DestinationsDock::eventFilter(QObject *watched, QEvent *event)
{
	const auto handle = handle_ids_.find(watched);
	if (handle != handle_ids_.end()) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		switch (event->type()) {
		case QEvent::MouseButtonPress:
			if (mouse->button() == Qt::LeftButton) {
				pressed_handle_ = watched;
				drag_start_ = mouse->position().toPoint();
				return true;
			}
			break;
		case QEvent::MouseMove:
			if (pressed_handle_ == watched && (mouse->buttons() & Qt::LeftButton) &&
			    (mouse->position().toPoint() - drag_start_).manhattanLength() >=
				    QApplication::startDragDistance()) {
				pressed_handle_ = nullptr;
				const QString id = handle->second;
				start_drag(static_cast<QWidget *>(watched), id);
				return true;
			}
			break;
		case QEvent::MouseButtonRelease:
			pressed_handle_ = nullptr;
			break;
		default:
			break;
		}
		return QWidget::eventFilter(watched, event);
	}

	if (watched == rows_container_) {
		switch (event->type()) {
		case QEvent::DragEnter:
		case QEvent::DragMove: {
			auto *drag = static_cast<QDragMoveEvent *>(event);
			if (!drag->mimeData()->hasFormat(kDestinationMime)) {
				return false;
			}
			drag->acceptProposedAction();
			show_drop_indicator(drop_position(drag->position().toPoint().y()));
			return true;
		}
		case QEvent::DragLeave:
			drop_indicator_->hide();
			return true;
		case QEvent::Drop: {
			auto *drop = static_cast<QDropEvent *>(event);
			drop_indicator_->hide();
			if (!drop->mimeData()->hasFormat(kDestinationMime)) {
				return false;
			}
			pending_move_ = std::make_pair(QString::fromUtf8(drop->mimeData()->data(kDestinationMime)),
						       drop_position(drop->position().toPoint().y()));
			drop->acceptProposedAction();
			return true;
		}
		default:
			break;
		}
	}
	return QWidget::eventFilter(watched, event);
}

void DestinationsDock::start_drag(QWidget *handle, const QString &id)
{
	QWidget *row = handle->parentWidget();

	auto *mime = new QMimeData();
	mime->setData(kDestinationMime, id.toUtf8());
	auto *drag = new QDrag(handle);
	drag->setMimeData(mime);
	drag->setPixmap(row->grab());
	drag->setHotSpot(handle->mapTo(row, drag_start_));

	// Dim the row being moved while it is dragged.
	auto *dim = new QGraphicsOpacityEffect(row);
	dim->setOpacity(0.4);
	row->setGraphicsEffect(dim);

	pending_move_.reset();
	drag->exec(Qt::MoveAction);

	row->setGraphicsEffect(nullptr);
	drop_indicator_->hide();

	// Emitting rebuilds the rows; doing it here, after the drag loop, keeps the handle
	// that owns this drag alive until the rebuild's deferred deletes run.
	if (pending_move_) {
		const auto [moved_id, position] = *pending_move_;
		pending_move_.reset();
		emit move_to_requested(moved_id, position);
	}
}

int DestinationsDock::drop_position(int y) const
{
	for (int index = 0; index < row_count_; ++index) {
		const QWidget *row = rows_layout_->itemAt(index)->widget();
		if (y < row->geometry().center().y()) {
			return index;
		}
	}
	return row_count_;
}

void DestinationsDock::show_drop_indicator(int position)
{
	if (row_count_ == 0) {
		drop_indicator_->hide();
		return;
	}
	const int half_gap = rows_layout_->spacing() / 2;
	const int y = position < row_count_
			      ? rows_layout_->itemAt(position)->widget()->geometry().top() - half_gap
			      : rows_layout_->itemAt(row_count_ - 1)->widget()->geometry().bottom() + half_gap;
	drop_indicator_->setGeometry(0, qMax(0, y - 1), rows_container_->width(), 2);
	drop_indicator_->raise();
	drop_indicator_->show();
}

void DestinationsDock::changeEvent(QEvent *event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ThemeChange) {
		apply_row_icons_all();
	}
}

} // namespace multistream
