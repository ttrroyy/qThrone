#include "include/ui/utils/ShareBlocks.h"
#include "3rdparty/qrcodegen.hpp"
#include <QApplication>
#include <QClipboard>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace ShareBlocks {
QImage QrImage(const QString &text, int side) {
    if (text.isEmpty()) return {};
    try {
        const auto qr = qrcodegen::QrCode::encodeText(text.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        // The standard quiet zone of four modules, so phones and quirc read it reliably.
        constexpr int padding = 4;
        const int size = qr.getSize();
        QImage image(size + padding * 2, size + padding * 2, QImage::Format_RGB32);
        image.fill(qRgb(255, 255, 255));
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                if (qr.getModule(x, y)) image.setPixel(x + padding, y + padding, qRgb(0, 0, 0));
        const int scale = qMax(1, side / image.width());
        return image.scaled(image.width() * scale, image.height() * scale, Qt::KeepAspectRatio, Qt::FastTransformation);
    } catch (const std::exception &) {
        // Too long for a QR code: the text block still carries it.
        return {};
    }
}

QGroupBox *Text(QWidget *parent, const QString &title, const QString &text, bool secret) {
    auto *box = new QGroupBox(title, parent);
    auto *row = new QHBoxLayout(box);
    auto *field = new QLineEdit(text, box);
    field->setReadOnly(true);
    field->setCursorPosition(0);
    if (secret) field->setEchoMode(QLineEdit::Password);
    row->addWidget(field, 1);
    if (secret) {
        auto *show = new QPushButton(QStringLiteral("Показать"), box);
        show->setCheckable(true);
        QObject::connect(show, &QPushButton::toggled, field, [field, show](bool on) {
            field->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
            show->setText(on ? QStringLiteral("Скрыть") : QStringLiteral("Показать"));
        });
        row->addWidget(show);
    }
    auto *copy = new QPushButton(QStringLiteral("Копировать"), box);
    copy->setEnabled(!text.isEmpty());
    QObject::connect(copy, &QPushButton::clicked, field, [field] { QApplication::clipboard()->setText(field->text()); });
    row->addWidget(copy);
    return box;
}

QGroupBox *Qr(QWidget *parent, const QString &title, const QString &text) {
    auto *box = new QGroupBox(title, parent);
    auto *column = new QVBoxLayout(box);
    auto *picture = new QLabel(box);
    const auto image = QrImage(text);
    picture->setAlignment(Qt::AlignCenter);
    if (image.isNull()) picture->setText(QStringLiteral("Ссылка слишком длинная для QR-кода"));
    else picture->setPixmap(QPixmap::fromImage(image));
    column->addWidget(picture);
    auto *copy = new QPushButton(QStringLiteral("Копировать QR"), box);
    copy->setEnabled(!image.isNull());
    QObject::connect(copy, &QPushButton::clicked, box, [image] { QApplication::clipboard()->setImage(image); });
    column->addWidget(copy, 0, Qt::AlignCenter);
    return box;
}
}
