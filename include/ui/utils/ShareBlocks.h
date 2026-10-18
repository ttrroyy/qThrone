#pragma once
#include <QImage>
#include <QString>

class QWidget;
class QGroupBox;

// One look for every server screen: a link, a QR code or a password in a titled block
// with a copy button; a QR code also copies as a picture.
namespace ShareBlocks {
    // The QR code of a text, black on white with a quiet zone, scaled to about side pixels.
    QImage QrImage(const QString &text, int side = 220);
    // A read-only text with "Копировать"; a secret is hidden until "Показать".
    QGroupBox *Text(QWidget *parent, const QString &title, const QString &text, bool secret = false);
    // A QR code with "Копировать QR" (as a picture).
    QGroupBox *Qr(QWidget *parent, const QString &title, const QString &text);
}
