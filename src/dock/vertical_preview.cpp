#include "dock/vertical_preview.hpp"

#include <QEvent>
#include <QHideEvent>
#include <QPlatformSurfaceEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QWindow>

namespace multistream {

VerticalPreview::VerticalPreview(QWidget *parent) : QWidget(parent)
{
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
}

void VerticalPreview::hideEvent(QHideEvent *event)
{
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

} // namespace multistream
