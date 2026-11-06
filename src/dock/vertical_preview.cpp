#include "dock/vertical_preview.hpp"

#include <QContextMenuEvent>
#include <QEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QWindow>

namespace multistream {
namespace {

// In logical pixels, so they feel the same whatever the preview's scale and DPI. Handles
// are easier to grab than they look, as in the main preview, and 5 is its snap distance.
constexpr double kHandleGrabRadius = 6.0;
constexpr double kSnapDistance = 5.0;
constexpr double kDragThreshold = 2.0;
constexpr int kOverlayIntervalMilliseconds = 33;

std::optional<NudgeDirection> nudge_direction(int key)
{
	switch (key) {
	case Qt::Key_Up:
		return NudgeDirection::Up;
	case Qt::Key_Down:
		return NudgeDirection::Down;
	case Qt::Key_Left:
		return NudgeDirection::Left;
	case Qt::Key_Right:
		return NudgeDirection::Right;
	default:
		return std::nullopt;
	}
}

bool is_remove_key(int key)
{
	return key == Qt::Key_Delete || key == Qt::Key_Backspace;
}

PreviewInteraction::Modifiers modifiers_of(const QInputEvent *event)
{
	// Qt's ControlModifier is the Command key on macOS, which is what the main preview
	// checks. MetaModifier is the Control key there, so both physical keys work.
	PreviewInteraction::Modifiers modifiers;
	modifiers.shift = event->modifiers().testFlag(Qt::ShiftModifier);
	modifiers.disable_snap =
		event->modifiers().testFlag(Qt::ControlModifier) || event->modifiers().testFlag(Qt::MetaModifier);
	return modifiers;
}

} // namespace

VerticalPreview::VerticalPreview(QWidget *parent) : QWidget(parent)
{
	setMouseTracking(true);
	setFocusPolicy(Qt::ClickFocus);
	overlay_timer_.setInterval(kOverlayIntervalMilliseconds);
	connect(&overlay_timer_, &QTimer::timeout, this, &VerticalPreview::refresh_overlay);

	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
}

VerticalPreview::~VerticalPreview()
{
	release();
}

void VerticalPreview::set_target(std::shared_ptr<VerticalPreviewTarget> target)
{
	release();
	target_ = std::move(target);
	if (isVisible()) {
		create_display();
	}
}

void VerticalPreview::set_canvas(VerticalCanvas *canvas)
{
	canvas_ = canvas;
	interaction_.select(std::nullopt);
	guides_ = {};
	if (canvas_ != nullptr && isVisible()) {
		overlay_timer_.start();
	} else if (canvas_ == nullptr) {
		overlay_timer_.stop();
		unsetCursor();
	}
	refresh_overlay();
}

void VerticalPreview::set_selected_item(std::optional<std::int64_t> id)
{
	if (id != interaction_.selection()) {
		interaction_.select(id);
		refresh_overlay();
	}
}

void VerticalPreview::release()
{
	if (display_) {
		obs_display_remove_draw_callback(display_, draw, this);
		display_ = nullptr;
	}
}

void VerticalPreview::draw(void *data, uint32_t width, uint32_t height)
{
	static_cast<VerticalPreview *>(data)->target_->render(width, height);
}

QSize VerticalPreview::pixel_size() const
{
	return size() * devicePixelRatioF();
}

void VerticalPreview::watch_window()
{
	QWindow *window = windowHandle();
	if (window == watched_window_) {
		return;
	}
	if (watched_window_ != nullptr) {
		watched_window_->removeEventFilter(this);
		disconnect(watched_window_, nullptr, this, nullptr);
	}
	watched_window_ = window;
	if (window != nullptr) {
		window->installEventFilter(this);
		connect(window, &QWindow::screenChanged, this, [this](QScreen *) {
			if (display_ && isVisible()) {
				const QSize pixels = pixel_size();
				obs_display_resize(display_, static_cast<uint32_t>(pixels.width()),
						   static_cast<uint32_t>(pixels.height()));
			}
		});
	}
}

void VerticalPreview::create_display()
{
	if (display_ || !target_ || !isVisible()) {
		return;
	}

	const QSize pixels = pixel_size();
	if (pixels.isEmpty()) {
		return;
	}

	const WId native = winId();
	watch_window();

	gs_init_data init = {};
	init.cx = static_cast<uint32_t>(pixels.width());
	init.cy = static_cast<uint32_t>(pixels.height());
	init.format = GS_BGRA;
	init.zsformat = GS_ZS_NONE;
#if defined(__APPLE__)
	init.window.view = reinterpret_cast<id>(native);
#else
	static_cast<void>(native);
	return;
#endif

	display_ = obs_display_create(&init, 0xFF000000);
	if (!display_) {
		return;
	}
	obs_display_add_draw_callback(display_, draw, this);
}

void VerticalPreview::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	create_display();
	if (canvas_ != nullptr) {
		overlay_timer_.start();
	}
}

