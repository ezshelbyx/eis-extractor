#include "vault/VaultCrypto.h"

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

namespace eis {

QByteArray VaultCrypto::randomBytes(int count)
{
    if (count <= 0)
        return QByteArray();
    QByteArray bytes(count, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(bytes.data()), count) != 1)
        return QByteArray();
    return bytes;
}

bool VaultCrypto::deriveKey(const QByteArray &password, const QByteArray &salt,
                            const KdfParams &params, QByteArray *key, QString *error)
{
    if (key)
        key->clear();
    if (salt.size() < 16) {
        if (error)
            *error = QStringLiteral("vault salt is missing or too short");
        return false;
    }
    if (params.memoryKiB < 8192 || params.iterations < 1 || params.lanes < 1) {
        if (error)
            *error = QStringLiteral("invalid Argon2id parameters in vault header");
        return false;
    }

    EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
    if (!kdf) {
        if (error)
            *error = QStringLiteral("OpenSSL does not provide Argon2id");
        return false;
    }
    EVP_KDF_CTX *ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx) {
        if (error)
            *error = QStringLiteral("cannot allocate Argon2id context");
        return false;
    }

    quint32 memcost = params.memoryKiB;
    quint32 iterations = params.iterations;
    quint32 lanes = params.lanes;
    quint32 version = params.version;
    quint32 threads = params.lanes;
    const size_t keyLength = 32;
    QByteArray derived(static_cast<int>(keyLength), Qt::Uninitialized);

    QByteArray passwordCopy = password;
    QByteArray saltCopy = salt;
    OSSL_PARAM kdfParams[] = {
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD,
                                          passwordCopy.data(),
                                          static_cast<size_t>(passwordCopy.size())),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                                          saltCopy.data(),
                                          static_cast<size_t>(saltCopy.size())),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memcost),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iterations),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &lanes),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_VERSION, &version),
        OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_THREADS, &threads),
        OSSL_PARAM_construct_end()
    };

    const int rc = EVP_KDF_derive(ctx, reinterpret_cast<unsigned char *>(derived.data()),
                                  keyLength, kdfParams);
    EVP_KDF_CTX_free(ctx);
    cleanse(&passwordCopy);
    if (rc != 1) {
        cleanse(&derived);
        if (error)
            *error = QStringLiteral("Argon2id key derivation failed");
        return false;
    }

    if (key)
        *key = derived;
    cleanse(&derived);
    return true;
}

bool VaultCrypto::encrypt(const QByteArray &key, const QByteArray &nonce,
                          const QByteArray &aad, const QByteArray &plaintext,
                          QByteArray *ciphertext, QByteArray *tag, QString *error)
{
    if (key.size() != 32 || nonce.size() != 12) {
        if (error)
            *error = QStringLiteral("invalid vault key or nonce");
        return false;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        if (error)
            *error = QStringLiteral("cannot allocate cipher context");
        return false;
    }

    QByteArray output(plaintext.size() + 32, Qt::Uninitialized);
    QByteArray tagOut(16, Qt::Uninitialized);
    int length = 0;
    int total = 0;
    bool ok = true;

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1
        || EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(key.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData())) != 1) {
        ok = false;
    }
    if (ok && !aad.isEmpty()
        && EVP_EncryptUpdate(ctx, nullptr, &length,
                             reinterpret_cast<const unsigned char *>(aad.constData()),
                             aad.size()) != 1) {
        ok = false;
    }
    if (ok && !plaintext.isEmpty()
        && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(output.data()), &length,
                             reinterpret_cast<const unsigned char *>(plaintext.constData()),
                             plaintext.size()) != 1) {
        ok = false;
    }
    if (ok) {
        total = length;
        if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(output.data()) + total,
                                &length) != 1) {
            ok = false;
        } else {
            total += length;
        }
    }
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16,
                                  reinterpret_cast<unsigned char *>(tagOut.data())) != 1) {
        ok = false;
    }
    EVP_CIPHER_CTX_free(ctx);

    if (!ok) {
        if (error)
            *error = QStringLiteral("vault encryption failed");
        return false;
    }

    output.resize(total);
    if (ciphertext)
        *ciphertext = output;
    if (tag)
        *tag = tagOut;
    return true;
}

bool VaultCrypto::decrypt(const QByteArray &key, const QByteArray &nonce,
                          const QByteArray &aad, const QByteArray &ciphertext,
                          const QByteArray &tag, QByteArray *plaintext, QString *error)
{
    if (key.size() != 32 || nonce.size() != 12 || tag.size() != 16) {
        if (error)
            *error = QStringLiteral("vault file header is corrupt");
        return false;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        if (error)
            *error = QStringLiteral("cannot allocate cipher context");
        return false;
    }

    QByteArray output(ciphertext.size() + 32, Qt::Uninitialized);
    int length = 0;
    int total = 0;
    bool ok = true;

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1
        || EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                              reinterpret_cast<const unsigned char *>(key.constData()),
                              reinterpret_cast<const unsigned char *>(nonce.constData())) != 1) {
        ok = false;
    }
    if (ok && !aad.isEmpty()
        && EVP_DecryptUpdate(ctx, nullptr, &length,
                             reinterpret_cast<const unsigned char *>(aad.constData()),
                             aad.size()) != 1) {
        ok = false;
    }
    if (ok && !ciphertext.isEmpty()
        && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(output.data()), &length,
                             reinterpret_cast<const unsigned char *>(ciphertext.constData()),
                             ciphertext.size()) != 1) {
        ok = false;
    }
    if (ok) {
        total = length;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                                const_cast<unsigned char *>(
                                    reinterpret_cast<const unsigned char *>(tag.constData()))) != 1
            || EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(output.data()) + total,
                                   &length) != 1) {
            ok = false;
        } else {
            total += length;
        }
    }
    EVP_CIPHER_CTX_free(ctx);

    if (!ok) {
        if (error)
            *error = QStringLiteral("invalid master password or corrupted vault");
        return false;
    }

    output.resize(total);
    if (plaintext)
        *plaintext = output;
    cleanse(&output);
    return true;
}

void VaultCrypto::cleanse(QByteArray *buffer)
{
    if (!buffer || buffer->isEmpty())
        return;
    OPENSSL_cleanse(buffer->data(), static_cast<size_t>(buffer->size()));
    buffer->clear();
}

} // namespace eis
