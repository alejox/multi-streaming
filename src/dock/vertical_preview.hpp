// Draws the vertical canvas in a native window of its own, through an obs_display, which
// exists only while the widget is visible. The OpenGL renderer queues work for the main
// thread on a display resize, and that work crashes if the native view is gone by then
// (observed at exit), so a display must not outlive visibility or get resized when hidden.
//
// It is also where items are edited directly, as in the main preview: the math lives in
// PreviewInteraction, and scene items are only ever touched here on the UI thread.
#pragma once

#include "vertical_canvas/vertical_canvas.hpp"

#include <obs.hpp>

#include <QTimer>
#include <QPointer>
#include <QWidget>

#include <cstdint>
#include <memory>
#include <optional>

namespace multistream {

class VerticalPreview final : public QWidget {
	Q_OBJECT

public:
	explicit VerticalPreview(QWidget *parent = nullptr);
	~VerticalPreview() override;

	void set_target(std::shared_ptr<VerticalPreviewTarget> target);

	// What gets edited. Nothing turns editing off.
	void set_canvas(VerticalCanvas *canvas);

	std::optional<std::int64_t> selected_item() const { return interaction_.selection(); }
	void set_selected_item(std::optional<std::int64_t> id);

	// Removes the draw callback and destroys the display. Safe to call repeatedly.
	void release();

	QPaintEngine *paintEngine() const override { return nullptr; }

signals:
	// 0 means nothing: scene item ids start at 1.
	void selection_changed(std::int64_t id);
	void context_menu_requested(std::int64_t id, const QPoint &global_position);
	void remove_requested(std::int64_t id);
	// A drag or nudge finished.
	void item_edited();

protected:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	bool event(QEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void contextMenuEvent(QContextMenuEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void leaveEvent(QEvent *event) override;

private:
	static void draw(void *data, uint32_t width, uint32_t height);

	QSize pixel_size() const;
	void create_display();
	void watch_window();
	// Re-syncs the native view and the OpenGL surface once the layout settles. force also
	// refreshes the surface when the native geometry was already right (a screen change,
	// or a move of the preview itself, which Qt applies to the native view on its own).
	void schedule_surface_sync(bool force);
	void sync_surface();
	// Qt moves a native child's view only when the child's own geometry changes. When an
	// ancestor without a native window (the dock) moves, the view keeps its old frame and
	// the picture floats where the preview used to be. Pushes the right geometry; returns
	// whether it was stale.
	bool sync_native_geometry();
	void watch_top_level();
	// Filters every ancestor widget, so a moved or resized dock or window triggers a sync.
	void watch_ancestors();
	PreviewMapping mapping() const;
	Vec2 canvas_point(const QPointF &position) const;
	PreviewInteraction::Tolerances tolerances() const;
	// Boxes of the other visible, unlocked items, to snap against.
	std::vector<ItemBox> other_boxes() const;
	void refresh_overlay();
	void lose_selection();
	void show_cursor(PreviewCursor cursor);
	void nudge(NudgeDirection direction, bool shift);

	OBSDisplay display_;
	// Disabled displays retired on undock, hide, or screen change. They are destroyed only
	// once purge_timer_ fires, which every retirement restarts, so each one is older than
	// the delay by then and any OpenGL swapchain update it queued has long finished.
	std::vector<OBSDisplay> retired_displays_;
	QTimer purge_timer_;
	QTimer sync_timer_;
	bool sync_forced_ = false;
	// The top-level window holding this preview, for its screen changes.
	QPointer<QWindow> watched_top_level_;
	std::vector<QPointer<QWidget>> watched_ancestors_;
	QWindow *watched_window_ = nullptr;
	// Read on the graphics thread, so only changed while no draw callback is registered.
	std::shared_ptr<VerticalPreviewTarget> target_;

	VerticalCanvas *canvas_ = nullptr;
	PreviewInteraction interaction_;
	SnapGuides guides_;
	QTimer overlay_timer_;
};

} // namespace multistream
