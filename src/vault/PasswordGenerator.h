#pragma once

#include <QString>

namespace eis {

struct GeneratorOptions {
    int length = 20;
    bool uppercase = true;
    bool lowercase = true;
    bool digits = true;
    bool symbols = true;
    bool excludeAmbiguous = true; // O0Il1 and similar look-alike characters
};

class PasswordGenerator {
public:
    // Cryptographically secure (OpenSSL RAND_bytes). Guarantees at least one
    // character from every enabled class when the length allows it.
    static QString generate(const GeneratorOptions &options, QString *error);

    // Entropy estimate in bits based on the character pool actually used.
    static double estimatedBits(const QString &password);
    static QString strengthLabel(double bits);
    static int strengthPercent(double bits);
};

} // namespace eis
