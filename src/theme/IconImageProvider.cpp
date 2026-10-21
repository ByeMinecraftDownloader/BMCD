#include "theme/IconImageProvider.h"

#include <QPainter>
#include <QSvgRenderer>

namespace {

const QHash<QString, QString> &iconPaths()
{
    static const QHash<QString, QString> paths = {
        {QStringLiteral("versions"), QStringLiteral("<path d=\"M4 6h16M4 12h16M4 18h10\"/>")},
        {QStringLiteral("queue"), QStringLiteral("<path d=\"M4 6h10M4 12h10M4 18h6\"/><path d=\"M16 9l5 3-5 3z\"/>")},
        {QStringLiteral("serve"), QStringLiteral("<path d=\"M12 16V4M7 9l5-5 5 5M4 16v4h16v-4\"/>")},
        {QStringLiteral("export"), QStringLiteral("<path d=\"M12 16V4M7 9l5-5 5 5M4 16v4h16v-4\"/>")},
        {QStringLiteral("settings"), QStringLiteral("<path d=\"M4 7h16M4 12h16M4 17h16\"/><circle cx=\"9\" cy=\"7\" r=\"2\"/><circle cx=\"15\" cy=\"12\" r=\"2\"/><circle cx=\"8\" cy=\"17\" r=\"2\"/>")},
        {QStringLiteral("minimize"), QStringLiteral("<path d=\"M5 12h14\"/>")},
        {QStringLiteral("maximize"), QStringLiteral("<rect x=\"5\" y=\"5\" width=\"14\" height=\"14\" rx=\"1\"/>")},
        {QStringLiteral("restore"), QStringLiteral("<rect x=\"8\" y=\"8\" width=\"11\" height=\"11\" rx=\"1\"/><path d=\"M5 16V5h11\"/>")},
        {QStringLiteral("close"), QStringLiteral("<path d=\"M6 6l12 12M18 6L6 18\"/>")},
        {QStringLiteral("refresh"), QStringLiteral("<path d=\"M20 12a8 8 0 1 1-2.3-5.7\"/><path d=\"M20 4v5h-5\"/>")},
        {QStringLiteral("download"), QStringLiteral("<path d=\"M12 4v12M7 11l5 5 5-5M4 20h16\"/>")},
        {QStringLiteral("play"), QStringLiteral("<path d=\"M8 5l11 7-11 7z\"/>")},
        {QStringLiteral("pause"), QStringLiteral("<path d=\"M8 5v14M16 5v14\"/>")},
        {QStringLiteral("import"), QStringLiteral("<path d=\"M12 4v11M7 10l5 5 5-5M4 20h16\"/>")},
        {QStringLiteral("folder"), QStringLiteral("<path d=\"M3 7h6l2 2h10v10H3z\"/>")},
        {QStringLiteral("search"), QStringLiteral("<circle cx=\"10.5\" cy=\"10.5\" r=\"6\"/><path d=\"M15 15l5 5\"/>")},
        {QStringLiteral("check"), QStringLiteral("<path d=\"M5 12l5 5L20 6\"/>")},
        {QStringLiteral("warning"), QStringLiteral("<path d=\"M12 3l9 17H3z\"/><path d=\"M12 9v5\"/><circle cx=\"12\" cy=\"17\" r=\"0.8\" fill=\"COLOR\" stroke=\"none\"/>")},
        {QStringLiteral("info"), QStringLiteral("<circle cx=\"12\" cy=\"12\" r=\"9\"/><path d=\"M12 11v6\"/><circle cx=\"12\" cy=\"7.5\" r=\"0.8\" fill=\"COLOR\" stroke=\"none\"/>")},
        {QStringLiteral("chevron"), QStringLiteral("<path d=\"M8 10l4 4 4-4\"/>")},
    };
    return paths;
}

} // namespace

IconImageProvider::IconImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage IconImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int slash = id.indexOf(QLatin1Char('/'));
    const QString name = slash >= 0 ? id.left(slash) : id;
    const QString color = slash >= 0 ? id.mid(slash + 1) : QStringLiteral("c9f2f4");

    const int edge = requestedSize.isValid() && requestedSize.width() > 0
                             ? qBound(8, requestedSize.width(), 256)
                             : 24;
    if (size)
        *size = QSize(edge, edge);

    const QString body = iconPaths().value(name);
    if (body.isEmpty())
        return {};

    QString svgBody = body;
    svgBody.replace(QStringLiteral("COLOR"), QLatin1Char('#') + color);
    const QString svg = QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" "
                                       "width=\"24\" height=\"24\" fill=\"none\" stroke=\"#%1\" "
                                       "stroke-width=\"1.8\" stroke-linecap=\"round\" stroke-linejoin=\"round\">%2</svg>")
                                .arg(color, svgBody);

    QSvgRenderer renderer(svg.toUtf8());
    QImage image(edge, edge, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter, QRectF(0, 0, edge, edge));
    return image;
}
