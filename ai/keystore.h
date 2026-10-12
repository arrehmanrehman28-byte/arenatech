#pragma once
// API key storage: environment variable KILO_API_KEY first, then a local
// secure store. On Windows the stored value is protected with DPAPI
// (CryptProtectData); elsewhere it falls back to QSettings with a logged
// warning. Keys are never written to logs.
#include <QString>

namespace arena {

class Keystore {
public:
    static QString readKey();
    static void writeKey(const QString &key);
    static void clearKey();
    static QString storagePath();
};

} // namespace arena
