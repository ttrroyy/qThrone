#pragma once

#include <QByteArray>
#include <QString>

// Wire protocol v1, specified in spec/protocol.md of throneproj/Throne-api.
namespace RemoteApi {
    constexpr quint16 kDefaultPort = 9095;
    constexpr char kProtocolVersion = 0x01;
    constexpr int kRandomSize = 16;
    constexpr int kServerHelloSize = kRandomSize;
    constexpr int kClientHelloSize = 1 + kRandomSize;
    constexpr int kKeySize = 32;
    constexpr int kTagSize = 16;
    constexpr int kFrameHeaderSize = 4;
    constexpr quint32 kMaxClientFrame = 1u << 20;
    constexpr quint32 kMaxServerFrame = 64u << 20;

    struct SessionKeys {
        QByteArray c2s;
        QByteArray s2c;
    };

    QByteArray RandomBytes(int size);

    QByteArray GenerateKey();

    // base64url without padding. KeyFromText returns an empty array unless the trimmed text decodes to exactly kKeySize bytes.
    QString KeyToText(const QByteArray &key);

    QByteArray KeyFromText(const QString &text);

    SessionKeys DeriveKeys(const QByteArray &key, const QByteArray &serverRandom, const QByteArray &clientRandom);

    // The whole frame: the 4-byte big-endian length, then ciphertext and tag.
    QByteArray SealFrame(const QByteArray &key, quint64 counter, const QByteArray &plaintext);

    // header is the frame's 4 length bytes, sealed the rest; plaintext is untouched unless the frame authenticates.
    bool OpenFrame(const QByteArray &key, quint64 counter, const QByteArray &header, const QByteArray &sealed, QByteArray *plaintext);
}
