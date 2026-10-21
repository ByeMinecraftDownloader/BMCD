#include "theme/TokenBuilder.h"

#include "theme/ColorMath.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace theme {
namespace {

struct Pair
{
    QColor Tokens::*foreground;
    QColor Tokens::*background;
    double target;
};

const Pair kPairs[] = {
    {&Tokens::onSurface, &Tokens::surface, 7.0},
    {&Tokens::onSurface, &Tokens::surfaceContainer, 7.0},
    {&Tokens::onSurface, &Tokens::surfaceContainerHigh, 7.0},
    {&Tokens::onSurfaceVariant, &Tokens::surface, 4.5},
    {&Tokens::primary, &Tokens::surface, 3.0},
    {&Tokens::onPrimary, &Tokens::primary, 4.5},
    {&Tokens::onPrimaryContainer, &Tokens::primaryContainer, 4.5},
    {&Tokens::outline, &Tokens::surface, 3.0},
    {&Tokens::error, &Tokens::surface, 3.0},
    {&Tokens::warning, &Tokens::surface, 3.0},
    {&Tokens::success, &Tokens::surface, 3.0},
    {&Tokens::link, &Tokens::surface, 3.0},
};

QString hex(const QColor &color)
{
    return color.name(QColor::HexArgb);
}

} // namespace

Tokens buildTokens(const QColor &accent, bool dark)
{
    double l = 0.0;
    double c = 0.0;
    double h = 195.0;
    if (accent.isValid()) {
        rgbToLch(accent, &l, &c, &h);
        if (c < 4.0)
            c = 4.0; // a grey accent still needs a recognisable primary
    }

    Tokens tokens;
    tokens.dark = dark;
    tokens.sourceName = accent.isValid() ? accent.name(QColor::HexRgb) : QStringLiteral("default");

    // Neutral chroma: a shadow of the accent hue keeps surfaces from looking
    // pasted on top of the desktop, while staying under the "tinted grey" line.
    const double neutral = std::clamp(c * 0.12, 0.0, 5.0);

    if (dark) {
        tokens.surface = lchToRgbClipped(7.0, neutral * 0.6, h);
        tokens.surfaceContainer = lchToRgbClipped(11.0, neutral * 0.8, h);
        tokens.surfaceContainerHigh = lchToRgbClipped(15.0, neutral, h);
        tokens.onSurface = lchToRgbClipped(94.0, neutral * 0.4, h);
        tokens.onSurfaceVariant = lchToRgbClipped(78.0, neutral * 0.7, h);
        tokens.primary = lchToRgbClipped(80.0, std::clamp(c, 10.0, 44.0), h);
        tokens.onPrimary = lchToRgbClipped(18.0, std::clamp(c * 0.25, 0.0, 12.0), h);
        tokens.primaryContainer = lchToRgbClipped(32.0, std::clamp(c * 0.7, 8.0, 36.0), h);
        tokens.onPrimaryContainer = lchToRgbClipped(90.0, std::clamp(c * 0.3, 0.0, 16.0), h);
        tokens.outline = lchToRgbClipped(56.0, neutral * 0.9, h);
        tokens.error = lchToRgbClipped(78.0, 62.0, 25.0);
        tokens.warning = lchToRgbClipped(83.0, 55.0, 80.0);
        tokens.success = lchToRgbClipped(74.0, 45.0, 148.0);
        tokens.link = lchToRgbClipped(84.0, std::clamp(c, 10.0, 40.0), h);
    } else {
        tokens.surface = lchToRgbClipped(98.0, neutral * 0.8, h);
        tokens.surfaceContainer = lchToRgbClipped(94.0, neutral, h);
        tokens.surfaceContainerHigh = lchToRgbClipped(90.0, neutral * 1.1, h);
        tokens.onSurface = lchToRgbClipped(12.0, neutral * 0.4, h);
        tokens.onSurfaceVariant = lchToRgbClipped(35.0, neutral * 0.7, h);
        tokens.primary = lchToRgbClipped(40.0, std::clamp(c, 12.0, 60.0), h);
        tokens.onPrimary = lchToRgbClipped(99.0, std::clamp(c * 0.2, 0.0, 10.0), h);
        tokens.primaryContainer = lchToRgbClipped(90.0, std::clamp(c * 0.5, 8.0, 30.0), h);
        tokens.onPrimaryContainer = lchToRgbClipped(20.0, std::clamp(c * 0.4, 0.0, 20.0), h);
        tokens.outline = lchToRgbClipped(52.0, neutral * 0.9, h);
        tokens.error = lchToRgbClipped(45.0, 65.0, 25.0);
        tokens.warning = lchToRgbClipped(45.0, 60.0, 80.0);
        tokens.success = lchToRgbClipped(40.0, 50.0, 148.0);
        tokens.link = lchToRgbClipped(38.0, std::clamp(c, 12.0, 55.0), h);
    }
    return tokens;
}

