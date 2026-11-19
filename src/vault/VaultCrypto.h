#pragma once

#include <QByteArray>
#include <QString>

namespace eis {

struct KdfParams {
    quint32 memoryKiB = 65536; // 64 MiB
    quint32 iterations = 3;
    quint32 lanes = 1;
    quint32 version = 0x13;    // Argon2 v1.3
};

// Thin wrapper over OpenSSL's reviewed primitives. No algorithms are
// implemented here: Argon2id comes from OpenSSL's KDF provider, encryption is
// AES-256-GCM from libcrypto.
class VaultCrypto {
public:
    static QByteArray randomBytes(int count);

    static bool deriveKey(const QByteArray &password,
                          const QByteArray &salt,
                          const KdfParams &params,
                          QByteArray *key,
                          QString *error);

    static bool encrypt(const QByteArray &key,
                        const QByteArray &nonce,
                        const QByteArray &aad,
                        const QByteArray &plaintext,
                        QByteArray *ciphertext,
                        QByteArray *tag,
                        QString *error);

    static bool decrypt(const QByteArray &key,
                        const QByteArray &nonce,
                        const QByteArray &aad,
                        const QByteArray &ciphertext,
                        const QByteArray &tag,
                        QByteArray *plaintext,
                        QString *error);

    static void cleanse(QByteArray *buffer);
};

} // namespace eis
