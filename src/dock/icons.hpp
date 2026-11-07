// Loads the plugin's monochrome SVG icons and paints them in the palette's text color, so
// they work on any theme. OBS's own Qt has the SVG image plugin (checked at runtime).
#pragma once

#include "destinations/platform.hpp"

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QSize>

class QAbstractButton;
class QWidget;

namespace multistream {

// The path is relative to the plugin's data directory. A missing or unreadable file gives
// an empty icon, which leaves the button's text and accessible name to do the work.
QIcon themed_icon(const char *relative_path, const QPalette &palette);
QPixmap themed_pixmap(const char *relative_path, const QPalette &palette, int logical_size, qreal pixel_ratio);

// The platform's brand color, or an invalid QColor for ones without one (Custom, X), where
// the theme's monochrome color is the right look.
QColor brand_color(Platform platform);

// Like themed_pixmap, but painted in an explicit color instead of the theme's.
QPixmap colored_pixmap(const char *relative_path, const QColor &color, int logical_size, qreal pixel_ratio);

// Applies an OBS theme utility class (Yami: text-danger, subtitle, btn-tool, ...). A change
// after the widget was shown only takes effect once the style polishes it again.
void set_theme_class(QWidget *widget, const char *css_class);

// Lets the button's theme class supply the icon (OBS's own toolbar icons) and uses the
// fallback only when the theme has none for that class. Call again on palette changes.
void apply_class_icon(QAbstractButton *button, const QIcon &fallback);

} // namespace multistream