void VerticalPreview::hideEvent(QHideEvent *event)
{
	overlay_timer_.stop();
	interaction_.cancel();
	release();
	QWidget::hideEvent(event);
}

void VerticalPreview::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (!isVisible()) {
		return;
	}
	if (!display_) {
		create_display();
		return;
	}
	const QSize pixels = pixel_size();
	if (!pixels.isEmpty()) {
		obs_display_resize(display_, static_cast<uint32_t>(pixels.width()), static_cast<uint32_t>(pixels.height()));
	}
}

void VerticalPreview::paintEvent(QPaintEvent *)
{
	create_display();
}

bool VerticalPreview::event(QEvent *event)
{
	// Claims these keys before OBS's window-wide shortcuts see them. Otherwise Delete here
	// would remove the source selected in OBS's main scene.
	if (event->type() == QEvent::ShortcutOverride) {
		const auto *key = static_cast<QKeyEvent *>(event);
		if (interaction_.selection() && (nudge_direction(key->key()) || is_remove_key(key->key()))) {
			event->accept();
			return true;
		}
	}
	// Docking and undocking can replace the native view that the display draws into.
	if (event->type() == QEvent::WinIdChange) {
		release();
		watch_window();
	}
	return QWidget::event(event);
}

bool VerticalPreview::eventFilter(QObject *watched, QEvent *event)
{
	if (watched == watched_window_ && event->type() == QEvent::PlatformSurface &&
	    static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() ==
		    QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
		release();
	}
	return QWidget::eventFilter(watched, event);
}

PreviewMapping VerticalPreview::mapping() const
{
	const QSize pixels = pixel_size();
	const CanvasSize canvas = canvas_ != nullptr ? canvas_->canvas_size() : CanvasSize{};
	return PreviewMapping(static_cast<int>(canvas.width), static_cast<int>(canvas.height), pixels.width(),
			      pixels.height());
}

Vec2 VerticalPreview::canvas_point(const QPointF &position) const
{
	const double ratio = devicePixelRatioF();
	return mapping().to_canvas({position.x() * ratio, position.y() * ratio});
}

PreviewInteraction::Tolerances VerticalPreview::tolerances() const
{
	const double canvas_per_logical = mapping().canvas_per_pixel() * devicePixelRatioF();
	PreviewInteraction::Tolerances result;
	result.handle_radius = kHandleGrabRadius * canvas_per_logical;
	result.snap_distance = kSnapDistance * canvas_per_logical;
	result.drag_threshold = kDragThreshold * canvas_per_logical;
	return result;
}

void VerticalPreview::refresh_overlay()
{
	if (!target_) {
		return;
	}

	PreviewOverlay overlay;
	overlay.pixel_ratio = devicePixelRatioF();
	overlay.guides = guides_;

	if (const auto id = interaction_.selection(); id && canvas_ != nullptr) {
		const std::optional<PreviewItem> item = canvas_->preview_item(*id);
		if (item) {
			if (item->visible) {
				overlay.box = item->box;
				overlay.handles = !item->locked;
			}
		} else if (!canvas_->contains_item(*id)) {
			lose_selection();
			overlay.guides = {};
		}
	}
	target_->set_overlay(overlay);
}

void VerticalPreview::lose_selection()
{
	interaction_.select(std::nullopt);
	guides_ = {};
	show_cursor(PreviewCursor::Arrow);
	emit selection_changed(0);
}

