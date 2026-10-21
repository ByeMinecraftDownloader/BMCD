#include <QtTest>

#include <cmath>

#include "theme/ColorMath.h"
#include "theme/ThemeSource.h"
#include "theme/TokenBuilder.h"

using namespace theme;

class TestTheme : public QObject
{
    Q_OBJECT

private slots:
    void labRoundTrip();
    void lchRoundTrip();
    void contrastKnownValues();
    void contrastGateAlwaysPasses();
    void accentDrivesPrimaryHue();
    void fallbackIsConsistent();
    void tokensSerialise();
};

void TestTheme::labRoundTrip()
{
    const QList<QColor> colors{QColor(0, 0, 0), QColor(255, 255, 255), QColor(128, 212, 215),
                               QColor(255, 0, 0), QColor(0, 128, 64), QColor(20, 20, 90)};
    for (const QColor &color : colors) {
        double l = 0, a = 0, b = 0;
        rgbToLab(color, &l, &a, &b);
        const QColor back = labToRgb(l, a, b);
        QVERIFY2(std::abs(back.red() - color.red()) <= 2, qPrintable(color.name()));
        QVERIFY2(std::abs(back.green() - color.green()) <= 2, qPrintable(color.name()));
        QVERIFY2(std::abs(back.blue() - color.blue()) <= 2, qPrintable(color.name()));
    }
}

void TestTheme::lchRoundTrip()
{
    const QColor color(80, 200, 120);
    double l = 0, c = 0, h = 0;
    rgbToLch(color, &l, &c, &h);
    QVERIFY(l > 0 && l < 100);
    QVERIFY(c > 0);
    QVERIFY(h >= 0 && h < 360);
    const QColor back = lchToRgbClipped(l, c, h);
    QVERIFY(std::abs(back.green() - color.green()) <= 3);
}

void TestTheme::contrastKnownValues()
{
    QVERIFY(std::abs(contrast(QColor(255, 255, 255), QColor(0, 0, 0)) - 21.0) < 0.01);
    QCOMPARE(contrast(QColor(120, 120, 120), QColor(120, 120, 120)), 1.0);
}

void TestTheme::contrastGateAlwaysPasses()
{
    // Every 30 degrees of hue, plus grey and near-black accents: the gate must
    // always produce a readable token set.
    for (int hue = 0; hue < 360; hue += 30) {
        for (bool dark : {true, false}) {
            Tokens tokens = buildTokens(lchToRgbClipped(55.0, 60.0, hue), dark);
            QVERIFY(enforceContrast(tokens));
            QVERIFY(contrast(tokens.onSurface, tokens.surface) >= 7.0);
            QVERIFY(contrast(tokens.onSurfaceVariant, tokens.surface) >= 4.5);
            QVERIFY(contrast(tokens.primary, tokens.surface) >= 3.0);
            QVERIFY(contrast(tokens.error, tokens.surface) >= 3.0);
        }
    }
    Tokens grey = buildTokens(QColor(128, 128, 128), true);
    QVERIFY(enforceContrast(grey));
}

void TestTheme::accentDrivesPrimaryHue()
{
    const QColor accent(200, 60, 60);
    const Tokens tokens = buildTokens(accent, true);
    double l = 0, c = 0, h = 0, sourceL = 0, sourceC = 0, sourceH = 0;
    rgbToLch(tokens.primary, &l, &c, &h);
    rgbToLch(accent, &sourceL, &sourceC, &sourceH);
    double delta = std::abs(h - sourceH);
    if (delta > 180)
        delta = 360 - delta;
    QVERIFY2(delta < 40, qPrintable(QStringLiteral("hue %1 vs %2").arg(h).arg(sourceH)));
}

void TestTheme::fallbackIsConsistent()
{
    for (bool dark : {true, false}) {
        Tokens tokens = fallbackTokens(dark);
        QVERIFY(enforceContrast(tokens));
        QVERIFY(tokens.sourceName == QStringLiteral("fallback"));
    }
}

void TestTheme::tokensSerialise()
{
    Tokens tokens = buildTokens(QColor(128, 212, 215), true);
    const QString json = toJson(tokens);
    QVERIFY(json.contains(QStringLiteral("\"surface\"")));
    QVERIFY(json.contains(QStringLiteral("\"dark\"")));
}

QTEST_GUILESS_MAIN(TestTheme)
#include "tst_theme.moc"
