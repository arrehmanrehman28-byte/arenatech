#include "keystore.h"
#include "logger.h"
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace arena {

namespace {
QString storeFile()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("credentials.key"));
}

#ifdef Q_OS_WIN
QByteArray protect(const QByteArray &plain)
{
    DATA_BLOB in{};
    in.pbData = const_cast<BYTE *>(reinterpret_cast<const BYTE *>(plain.constData()));
    in.cbData = static_cast<DWORD>(plain.size());
    DATA_BLOB out{};
    if (CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        QByteArray protectedBytes(reinterpret_cast<const char *>(out.pbData), out.cbData);
        LocalFree(out.pbData);
        return protectedBytes;
    }
    return QByteArray();
}

QByteArray unprotect(const QByteArray &blob)
{
    DATA_BLOB in{};
    in.pbData = const_cast<BYTE *>(reinterpret_cast<const BYTE *>(blob.constData()));
    in.cbData = static_cast<DWORD>(blob.size());
    DATA_BLOB out{};
    if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        QByteArray plain(reinterpret_cast<const char *>(out.pbData), out.cbData);
        LocalFree(out.pbData);
        return plain;
    }
    return QByteArray();
}
#endif
} // namespace

QString Keystore::storagePath() { return storeFile(); }

QString Keystore::readKey()
{
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("KILO_API_KEY"));
    if (!env.isEmpty()) return env;
    QFile f(storeFile());
    if (!f.open(QIODevice::ReadOnly)) return QString();
    const QByteArray blob = f.readAll().trimmed();
    if (blob.isEmpty()) return QString();
#ifdef Q_OS_WIN
    const QByteArray plain = unprotect(blob);
    return plain.isEmpty() ? QString() : QString::fromUtf8(plain);
#else
    return QString::fromUtf8(blob);
#endif
}

void Keystore::writeKey(const QString &key)
{
    QDir().mkpath(QFileInfo(storeFile()).absolutePath());
    QFile f(storeFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logError(QStringLiteral("Could not write key store"), storeFile());
        return;
    }
#ifdef Q_OS_WIN
    const QByteArray plain = key.toUtf8();
    const QByteArray protectedBytes = protect(plain);
    if (!protectedBytes.isEmpty()) {
        f.write(protectedBytes);
        return;
    }
    logWarn(QStringLiteral("DPAPI protection failed; storing key unencrypted"));
#endif
    f.write(key.toUtf8());
}

void Keystore::clearKey()
{
    QFile::remove(storeFile());
}

} // namespace arena
