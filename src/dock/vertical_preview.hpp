// Draws the vertical canvas in a native window of its own, through an obs_display, which
// exists only while the widget is visible. The OpenGL renderer queues work for the main
// thread on a display resize, and that work crashes if the native view is gone by then
// (observed at exit), so a display must not outlive visibility or get resized when hidden.
#pragma once

#include "vertical_canvas/vertical_canvas.hpp"

#include <obs.hpp>

#include <QWidget>

#include <memory>

namespace multistream {

class VerticalPreview final : public QWidget {
	Q_OBJECT

public:
	explicit VerticalPreview(QWidget *parent = nullptr);
	~VerticalPreview() override;

	void set_target(std::shared_ptr<VerticalPreviewTarget> target);

	// Removes the draw callback and destroys the display. Safe to call repeatedly.
	void release();

	QPaintEngine *paintEngine() const override { return nullptr; }

protected:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	bool event(QEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	static void draw(void *data, uint32_t width, uint32_t height);

	QSize pixel_size() const;
	void create_display();
	void watch_window();

	OBSDisplay display_;
	QWindow *watched_window_ = nullptr;
	// Read on the graphics thread, so only changed while no draw callback is registered.
	std::shared_ptr<VerticalPreviewTarget> target_;
};

} // namespace multistream
