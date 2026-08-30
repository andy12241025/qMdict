// Draws the qMdict application icon and writes the PNG set plus a Windows .ico.
//
// The artwork is generated rather than hand-drawn in an editor so it stays
// reproducible and tweakable. Run it after changing anything here:
//
//   cmake -S . -B build -DQMDICT_BUILD_ICON_TOOL=ON && cmake --build build
//   ./build/qmdict_make_icon resources
//
// Qt Svg is deliberately not used: qMdict links only qtbase, and adding a
// module just to rasterise one icon at build time is not worth it.

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <cstdio>

namespace {

// The two halves of an open book, in a 0..1 coordinate space.
QPainterPath bookPage(bool left)
{
    const auto x = [left](qreal v) { return left ? v : 1.0 - v; };

    QPainterPath path;
    path.moveTo(x(0.500), 0.325);
    path.quadTo(x(0.330), 0.245, x(0.148), 0.288);
    path.lineTo(x(0.148), 0.700);
    path.quadTo(x(0.330), 0.657, x(0.500), 0.737);
    path.closeSubpath();
    return path;
}

QImage renderIcon(int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(size, size);

    QLinearGradient gradient(0.0, 0.0, 1.0, 1.0);
    gradient.setColorAt(0.0, QColor(0x4f, 0x83, 0xf7));
    gradient.setColorAt(1.0, QColor(0x7b, 0x54, 0xef));

    QPainterPath background;
    background.addRoundedRect(QRectF(0.0, 0.0, 1.0, 1.0), 0.22, 0.22);
    painter.fillPath(background, gradient);

    // A soft highlight across the top keeps the tile from looking flat.
    QLinearGradient sheen(0.0, 0.0, 0.0, 0.55);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 46));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.fillPath(background, sheen);

    // Below 32px the magnifier and the book merge into a smudge, so tiny
    // sizes get a simplified, slightly larger book on its own.
    const bool tiny = size < 32;
    if (tiny) {
        painter.translate(0.5, 0.5);
        painter.scale(1.16, 1.16);
        painter.translate(-0.5, -0.5);
    }

    painter.fillPath(bookPage(true), QBrush(QColor(255, 255, 255)));
    painter.fillPath(bookPage(false), QBrush(QColor(228, 233, 248)));

    // The spine, drawn as a wedge so the book reads as open rather than flat.
    QPainterPath spine;
    spine.moveTo(0.5, 0.325);
    spine.lineTo(0.5, 0.737);
    painter.setPen(QPen(QColor(0x4f, 0x5b, 0x9a, 150), 0.022, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(spine);

    // Text lines, only where there are enough pixels to show them.
    if (size >= 48) {
        painter.setPen(QPen(QColor(0x6b, 0x76, 0xa8, 110), 0.017, Qt::SolidLine, Qt::RoundCap));
        for (int row = 0; row < 3; ++row) {
            const qreal y = 0.395 + row * 0.083;
            painter.drawLine(QPointF(0.225, y), QPointF(0.430, y - 0.012));
            painter.drawLine(QPointF(0.570, y - 0.012), QPointF(0.775, y));
        }
    }

    // A magnifier marks this as a lookup tool rather than a reader. It is
    // separated from the page by a stroke in the background gradient, which
    // keeps white-on-white from merging at small sizes.
    if (tiny) {
        painter.end();
        return image;
    }

    const QPointF centre(0.735, 0.725);
    const qreal radius = 0.138;

    QPainterPath lens;
    lens.addEllipse(centre, radius, radius);
    QPainterPath handle;
    handle.moveTo(centre.x() + 0.098, centre.y() + 0.098);
    handle.lineTo(0.912, 0.902);

    // Clear a gap around the badge, then fill the lens so the page beneath
    // does not show through as a stray white shape.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QBrush(gradient), 0.082, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(lens);
    painter.drawPath(handle);
    painter.fillPath(lens, gradient);

    painter.setPen(QPen(QColor(255, 255, 255), 0.050, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(lens);
    painter.setPen(QPen(QColor(255, 255, 255), 0.056, Qt::SolidLine, Qt::RoundCap));
    painter.drawPath(handle);

    painter.end();
    return image;
}

void appendLe(QByteArray *out, quint32 value, int width)
{
    for (int i = 0; i < width; ++i)
        out->append(char((value >> (8 * i)) & 0xff));
}

// A 32-bit BMP entry: header, bottom-up BGRA rows, then the (unused) AND mask.
QByteArray bmpEntry(const QImage &source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();

    QByteArray pixels;
    pixels.reserve(w * h * 4);
    for (int y = h - 1; y >= 0; --y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            pixels.append(char(qBlue(row[x])));
            pixels.append(char(qGreen(row[x])));
            pixels.append(char(qRed(row[x])));
            pixels.append(char(qAlpha(row[x])));
        }
    }

    const int maskStride = ((w + 31) / 32) * 4;
    const QByteArray mask(maskStride * h, '\0');

    QByteArray out;
    appendLe(&out, 40, 4);                 // BITMAPINFOHEADER size
    appendLe(&out, quint32(w), 4);
    appendLe(&out, quint32(h * 2), 4);     // colour data plus mask
    appendLe(&out, 1, 2);                  // planes
    appendLe(&out, 32, 2);                 // bits per pixel
    appendLe(&out, 0, 4);                  // BI_RGB
    appendLe(&out, quint32(pixels.size() + mask.size()), 4);
    appendLe(&out, 0, 4);
    appendLe(&out, 0, 4);
    appendLe(&out, 0, 4);
    appendLe(&out, 0, 4);
    out += pixels;
    out += mask;
    return out;
}

QByteArray pngEntry(const QImage &image)
{
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
}

bool writeIco(const QString &path, const QList<QImage> &images)
{
    QByteArray directory;
    appendLe(&directory, 0, 2); // reserved
    appendLe(&directory, 1, 2); // type: icon
    appendLe(&directory, quint32(images.size()), 2);

    QByteArray payload;
    quint32 offset = quint32(6 + images.size() * 16);

    for (const QImage &image : images) {
        // Windows only accepts PNG-compressed entries from Vista onwards, so
        // the small sizes stay as BMP for maximum compatibility.
        const QByteArray data = image.width() >= 128 ? pngEntry(image) : bmpEntry(image);

        appendLe(&directory, quint32(image.width() >= 256 ? 0 : image.width()), 1);
        appendLe(&directory, quint32(image.height() >= 256 ? 0 : image.height()), 1);
        appendLe(&directory, 0, 1); // palette size
        appendLe(&directory, 0, 1); // reserved
        appendLe(&directory, 1, 2); // planes
        appendLe(&directory, 32, 2);
        appendLe(&directory, quint32(data.size()), 4);
        appendLe(&directory, offset, 4);

        offset += quint32(data.size());
        payload += data;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(directory);
    file.write(payload);
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    const QString root = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("resources");
    if (!QDir().mkpath(root + QStringLiteral("/icons"))) {
        std::fprintf(stderr, "cannot create %s/icons\n", qPrintable(root));
        return 1;
    }

    const QList<int> sizes = {16, 24, 32, 48, 64, 128, 256};
    QList<QImage> images;

    for (int size : sizes) {
        const QImage image = renderIcon(size);
        images.append(image);

        const QString path = QStringLiteral("%1/icons/qmdict-%2.png").arg(root).arg(size);
        if (!image.save(path, "PNG")) {
            std::fprintf(stderr, "cannot write %s\n", qPrintable(path));
            return 1;
        }
        std::printf("wrote %s\n", qPrintable(path));
    }

    const QString ico = root + QStringLiteral("/qmdict.ico");
    if (!writeIco(ico, images)) {
        std::fprintf(stderr, "cannot write %s\n", qPrintable(ico));
        return 1;
    }
    std::printf("wrote %s\n", qPrintable(ico));

    return 0;
}
