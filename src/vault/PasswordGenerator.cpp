#include "vault/PasswordGenerator.h"

#include "vault/VaultCrypto.h"

#include <QSet>

#include <cmath>
#include <openssl/rand.h>

namespace eis {

namespace {

const char *kUppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
const char *kLowercase = "abcdefghijklmnopqrstuvwxyz";
const char *kDigits = "0123456789";
const char *kSymbols = "!@#$%^&*_-+=:,.?~";
const char *kAmbiguous = "O0Il1|`'\"{}[]()/\\";

QString filtered(const char *set, bool excludeAmbiguous)
{
    QString result;
    const QString ambiguous = QString::fromLatin1(kAmbiguous);
    for (const char *p = set; *p; ++p) {
        const QChar ch = QChar::fromLatin1(*p);
        if (excludeAmbiguous && ambiguous.contains(ch))
            continue;
        result.append(ch);
    }
    return result;
}

int secureIndex(int bound)
{
    if (bound <= 0)
        return 0;
    // Rejection sampling removes modulo bias.
    const quint32 limit = 0xFFFFFFFFu - (0xFFFFFFFFu % static_cast<quint32>(bound));
    for (int attempt = 0; attempt < 128; ++attempt) {
        unsigned char bytes[4];
        if (RAND_bytes(bytes, 4) != 1)
            return -1;
        const quint32 value = (static_cast<quint32>(bytes[0]) << 24)
                            | (static_cast<quint32>(bytes[1]) << 16)
                            | (static_cast<quint32>(bytes[2]) << 8)
                            | static_cast<quint32>(bytes[3]);
        if (value < limit)
            return static_cast<int>(value % static_cast<quint32>(bound));
    }
    return -1;
}

void secureShuffle(QString *text)
{
    QChar *data = text->data();
    for (int i = text->size() - 1; i > 0; --i) {
        const int j = secureIndex(i + 1);
        if (j < 0)
            return;
        const QChar temporary = data[i];
        data[i] = data[j];
        data[j] = temporary;
    }
}

} // namespace

QString PasswordGenerator::generate(const GeneratorOptions &options, QString *error)
{
    if (options.length < 4 || options.length > 256) {
        if (error)
            *error = QStringLiteral("password length must be between 4 and 256");
        return QString();
    }

    QStringList classes;
    if (options.uppercase)
        classes << filtered(kUppercase, options.excludeAmbiguous);
    if (options.lowercase)
        classes << filtered(kLowercase, options.excludeAmbiguous);
    if (options.digits)
        classes << filtered(kDigits, options.excludeAmbiguous);
    if (options.symbols)
        classes << filtered(kSymbols, options.excludeAmbiguous);

    classes.removeAll(QString());
    if (classes.isEmpty()) {
        if (error)
            *error = QStringLiteral("select at least one character class");
        return QString();
    }

    QString pool;
    for (const QString &c : classes)
        pool += c;

    if (options.length < classes.size()) {
        if (error)
            *error = QStringLiteral("length is too short for the selected character classes");
        return QString();
    }

    QString password;
    password.reserve(options.length);

    // One guaranteed character per selected class.
    for (const QString &c : classes) {
        const int index = secureIndex(c.size());
        if (index < 0) {
            if (error)
                *error = QStringLiteral("secure random generator unavailable");
            return QString();
        }
        password.append(c.at(index));
    }
    while (password.size() < options.length) {
        const int index = secureIndex(pool.size());
        if (index < 0) {
            if (error)
                *error = QStringLiteral("secure random generator unavailable");
            return QString();
        }
        password.append(pool.at(index));
    }
    secureShuffle(&password);
    return password;
}

double PasswordGenerator::estimatedBits(const QString &password)
{
    if (password.isEmpty())
        return 0.0;

    bool upper = false, lower = false, digit = false, symbol = false, other = false;
    QSet<QChar> unique;
    for (const QChar ch : password) {
        unique.insert(ch);
        if (ch.isUpper())
            upper = true;
        else if (ch.isLower())
            lower = true;
        else if (ch.isDigit())
            digit = true;
        else if (ch.isPrint())
            symbol = true;
        else
            other = true;
    }

    int pool = 0;
    if (upper)
        pool += 26;
    if (lower)
        pool += 26;
    if (digit)
        pool += 10;
    if (symbol)
        pool += 33;
    if (other)
        pool += 100;

    if (pool <= 0)
        return 0.0;

    double bits = password.size() * std::log2(static_cast<double>(pool));
    // Repeated characters and very short passwords reduce practical entropy.
    if (unique.size() < password.size())
        bits *= 0.7 + 0.3 * (static_cast<double>(unique.size()) / password.size());
    if (password.size() < 8)
        bits *= 0.5;
    return bits;
}

QString PasswordGenerator::strengthLabel(double bits)
{
    if (bits < 40.0)
        return QStringLiteral("Weak");
    if (bits < 60.0)
        return QStringLiteral("Fair");
    if (bits < 80.0)
        return QStringLiteral("Strong");
    if (bits < 100.0)
        return QStringLiteral("Very strong");
    return QStringLiteral("Excellent");
}

int PasswordGenerator::strengthPercent(double bits)
{
    const int percent = static_cast<int>(std::round(bits));
    return qBound(0, percent, 100);
}

} // namespace eis
