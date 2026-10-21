#include <QCoreApplication>
#include <QImage>
#include <QTextStream>
#include <QSet>
#include <QFile>

// Checks the offscreen screenshot produced by `bmcd`: fixed size,
// palette background, a minimum number of distinct colours and no Qt warnings
// recorded in the log file next to the image.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        out << "usage: ui_check <screenshot.png>\n";
        return 2;
    }

    QImage image(args.at(1));
    if (image.isNull()) {
        out << "FAIL: cannot read " << args.at(1) << "\n";
        return 1;
    }
    image = image.convertToFormat(QImage::Format_RGB32);

    int failures = 0;
    auto check = [&](bool ok, const QString &what) {
        out << (ok ? "ok   " : "FAIL ") << what << "\n";
        if (!ok)
            ++failures;
    };

    check(image.width() == 1120 && image.height() == 760,
          QStringLiteral("size 1120x760 (got %1x%2)").arg(image.width()).arg(image.height()));

    const QColor corner = image.pixelColor(4, 300);
    const int delta = qAbs(corner.red() - 14) + qAbs(corner.green() - 20) + qAbs(corner.blue() - 21);
    check(delta <= 24, QStringLiteral("background near #0E1415 (got %1,%2,%3)")
                            .arg(corner.red()).arg(corner.green()).arg(corner.blue()));

    QSet<QRgb> colours;
    for (int y = 0; y < image.height(); y += 3)
        for (int x = 0; x < image.width(); x += 3)
            colours.insert(image.pixel(x, y));
    check(colours.size() > 200, QStringLiteral("at least 201 distinct colours (got %1)").arg(colours.size()));

    if (args.size() > 2) {
        QFile log(args.at(2));
        if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString text = QString::fromUtf8(log.readAll());
            const QStringList bad = {QStringLiteral("ReferenceError"), QStringLiteral("TypeError"),
                                     QStringLiteral("Unable to assign"), QStringLiteral("is not defined")};
            for (const QString &needle : bad)
                check(!text.contains(needle), QStringLiteral("log free of \"%1\"").arg(needle));
        }
    }

    out << (failures == 0 ? "PASS\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
