#pragma once
// Translation layer. Every user-facing string goes through arenaTr();
// fragments are never concatenated. Placeholders use {name} form.
// Languages are drop-in: add <bcp47>.json to this directory and it appears
// in the picker automatically (see README.md).
#include <QObject>
#include <QString>
#include <QHash>
#include <QVariantMap>
#include <QList>

namespace arena {

class I18n : public QObject {
    Q_OBJECT
public:
    struct LocaleInfo {
        QString bcp47;
        QString nativeName;
        QString dir; // "ltr" | "rtl"
    };

    static I18n &instance();

    bool loadDirectory(const QString &dir, QString *error = nullptr);
    QList<LocaleInfo> locales() const;
    QString currentLocale() const;
    bool isRtl() const;
    void setLocale(const QString &bcp47);
    // Lookup: current locale -> English fallback -> key itself.
    // NOTE: not named `tr` — Q_OBJECT injects a static tr(const char*)
    // member into every QObject subclass, which would hijack string-literal
    // calls (verified against Qt 6.8).
    QString translate(const QString &key, const QVariantMap &args = {}) const;

signals:
    void localeChanged(const QString &bcp47);

private:
    I18n() = default;
    struct Loaded {
        LocaleInfo info;
        QHash<QString, QString> strings;
    };
    QHash<QString, Loaded> m_locales;
    QString m_current = QStringLiteral("en");
};

// Convenience free function; defined in i18n.cpp.
QString arenaTr(const QString &key, const QVariantMap &args = {});

} // namespace arena
