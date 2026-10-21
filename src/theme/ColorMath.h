#pragma once

#include <QColor>

namespace theme {

// sRGB (gamma encoded) <-> linear light.
double toLinear(double channel);
double toGamma(double channel);

// CIELAB (D65) conversions.
void rgbToLab(const QColor &color, double *l, double *a, double *b);
QColor labToRgb(double l, double a, double b);

// Cylindrical form used for tonal palettes: tone is L* (0..100), hue in
// degrees. Chroma is clipped into sRGB by bisection so a token is always a
// paintable colour.
void rgbToLch(const QColor &color, double *l, double *c, double *h);
QColor lchToRgb(double l, double c, double h);
QColor lchToRgbClipped(double l, double c, double h);

// True when every channel of `color` sits inside [0, 1].
bool isInGamut(const QColor &color);

// WCAG 2.x contrast ratio (1..21).
double contrast(const QColor &a, const QColor &b);

// Push `fg`'s lightness away from `bg` until the pair reaches `target`
// contrast. Returns the untouched colour when it already passes.
QColor ensureContrast(const QColor &fg, const QColor &bg, double target);

// Blend `a` towards `b` by `ratio` in linear light (no gamma banding).
QColor mixLinear(const QColor &a, const QColor &b, double ratio);

} // namespace theme
