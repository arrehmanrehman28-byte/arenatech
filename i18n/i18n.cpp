#include "i18n.h"
#include "logger.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace arena {

I18n &I18n::instance()
{
    static I18n i;
    return i;
}

bool I18n::loadDirectory(const QString &dir, QString *error)
{
    QDir d(dir);
    if (!d.exists()) {
        if (error) *error = QStringLiteral("i18n directory not found: %1").arg(dir);
        return false;
    }
    QHash<QString, Loaded> found;
    for (const QString &f : d.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile file(d.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        QJsonParseError pe{};
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            logWarn(QStringLiteral("Skipping invalid translation file: %1").arg(f));
            continue;
        }
        const QJsonObject o = doc.object();
        Loaded l;
        const QJsonObject meta = o.value(QStringLiteral("meta")).toObject();
        l.info.bcp47 = meta.value(QStringLiteral("bcp47")).toString(f.left(f.size() - 5));
        l.info.nativeName = meta.value(QStringLiteral("nativeName")).toString(l.info.bcp47);
        l.info.dir = meta.value(QStringLiteral("dir")).toString(QStringLiteral("ltr"));
        const QJsonObject strings = o.value(QStringLiteral("strings")).toObject();
        for (auto it = strings.constBegin(); it != strings.constEnd(); ++it)
            l.strings.insert(it.key(), it.value().toString());
        found.insert(l.info.bcp47, std::move(l));
    }
    if (found.isEmpty()) {
        if (error) *error = QStringLiteral("No translation files found in %1").arg(dir);
        return false;
    }
    m_locales = std::move(found);
    if (!m_locales.contains(m_current)) m_current = QStringLiteral("en");
    return true;
}

QList<I18n::LocaleInfo> I18n::locales() const
{
    QList<LocaleInfo> out;
    for (const auto &l : std::as_const(m_locales)) out.append(l.info);
    std::sort(out.begin(), out.end(), [](const LocaleInfo &a, const LocaleInfo &b) {
        return a.nativeName.localeAwareCompare(b.nativeName) < 0;
    });
    return out;
}

QString I18n::currentLocale() const { return m_current; }

bool I18n::isRtl() const
{
    auto it = m_locales.constFind(m_current);
    if (it != m_locales.constEnd())
        return it->info.dir == QLatin1String("rtl");
    return false; // conservative fallback
}

void I18n::setLocale(const QString &bcp47)
{
    if (!m_locales.contains(bcp47)) return;
    if (bcp47 == m_current) return;
    m_current = bcp47;
    emit localeChanged(m_current);
}

QString I18n::translate(const QString &key, const QVariantMap &args) const
{
    QString result;
    if (const auto it = m_locales.constFind(m_current); it != m_locales.constEnd()) {
        if (const auto s = it->strings.constFind(key); s != it->strings.constEnd())
            result = s.value();
    }
    if (result.isEmpty()) {
        if (const auto it = m_locales.constFind(QStringLiteral("en")); it != m_locales.constEnd())
            result = it->strings.value(key, QString());
    }
    if (result.isEmpty()) {
        if (qEnvironmentVariableIntValue("ARENA_DEVELOPER") > 0)
            logWarn(QStringLiteral("Missing translation key: %1").arg(key));
        return key;
    }
    // Substitute {placeholders} only — translated fragments are never concatenated.
    for (auto it = args.constBegin(); it != args.constEnd(); ++it)
        result.replace('{' + it.key() + '}', it.value().toString());
    return result;
}

QString arenaTr(const QString &key, const QVariantMap &args)
{
    return I18n::instance().translate(key, args);
}

} // namespace arena