void VerticalPreview::show_cursor(PreviewCursor cursor)
{
	switch (cursor) {
	case PreviewCursor::Arrow:
		unsetCursor();
		break;
	case PreviewCursor::Move:
		setCursor(Qt::SizeAllCursor);
		break;
	case PreviewCursor::SizeHorizontal:
		setCursor(Qt::SizeHorCursor);
		break;
	case PreviewCursor::SizeVertical:
		setCursor(Qt::SizeVerCursor);
		break;
	case PreviewCursor::SizeDiagonalDown:
		setCursor(Qt::SizeFDiagCursor);
		break;
	case PreviewCursor::SizeDiagonalUp:
		setCursor(Qt::SizeBDiagCursor);
		break;
	}
}

void VerticalPreview::mousePressEvent(QMouseEvent *event)
{
	if (canvas_ == nullptr || !mapping().valid()) {
		return;
	}
	if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton) {
		return;
	}

	setFocus(Qt::MouseFocusReason);
	const bool changed =
		interaction_.press(canvas_point(event->position()), canvas_->preview_items(), tolerances());
	if (event->button() != Qt::LeftButton) {
		interaction_.cancel();
	}
	if (changed) {
		emit selection_changed(interaction_.selection().value_or(0));
	}
	refresh_overlay();
}

void VerticalPreview::mouseMoveEvent(QMouseEvent *event)
{
	if (canvas_ == nullptr || !mapping().valid()) {
		return;
	}

	const Vec2 point = canvas_point(event->position());
	if (!interaction_.gesture_active()) {
		show_cursor(interaction_.cursor_at(point, canvas_->preview_items(), tolerances().handle_radius));
		return;
	}

	const std::optional<BoxChange> change = interaction_.move(point, canvas_->canvas_size(), modifiers_of(event));
	const auto id = interaction_.selection();
	if (!change || !id) {
		return;
	}

	switch (canvas_->set_item_box(*id, change->box)) {
	case VerticalCanvas::BoxResult::Applied:
		guides_ = change->guides;
		break;
	case VerticalCanvas::BoxResult::Skipped:
		break;
	case VerticalCanvas::BoxResult::Gone:
		lose_selection();
		break;
	}
	refresh_overlay();
}

void VerticalPreview::mouseReleaseEvent(QMouseEvent *event)
{
	if (canvas_ == nullptr || event->button() != Qt::LeftButton || !interaction_.gesture_active()) {
		return;
	}

	const bool dragged = interaction_.dragging();
	const bool changed = interaction_.release(canvas_point(event->position()), canvas_->preview_items());
	guides_ = {};
	if (changed) {
		emit selection_changed(interaction_.selection().value_or(0));
	}
	if (dragged) {
		emit item_edited();
	}
	refresh_overlay();
}

void VerticalPreview::contextMenuEvent(QContextMenuEvent *event)
{
	if (canvas_ != nullptr && interaction_.selection()) {
		emit context_menu_requested(*interaction_.selection(), event->globalPos());
	}
}

void VerticalPreview::keyPressEvent(QKeyEvent *event)
{
	const auto id = interaction_.selection();
	if (canvas_ == nullptr || !id) {
		QWidget::keyPressEvent(event);
		return;
	}

	if (const auto direction = nudge_direction(event->key())) {
		nudge(*direction, event->modifiers().testFlag(Qt::ShiftModifier));
	} else if (is_remove_key(event->key())) {
		emit remove_requested(*id);
	} else {
		QWidget::keyPressEvent(event);
	}
}

void VerticalPreview::nudge(NudgeDirection direction, bool shift)
{
	const auto id = interaction_.selection();
	const std::optional<PreviewItem> item = canvas_->preview_item(*id);
	if (!item || item->locked) {
		return;
	}
	if (canvas_->set_item_box(*id, plan_nudge(item->box, direction, shift)) == VerticalCanvas::BoxResult::Gone) {
		lose_selection();
		return;
	}
	refresh_overlay();
	emit item_edited();
}

void VerticalPreview::leaveEvent(QEvent *event)
{
	if (!interaction_.gesture_active()) {
		unsetCursor();
	}
	QWidget::leaveEvent(event);
}

} // namespace multistream
