#pragma once

#include <QColor>
#include <QString>

namespace theme {

// The single colour contract for the whole UI. QML must only ever read these
// tokens (via Palette), never a raw desktop colour: that keeps the 0.8 UI
// refactor from renegotiating colours with every new view.
struct Tokens
{
    bool dark = true;
    QString sourceName;

    QColor surface;
    QColor surfaceContainer;
    QColor surfaceContainerHigh;
    QColor onSurface;
    QColor onSurfaceVariant;
    QColor primary;
    QColor onPrimary;
    QColor primaryContainer;
    QColor onPrimaryContainer;
    QColor outline;
    QColor error;
    QColor warning;
    QColor success;
    QColor link;
};

// Derive a full token set from one accent colour. Scheme B: the user's accent
// colour drives hue/chroma, tone comes from the Material-like tonal ladder.
// Contrast is enforced afterwards (surface text >= 7:1, muted >= 4.5:1,
// accents >= 3:1) so an unfortunate accent can never produce unreadable text.
Tokens buildTokens(const QColor &accent, bool dark);

// Palette A: hand picked two-level fallback, used when no accent is available
// or when a token set fails the contrast gate twice.
Tokens fallbackTokens(bool dark);

// Apply the contrast gate in place; returns false when `tokens` still has a
// failing pair after adjustment (caller should fall back to `fallbackTokens`).
bool enforceContrast(Tokens &tokens);

// Serialise for tests / debugging (`bmcd --tokens --dark`).
QString toJson(const Tokens &tokens);

} // namespace theme