bool enforceContrast(Tokens &tokens)
{
    for (const Pair &pair : kPairs) {
        QColor &foreground = tokens.*pair.foreground;
        const QColor background = tokens.*pair.background;
        if (contrast(foreground, background) >= pair.target)
            continue;
        foreground = ensureContrast(foreground, background, pair.target);
    }
    for (const Pair &pair : kPairs) {
        if (contrast(tokens.*pair.foreground, tokens.*pair.background) < pair.target)
            return false;
    }
    return true;
}

Tokens fallbackTokens(bool dark)
{
    Tokens tokens;
    tokens.dark = dark;
    tokens.sourceName = QStringLiteral("fallback");
    if (dark) {
        tokens.surface = QColor(0x10, 0x14, 0x18);
        tokens.surfaceContainer = QColor(0x17, 0x1c, 0x21);
        tokens.surfaceContainerHigh = QColor(0x1f, 0x25, 0x2b);
        tokens.onSurface = QColor(0xe6, 0xea, 0xef);
        tokens.onSurfaceVariant = QColor(0xa8, 0xb3, 0xbf);
        tokens.primary = QColor(0x80, 0xd4, 0xd7);
        tokens.onPrimary = QColor(0x06, 0x1f, 0x21);
        tokens.primaryContainer = QColor(0x1c, 0x3d, 0x40);
        tokens.onPrimaryContainer = QColor(0xc9, 0xf2, 0xf4);
        tokens.outline = QColor(0x5a, 0x66, 0x72);
        tokens.error = QColor(0xff, 0x8f, 0x8a);
        tokens.warning = QColor(0xf5, 0xc5, 0x6b);
        tokens.success = QColor(0x9b, 0xe0, 0x8a);
        tokens.link = QColor(0xa8, 0xe6, 0xe8);
    } else {
        tokens.surface = QColor(0xfa, 0xfb, 0xfd);
        tokens.surfaceContainer = QColor(0xf1, 0xf4, 0xf7);
        tokens.surfaceContainerHigh = QColor(0xe7, 0xec, 0xf1);
        tokens.onSurface = QColor(0x14, 0x18, 0x1c);
        tokens.onSurfaceVariant = QColor(0x44, 0x4f, 0x5a);
        tokens.primary = QColor(0x00, 0x6a, 0x6e);
        tokens.onPrimary = QColor(0xf5, 0xff, 0xff);
        tokens.primaryContainer = QColor(0xc4, 0xec, 0xee);
        tokens.onPrimaryContainer = QColor(0x00, 0x25, 0x27);
        tokens.outline = QColor(0x6d, 0x77, 0x82);
        tokens.error = QColor(0xb3, 0x26, 0x1e);
        tokens.warning = QColor(0x7a, 0x53, 0x00);
        tokens.success = QColor(0x2e, 0x6b, 0x2f);
        tokens.link = QColor(0x00, 0x5f, 0x63);
    }
    return tokens;
}

QString toJson(const Tokens &tokens)
{
    const auto entry = [](const QColor &color) { return QJsonValue(color.name(QColor::HexArgb)); };
    QJsonObject object{
        {QStringLiteral("dark"), tokens.dark},
        {QStringLiteral("source"), tokens.sourceName},
        {QStringLiteral("surface"), entry(tokens.surface)},
        {QStringLiteral("surfaceContainer"), entry(tokens.surfaceContainer)},
        {QStringLiteral("surfaceContainerHigh"), entry(tokens.surfaceContainerHigh)},
        {QStringLiteral("onSurface"), entry(tokens.onSurface)},
        {QStringLiteral("onSurfaceVariant"), entry(tokens.onSurfaceVariant)},
        {QStringLiteral("primary"), entry(tokens.primary)},
        {QStringLiteral("onPrimary"), entry(tokens.onPrimary)},
        {QStringLiteral("primaryContainer"), entry(tokens.primaryContainer)},
        {QStringLiteral("onPrimaryContainer"), entry(tokens.onPrimaryContainer)},
        {QStringLiteral("outline"), entry(tokens.outline)},
        {QStringLiteral("error"), entry(tokens.error)},
        {QStringLiteral("warning"), entry(tokens.warning)},
        {QStringLiteral("success"), entry(tokens.success)},
        {QStringLiteral("link"), entry(tokens.link)},
    };
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
}

} // namespace theme
