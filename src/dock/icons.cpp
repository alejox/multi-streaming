#include "dock/icons.hpp"

#include <obs-module.h>

#include <QAbstractButton>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QString>
#include <QStyle>
#include <QWidget>

namespace multistream {
namespace {

constexpr int kIconSizes[] = {16, 24, 32, 48};

QPixmap render(const QString &path, const QColor &color, int pixels)
{
	QImageReader reader(path, "svg");
	reader.setScaledSize(QSize(pixels, pixels));
	QImage image = reader.read();
	if (image.isNull()) {
		return {};
	}

	image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	QPainter painter(&image);
	painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
	painter.fillRect(image.rect(), color);
	painter.end();
	return QPixmap::fromImage(image);
}

QString data_path(const char *relative_path)
{
	char *path = obs_module_file(relative_path);
	const QString result = path != nullptr ? QString::fromUtf8(path) : QString();
	bfree(path);
	return result;
}

} // namespace

QIcon themed_icon(const char *relative_path, const QPalette &palette)
{
	const QString path = data_path(relative_path);
	QIcon icon;
	if (path.isEmpty()) {
		return icon;
	}
	for (const int pixels : kIconSizes) {
		icon.addPixmap(render(path, palette.color(QPalette::Active, QPalette::ButtonText), pixels), QIcon::Normal);
		icon.addPixmap(render(path, palette.color(QPalette::Disabled, QPalette::ButtonText), pixels),
			       QIcon::Disabled);
	}
	return icon;
}

QPixmap themed_pixmap(const char *relative_path, const QPalette &palette, int logical_size, qreal pixel_ratio)
{
	const QString path = data_path(relative_path);
	if (path.isEmpty()) {
		return {};
	}
	QPixmap pixmap = render(path, palette.color(QPalette::Active, QPalette::WindowText),
				qRound(logical_size * pixel_ratio));
	pixmap.setDevicePixelRatio(pixel_ratio);
	return pixmap;
}

QColor brand_color(Platform platform)
{
	switch (platform) {
	case Platform::YouTube:
		return QColor(0xFF, 0x00, 0x00);
	case Platform::Kick:
		return QColor(0x53, 0xFC, 0x18);
	case Platform::Twitch:
		return QColor(0x91, 0x46, 0xFF);
	case Platform::Facebook:
		return QColor(0x18, 0x77, 0xF2);
	case Platform::TikTok:
		return QColor(0xFE, 0x2C, 0x55);
	case Platform::Instagram:
		return QColor(0xE4, 0x40, 0x5F);
	case Platform::X:
	case Platform::Custom:
	default:
		return QColor();
	}
}

QPixmap colored_pixmap(const char *relative_path, const QColor &color, int logical_size, qreal pixel_ratio)
{
	const QString path = data_path(relative_path);
	if (path.isEmpty()) {
		return {};
	}
	QPixmap pixmap = render(path, color, qRound(logical_size * pixel_ratio));
	pixmap.setDevicePixelRatio(pixel_ratio);
	return pixmap;
}

void set_theme_class(QWidget *widget, const char *css_class)
{
	if (widget->property("class").toString() == QLatin1String(css_class)) {
		return;
	}
	widget->setProperty("class", QString::fromLatin1(css_class));
	widget->style()->unpolish(widget);
	widget->style()->polish(widget);
}

// A QSS qproperty-icon is applied while polishing, and a later setIcon would overwrite it,
// so the fallback is only set once polishing left the icon empty.
void apply_class_icon(QAbstractButton *button, const QIcon &fallback)
{
	button->setIcon(QIcon());
	button->style()->unpolish(button);
	button->style()->polish(button);
	if (button->icon().isNull()) {
		button->setIcon(fallback);
	}
}

} // namespace multistream
