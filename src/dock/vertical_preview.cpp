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

#include <obs-module.h>

#if defined(__linux__)
#include <obs-nix-platform.h>
#endif

namespace multistream {
namespace {

// In logical pixels, so they feel the same whatever the preview's scale and DPI. Handles
// are easier to grab than they look, as in the main preview, and 5 is its snap distance.
constexpr double kHandleGrabRadius = 6.0;
constexpr double kSnapDistance = 5.0;
constexpr double kDragThreshold = 2.0;
constexpr int kOverlayIntervalMilliseconds = 33;
// Long past any swapchain update a retired display queued on the main thread.
constexpr int kRetiredDisplayPurgeMilliseconds = 5000;
// How long the window must stay still after a screen change before the display is remade.
constexpr int kRecreateSettleMilliseconds = 300;

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

	recreate_timer_.setSingleShot(true);
	recreate_timer_.setInterval(kRecreateSettleMilliseconds);
	connect(&recreate_timer_, &QTimer::timeout, this, &VerticalPreview::recreate_display);

	purge_timer_.setSingleShot(true);
	purge_timer_.setInterval(kRetiredDisplayPurgeMilliseconds);
	connect(&purge_timer_, &QTimer::timeout, this, [this] { retired_displays_.clear(); });

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
	if (!display_) {
		return;
	}

	// Never destroy a display while the graphics thread can still resize it: the OpenGL
	// renderer queues a swapchain update on the main queue from a resize, and destroying
	// the swapchain first crashes that block (__gl_update_block_invoke). Disable it so the
	// graphics thread skips it, then retire it for the destructor, which runs after any
	// queued update has long finished.
	obs_display_set_enabled(display_, false);
	obs_display_remove_draw_callback(display_, draw, this);
	retired_displays_.push_back(std::move(display_));
	purge_timer_.start();
}

void VerticalPreview::schedule_recreate(const char *reason)
{
	recreate_reason_ = reason;
	recreate_forced_ = true;
	recreate_timer_.start();
}

void VerticalPreview::schedule_position_check()
{
	if (!display_) {
		return;
	}
	if (!recreate_forced_) {
		recreate_reason_ = "moved inside its window";
	}
	recreate_timer_.start();
}

QPoint VerticalPreview::position_in_window() const
{
	return mapTo(window(), QPoint(0, 0));
}

void VerticalPreview::recreate_display()
{
	const bool forced = recreate_forced_;
	recreate_forced_ = false;
	if (!isVisible()) {
		return;
	}
	const QPoint in_window = position_in_window();
	if (!forced && display_ && in_window == display_position_) {
		return;
	}
	// On macOS the OpenGL swapchain re-attaches its surface to the view only when its pixel
	// size changes. A move to another screen, or a move inside the window at the same size
	// (a wider window shifting the docks), leaves the view blank or the picture floating
	// where the view used to be. A fresh display, made once the window has settled,
	// attaches to the view where it is now.
	const QSize pixels = pixel_size();
	const QScreen *current = screen();
	blog(LOG_INFO,
	     "[obs-multi-stream] vertical preview %s, recreating its display at %dx%d px, at %d,%d in its window, "
	     "on screen '%s' (ratio %.2f)",
	     recreate_reason_, pixels.width(), pixels.height(), in_window.x(), in_window.y(),
	     current != nullptr ? current->name().toUtf8().constData() : "?", devicePixelRatioF());
	release();
	create_display();
}

void VerticalPreview::watch_top_level()
{
	QWidget *top = window();
	QWindow *handle = top != nullptr ? top->windowHandle() : nullptr;
	if (handle == watched_top_level_) {
		return;
	}
	if (watched_top_level_ != nullptr) {
		watched_top_level_->removeEventFilter(this);
		disconnect(watched_top_level_, nullptr, this, nullptr);
	}
	watched_top_level_ = handle;
	if (handle != nullptr) {
		handle->installEventFilter(this);
		connect(handle, &QWindow::screenChanged, this,
			[this](QScreen *) { schedule_recreate("moved to another screen"); });
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
	watch_top_level();
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
		connect(window, &QWindow::screenChanged, this,
			[this](QScreen *) { schedule_recreate("moved to another screen"); });
		// Same as OBSQTDisplay: becoming visible again creates or resizes the display.
		connect(window, &QWindow::visibleChanged, this, [this](bool visible) {
			if (!visible || !isVisible()) {
				return;
			}
			if (!display_) {
				create_display();
				return;
			}
			const QSize pixels = pixel_size();
			obs_display_resize(display_, static_cast<uint32_t>(pixels.width()),
					   static_cast<uint32_t>(pixels.height()));
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
#elif defined(_WIN32)
	init.window.hwnd = reinterpret_cast<void *>(native);
#elif defined(__linux__)
	// Same as OBSQTDisplay in OBS Studio. It waits for exposure because a Wayland surface
	// is not usable before that; the paint event that follows exposure retries.
	if (windowHandle() == nullptr || !windowHandle()->isExposed()) {
		return;
	}
	switch (obs_get_nix_platform()) {
	case OBS_NIX_PLATFORM_X11_EGL:
		init.window.id = static_cast<uint32_t>(native);
		init.window.display = obs_get_nix_platform_display();
		break;
	case OBS_NIX_PLATFORM_WAYLAND:
		init.window.display = reinterpret_cast<void *>(native);
		if (init.window.display == nullptr) {
			return;
		}
		break;
	default:
		return;
	}
#else
	static_cast<void>(native);
	return;
#endif

	// Gray background (0xAABBGGRR) so the margins stand apart from the black canvas
	// rectangle, the same way the main OBS preview shows where the canvas ends.
	display_ = obs_display_create(&init, 0xFF4A4A4A);
	if (!display_) {
		return;
	}
	display_position_ = position_in_window();
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
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
	if (event->type() == QEvent::DevicePixelRatioChange) {
		schedule_recreate("changed pixel ratio");
	}
#endif
	if (event->type() == QEvent::Move) {
		schedule_position_check();
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
	// While a recreate waits, every move of the window pushes it back until the move ends.
	if (watched == watched_top_level_ && event->type() == QEvent::Move && recreate_timer_.isActive()) {
		recreate_timer_.start();
	}
	// A resized window or a rearranged dock can shift the preview without resizing it.
	if ((watched == watched_top_level_ && event->type() == QEvent::Resize) ||
	    (watched == watched_window_ && event->type() == QEvent::Move)) {
		schedule_position_check();
	}
	// A display lost while the window was hidden or off screen comes back once it is
	// exposed again, without waiting for a resize.
	if (watched == watched_window_ && event->type() == QEvent::Expose && watched_window_->isExposed() &&
	    !display_) {
		create_display();
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

std::vector<ItemBox> VerticalPreview::other_boxes() const
{
	std::vector<ItemBox> others;
	const auto selected = interaction_.selection();
	for (const PreviewItem &item : canvas_->preview_items()) {
		if (item.id != selected && item.visible && !item.locked) {
			others.push_back(item.box);
		}
	}
	return others;
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

	const std::optional<BoxChange> change =
		interaction_.move(point, canvas_->canvas_size(), other_boxes(), modifiers_of(event));
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
