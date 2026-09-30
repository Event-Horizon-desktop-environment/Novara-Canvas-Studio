#include "UX/theme_icons.hpp"

#include "UX/theme_tokens.hpp"

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QIconEngine>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QSvgRenderer>

#include <algorithm>
#include <utility>

namespace canvas::gui {

namespace {

const QColor kReservedAccent(0xE5, 0x48, 0x4D);

qreal global_dpr() {
    qreal dpr = 1.0;
    if (QApplication* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        dpr = app->devicePixelRatio();
        if (QScreen* s = app->primaryScreen()) {
            const qreal sdpr = s->devicePixelRatio();
            if (sdpr > dpr)
                dpr = sdpr;
        }
    }
    if (dpr <= 0.0)
        dpr = 1.0;
    return dpr;
}

class SvgIconEngine : public QIconEngine {
public:
    explicit SvgIconEngine(QString file, QColor normal = QColor(), bool tint = true)
        : normal_(std::move(normal)), file_(std::move(file)), tint_(tint) {}

    QIconEngine* clone() const override { return new SvgIconEngine(file_, normal_, tint_); }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        if (!painter || rect.isEmpty())
            return;
        qreal dpr = global_dpr();
        if (QPaintDevice* dev = painter->device()) {
            const qreal pdpr = dev->devicePixelRatioF();
            if (pdpr > 0.0)
                dpr = pdpr;
        }
        painter->drawPixmap(rect, render(rect.size(), dpr, mode));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State) override {
        return render(size, global_dpr(), mode);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State, qreal scale) override {
        const qreal dpr = (scale > 0.0) ? scale : global_dpr();
        return render(size, dpr, mode);
    }

    QPixmap render(const QSize& logical, qreal dpr, QIcon::Mode mode) const {
        if (logical.isEmpty())
            return QPixmap();
        if (dpr <= 0.0)
            dpr = 1.0;
        const int pw = std::max(1, qRound(logical.width() * dpr));
        const int ph = std::max(1, qRound(logical.height() * dpr));
        const QSize px(pw, ph);
        constexpr int kSupersample = 2;
        const QSize big(pw * kSupersample, ph * kSupersample);
        QImage hi(big, QImage::Format_ARGB32_Premultiplied);
        hi.fill(Qt::transparent);
        {
            QSvgRenderer renderer(QStringLiteral(":/icons/%1.svg").arg(file_));
            QPainter p(&hi);
            p.setRenderHint(QPainter::Antialiasing);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            renderer.render(&p, QRectF(QPointF(0, 0), QSizeF(big)));
            p.end();
        }
        QImage small = hi.scaled(px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QColor tint = modeColor(mode);
        if (normal_.isValid() && (mode == QIcon::Normal || mode == QIcon::Selected)) {
            tint = normal_;
        }
        if (tint_ && tint.isValid() && tint != QColor(Qt::transparent)) {
            QPainter tp(&small);
            tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tp.fillRect(QRect(QPoint(0, 0), px), tint);
            tp.end();
            QImage accent;
            if (extractReservedAccent(hi, &accent)) {
                QImage scaledAccent =
                    accent.scaled(px, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                QPainter ov(&small);
                ov.setRenderHint(QPainter::SmoothPixmapTransform);
                ov.setCompositionMode(QPainter::CompositionMode_SourceOver);
                ov.drawImage(QRect(QPoint(0, 0), px), scaledAccent);
                ov.end();
            }
        }
        QPixmap pm = QPixmap::fromImage(small);
        pm.setDevicePixelRatio(dpr);
        return pm;
    }

    static bool extractReservedAccent(const QImage& srcImg, QImage* out) {
        const QImage img = srcImg.convertToFormat(QImage::Format_ARGB32);
        QImage sel(img.size(), QImage::Format_ARGB32);
        sel.fill(Qt::transparent);
        const QRgb accent = kReservedAccent.rgba();
        bool any = false;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            QRgb* srow = reinterpret_cast<QRgb*>(sel.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const QRgb c = row[x];
                const int r = qRed(c), g = qGreen(c), b = qBlue(c);
                if (r >= 140 && (r - g) > 120 && (r - b) > 120) {
                    srow[x] = qRgba(qRed(accent), qGreen(accent), qBlue(accent), qAlpha(c));
                    any = true;
                }
            }
        }
        if (!any)
            return false;
        *out = sel;
        return true;
    }

private:
    static QColor modeColor(QIcon::Mode mode) {
        const ThemeTokens& t = tokens();
        switch (mode) {
            case QIcon::Normal:
            case QIcon::Selected:
                return t.icon;
            case QIcon::Active:
                return t.icon;
            case QIcon::Disabled:
                return t.ink_faint;
        }
        return QColor(Qt::transparent);
    }

    QColor normal_;
    QString file_;
    bool tint_;
};

}

QIcon icon(const char* name) { return QIcon(new SvgIconEngine(QString::fromLatin1(name))); }

QIcon icon(const char* name, const QColor& normal) {
    return QIcon(new SvgIconEngine(QString::fromLatin1(name), normal));
}

QIcon raw_icon(const char* name) {
    return QIcon(new SvgIconEngine(QString::fromLatin1(name), QColor(), false));
}

}
