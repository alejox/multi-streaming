#include "dock/icons.hpp"

#include <obs-module.h>

#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QString>

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

} // namespace multistream
