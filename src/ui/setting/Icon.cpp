#include "include/ui/setting/Icon.hpp"

#include "include/global/Configs.hpp"

#include <QHash>
#include <QPixmap>
#include <QPainter>

#include <optional>

namespace {
    QHash<Icon::TrayIconStatus, QIcon> g_trayIcons;
    std::optional<bool> g_trayIconsCustom;

    QString statusName(Icon::TrayIconStatus status) {
        switch (status) {
            case Icon::TrayIconStatus::None: return QStringLiteral("Off");
            case Icon::TrayIconStatus::Connecting: return QStringLiteral("Connecting");
            case Icon::TrayIconStatus::Running: return QStringLiteral("Throne");
            case Icon::TrayIconStatus::SystemProxy: return QStringLiteral("Proxy");
            case Icon::TrayIconStatus::Vpn: return QStringLiteral("Tun");
        }
        MW_show_log("Icon::GetTrayIcon: Unknown status");
        return QStringLiteral("Off");
    }

    QIcon loadNamedIcon(const QString &name, bool useCustom, const QString &customFallback = {}) {
        if (useCustom) {
            for (const auto &candidate : {name, customFallback}) {
                if (candidate.isEmpty()) continue;
                // QIcon(path).isNull() is not a decode check: a PNG with a valid signature and a corrupt body passes it.
                if (const auto custom = QPixmap(QStringLiteral("icons/") + candidate + QStringLiteral(".png")); !custom.isNull()) return QIcon(custom);
            }
        }
        return QIcon(QStringLiteral(":/Throne/") + name + QStringLiteral(".png"));
    }
} // namespace

void Icon::InvalidateTrayIconCache() {
    g_trayIcons.clear();
}

QIcon Icon::GetTrayIcon(TrayIconStatus status) {
    const bool useCustom = Configs::dataManager->settingsRepo->use_custom_icons;
    if (g_trayIconsCustom != useCustom) {
        g_trayIcons.clear();
        g_trayIconsCustom = useCustom;
    }
    if (const auto it = g_trayIcons.constFind(status); it != g_trayIcons.cend()) return it.value();

    // A custom set without Connecting.png keeps its own Off icon rather than mixing in the bundled one.
    const QString customFallback = status == TrayIconStatus::Connecting ? statusName(TrayIconStatus::None) : QString();
    QIcon icon = loadNamedIcon(statusName(status), useCustom, customFallback);
    if (!useCustom || status == TrayIconStatus::Vpn) {
        // Preserve the original silhouette, transparency and antialiased edges.
        const QColor color = status == TrayIconStatus::Vpn ? QColor(Qt::white)
            : status == TrayIconStatus::SystemProxy ? QColor("#C4A7F5") : QColor("#A7DFFF");
        QIcon tinted;
        auto sizes = icon.availableSizes();
        if (sizes.isEmpty()) sizes = {QSize(16,16), QSize(32,32), QSize(64,64)};
        for (const auto &size : sizes) {
            auto pixmap = icon.pixmap(size);
            if (status == TrayIconStatus::Vpn || (!useCustom && status == TrayIconStatus::SystemProxy)) {
                auto image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
                int darkest = 255, lightest = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        const auto pixel = image.pixel(x, y);
                        if (qAlpha(pixel) < 128) continue;
                        darkest = qMin(darkest, qGray(pixel));
                        lightest = qMax(lightest, qGray(pixel));
                    }
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        const auto pixel = image.pixel(x, y);
                        const int shade = lightest > darkest
                            ? qBound(194, 194 + 61 * (qGray(pixel) - darkest) / (lightest - darkest), 255) : 255;
                        if (status == TrayIconStatus::SystemProxy) {
                            const int r = 134 + 62 * (shade - 194) / 61;
                            const int g = 94 + 73 * (shade - 194) / 61;
                            const int b = 194 + 51 * (shade - 194) / 61;
                            image.setPixel(x, y, qRgba(r, g, b, qAlpha(pixel)));
                        } else {
                            image.setPixel(x, y, qRgba(shade, shade, shade, qAlpha(pixel)));
                        }
                    }
                tinted.addPixmap(QPixmap::fromImage(image));
                continue;
            }
            QPainter painter(&pixmap);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), color);
            painter.end();
            tinted.addPixmap(pixmap);
        }
        icon = tinted;
    }
    g_trayIcons.insert(status, icon);
    return icon;
}

QIcon Icon::GetTaskbarIcon(TrayIconStatus status) {
    const auto &settings = Configs::dataManager->settingsRepo;
    // The bundled icon only: a custom PNG has no room for the padding macOS adds in the dock.
    if (settings->use_custom_icons && !settings->follow_status_in_taskbar) {
        return QIcon(QStringLiteral(":/Throne/Throne.png"));
    }
    return GetTrayIcon(status);
}
