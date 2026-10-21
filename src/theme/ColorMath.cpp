#include "theme/ColorMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace theme {
namespace {

constexpr double kWhiteX = 0.95047;
constexpr double kWhiteY = 1.0;
constexpr double kWhiteZ = 1.08883;
constexpr double kEpsilon = 216.0 / 24389.0;   // 0.008856
constexpr double kKappa = 24389.0 / 27.0;      // 903.3

double labF(double t)
{
    return t > kEpsilon ? std::cbrt(t) : (kKappa * t + 16.0) / 116.0;
}

double labFInv(double t)
{
    const double t3 = t * t * t;
    return t3 > kEpsilon ? t3 : (116.0 * t - 16.0) / kKappa;
}

} // namespace

double toLinear(double channel)
{
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double toGamma(double channel)
{
    return channel <= 0.0031308 ? 12.92 * channel : 1.055 * std::pow(channel, 1.0 / 2.4) - 0.055;
}

void rgbToLab(const QColor &color, double *l, double *a, double *b)
{
    const double r = toLinear(color.redF());
    const double g = toLinear(color.greenF());
    const double bl = toLinear(color.blueF());

    const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * bl) / kWhiteX;
    const double y = (0.2126729 * r + 0.7151522 * g + 0.0721750 * bl) / kWhiteY;
    const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * bl) / kWhiteZ;

    const double fx = labF(x);
    const double fy = labF(y);
    const double fz = labF(z);

    *l = 116.0 * fy - 16.0;
    *a = 500.0 * (fx - fy);
    *b = 200.0 * (fy - fz);
}

QColor labToRgb(double l, double a, double b)
{
    const double fy = (l + 16.0) / 116.0;
    const double fx = fy + a / 500.0;
    const double fz = fy - b / 200.0;

    const double x = kWhiteX * labFInv(fx);
    const double y = kWhiteY * labFInv(fy);
    const double z = kWhiteZ * labFInv(fz);

    const double r = 3.2404542 * x - 1.5371385 * y - 0.4985314 * z;
    const double g = -0.9692660 * x + 1.8760108 * y + 0.0415560 * z;
    const double bl = 0.0556434 * x - 0.2040259 * y + 1.0572252 * z;

    return QColor::fromRgbF(std::clamp(toGamma(r), 0.0, 1.0),
                            std::clamp(toGamma(g), 0.0, 1.0),
                            std::clamp(toGamma(bl), 0.0, 1.0));
}

void rgbToLch(const QColor &color, double *l, double *c, double *h)
{
    double a = 0.0;
    double b = 0.0;
    rgbToLab(color, l, &a, &b);
    *c = std::hypot(a, b);
    *h = std::fmod(std::atan2(b, a) * 180.0 / std::numbers::pi + 360.0, 360.0);
}

QColor lchToRgb(double l, double c, double h)
{
    const double rad = h * std::numbers::pi / 180.0;
    return labToRgb(l, c * std::cos(rad), c * std::sin(rad));
}

QColor lchToRgbClipped(double l, double c, double h)
{
    l = std::clamp(l, 0.0, 100.0);
    c = std::max(0.0, c);

    const QColor direct = lchToRgb(l, c, h);
    if (direct.isValid() && isInGamut(lchToRgb(l, c, h)))
        return direct;

    // Bisect on chroma; 12 iterations get well below one 8-bit step.
    double lo = 0.0;
    double hi = c;
    for (int i = 0; i < 24; ++i) {
        const double mid = (lo + hi) / 2.0;
        if (isInGamut(lchToRgb(l, mid, h)))
            lo = mid;
        else
            hi = mid;
    }
    return lchToRgb(l, lo, h);
}

bool isInGamut(const QColor &color)
{
    if (!color.isValid())
        return false;
    const double r = color.redF();
    const double g = color.greenF();
    const double b = color.blueF();
    const double eps = 1e-6;
    return r > -eps && r < 1.0 + eps && g > -eps && g < 1.0 + eps && b > -eps && b < 1.0 + eps;
}

double contrast(const QColor &a, const QColor &b)
{
    const auto luminance = [](const QColor &color) {
        return 0.2126 * toLinear(color.redF()) + 0.7152 * toLinear(color.greenF())
               + 0.0722 * toLinear(color.blueF());
    };
    const double la = luminance(a);
    const double lb = luminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

QColor ensureContrast(const QColor &fg, const QColor &bg, double target)
{
    if (contrast(fg, bg) >= target)
        return fg;

    double l = 0.0;
    double c = 0.0;
    double h = 0.0;
    rgbToLch(fg, &l, &c, &h);
    double bgL = 0.0;
    double bgC = 0.0;
    double bgH = 0.0;
    rgbToLch(bg, &bgL, &bgC, &bgH);

    const bool lighten = l < bgL;
    for (int step = 1; step <= 100; ++step) {
        const double candidateL = std::clamp(lighten ? l + step : l - step, 0.0, 100.0);
        const QColor candidate = lchToRgbClipped(candidateL, c, h);
        if (contrast(candidate, bg) >= target)
            return candidate;
        if ((lighten && candidateL >= 100.0) || (!lighten && candidateL <= 0.0))
            break;
    }
    return contrast(QColor(255, 255, 255), bg) > contrast(QColor(0, 0, 0), bg) ? QColor(255, 255, 255)
                                                                              : QColor(0, 0, 0);
}

QColor mixLinear(const QColor &a, const QColor &b, double ratio)
{
    ratio = std::clamp(ratio, 0.0, 1.0);
    const auto blend = [ratio](double x, double y) {
        return toGamma(toLinear(x) * (1.0 - ratio) + toLinear(y) * ratio);
    };
    return QColor::fromRgbF(blend(a.redF(), b.redF()), blend(a.greenF(), b.greenF()),
                            blend(a.blueF(), b.blueF()));
}

} // namespace theme
