#include "include/api/remote/Protocol.hpp"

#include <QList>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QtEndian>

#include "3rdparty/monocypher/monocypher.h"

namespace RemoteApi {
    QByteArray RandomBytes(int size) {
        if (size <= 0) return {};
        QList<quint32> words((size + 3) / 4);
        QRandomGenerator::system()->fillRange(words.data(), words.size());
        QByteArray bytes(reinterpret_cast<const char *>(words.constData()), size);
        crypto_wipe(words.data(), words.size() * sizeof(quint32));
        return bytes;
    }

    QByteArray GenerateKey() {
        return RandomBytes(kKeySize);
    }

    QString KeyToText(const QByteArray &key) {
        return QString::fromLatin1(key.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    }

    QByteArray KeyFromText(const QString &text) {
        const auto trimmed = text.trimmed();
        // AbortOnBase64DecodingErrors still accepts '=' padding, which the text form never has.
        if (trimmed.contains(QLatin1Char('='))) return {};
        const auto result = QByteArray::fromBase64Encoding(trimmed.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
        if (!result || result.decoded.size() != kKeySize) return {};
        return result.decoded;
    }

    SessionKeys DeriveKeys(const QByteArray &key, const QByteArray &serverRandom, const QByteArray &clientRandom) {
        const auto randoms = serverRandom + clientRandom;
        return {
            QMessageAuthenticationCode::hash(QByteArrayLiteral("rawline-api/1 c2s") + randoms, key, QCryptographicHash::Sha256),
            QMessageAuthenticationCode::hash(QByteArrayLiteral("rawline-api/1 s2c") + randoms, key, QCryptographicHash::Sha256),
        };
    }

    // Each frame gets a fresh context: Monocypher re-keys a context after every message, which RFC 8439 does not.
    QByteArray SealFrame(const QByteArray &key, quint64 counter, const QByteArray &plaintext) {
        if (key.size() != kKeySize) return {};
        const auto size = plaintext.size();
        QByteArray frame(kFrameHeaderSize + size + kTagSize, Qt::Uninitialized);
        auto *out = reinterpret_cast<uint8_t *>(frame.data());
        qToBigEndian(quint32(size + kTagSize), out);

        uint8_t nonce[12] = {};
        qToBigEndian(counter, nonce + 4);
        crypto_aead_ctx ctx;
        crypto_aead_init_ietf(&ctx, reinterpret_cast<const uint8_t *>(key.constData()), nonce);
        crypto_aead_write(&ctx, out + kFrameHeaderSize, out + kFrameHeaderSize + size, out, kFrameHeaderSize,
                          reinterpret_cast<const uint8_t *>(plaintext.constData()), size_t(size));
        crypto_wipe(&ctx, sizeof ctx);
        return frame;
    }

    bool OpenFrame(const QByteArray &key, quint64 counter, const QByteArray &header, const QByteArray &sealed, QByteArray *plaintext) {
        if (key.size() != kKeySize || header.size() != kFrameHeaderSize || sealed.size() < kTagSize) return false;
        if (qFromBigEndian<quint32>(header.constData()) != quint32(sealed.size())) return false;
        const auto size = sealed.size() - kTagSize;
        const auto *in = reinterpret_cast<const uint8_t *>(sealed.constData());
        QByteArray opened(size, Qt::Uninitialized);

        uint8_t nonce[12] = {};
        qToBigEndian(counter, nonce + 4);
        crypto_aead_ctx ctx;
        crypto_aead_init_ietf(&ctx, reinterpret_cast<const uint8_t *>(key.constData()), nonce);
        const int mismatch = crypto_aead_read(&ctx, reinterpret_cast<uint8_t *>(opened.data()), in + size,
                                              reinterpret_cast<const uint8_t *>(header.constData()), kFrameHeaderSize, in, size_t(size));
        crypto_wipe(&ctx, sizeof ctx);
        if (mismatch != 0) return false;
        *plaintext = opened;
        return true;
    }
}
