#pragma once

#include <QQuickImageProvider>

// Renders the UI icon set on demand: image://bmcd-icon/<name>/<rrggbb>.
// SVG keeps the icons sharp at any scale, while the colour comes from the
// active palette token, so no per-theme assets are needed.
class IconImageProvider : public QQuickImageProvider
{
public:
    IconImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
