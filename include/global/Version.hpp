#pragma once

#include <QRegularExpression>
#include <QString>
#include <array>
#include <optional>

namespace ReleaseVersion {
inline std::optional<std::array<qulonglong, 5>> Parse(const QString &version) {
    static const QRegularExpression pattern(R"(^(\d+)\.(\d+)(?:\.(\d+))?(?:-(alpha|beta|rc)\.(\d+))?$)");
    const auto match = pattern.match(version);
    if (!match.hasMatch()) return std::nullopt;
    std::array<qulonglong, 5> result{0, 0, 0, 4, 0};
    for (int i = 0; i < 3; ++i) {
        if (match.captured(i + 1).isEmpty()) continue;
        bool ok = false;
        result[i] = match.captured(i + 1).toULongLong(&ok);
        if (!ok) return std::nullopt;
    }
    if (!match.captured(4).isEmpty()) {
        const auto stage = match.captured(4);
        result[3] = stage == "alpha" ? 1 : stage == "beta" ? 2 : 3;
        bool ok = false;
        result[4] = match.captured(5).toULongLong(&ok);
        if (!ok) return std::nullopt;
    }
    return result;
}

inline bool IsNewer(const QString &candidate, const QString &current) {
    const auto next = Parse(candidate);
    const auto previous = Parse(current);
    return next && previous && *next > *previous;
}

inline QString FromAsset(const QString &asset) {
    static const QRegularExpression pattern(R"(^qThrone-(\d+\.\d+(?:\.\d+)?(?:-(?:alpha|beta|rc)\.\d+)?)-)");
    return pattern.match(asset).captured(1);
}
}
