#include "settings.h"

namespace arena {

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

Settings::Settings()
    : m_qs(QSettings::IniFormat, QSettings::UserScope,
           QStringLiteral("ArenaEdit"), QStringLiteral("ArenaEdit"))
{
}

QString Settings::language() const { return m_qs.value(QStringLiteral("general/language"), QStringLiteral("en")).toString(); }
void Settings::setLanguage(const QString &b) { m_qs.setValue(QStringLiteral("general/language"), b); emit changed(QStringLiteral("general/language")); }

bool Settings::firstRunDone() const { return m_qs.value(QStringLiteral("general/firstRunDone"), false).toBool(); }
void Settings::setFirstRunDone() { m_qs.setValue(QStringLiteral("general/firstRunDone"), true); emit changed(QStringLiteral("general/firstRunDone")); }

QString Settings::theme() const { return m_qs.value(QStringLiteral("appearance/theme"), QStringLiteral("dark")).toString(); }
void Settings::setTheme(const QString &t) { m_qs.setValue(QStringLiteral("appearance/theme"), t); emit changed(QStringLiteral("appearance/theme")); }

QColor Settings::accentColor() const { return m_qs.value(QStringLiteral("appearance/accent"), QColor(0x4C, 0x8D, 0xFF)).value<QColor>(); }
void Settings::setAccentColor(const QColor &c) { m_qs.setValue(QStringLiteral("appearance/accent"), c.name(QColor::HexRgb)); emit changed(QStringLiteral("appearance/accent")); }

int Settings::fontScale() const { return m_qs.value(QStringLiteral("appearance/fontScale"), 1).toInt(); }
void Settings::setFontScale(int s) { m_qs.setValue(QStringLiteral("appearance/fontScale"), s); emit changed(QStringLiteral("appearance/fontScale")); }

bool Settings::reduceMotion() const { return m_qs.value(QStringLiteral("appearance/reduceMotion"), false).toBool(); }
void Settings::setReduceMotion(bool on) { m_qs.setValue(QStringLiteral("appearance/reduceMotion"), on); emit changed(QStringLiteral("appearance/reduceMotion")); }

bool Settings::snapEnabled() const { return m_qs.value(QStringLiteral("editing/snap"), true).toBool(); }
void Settings::setSnapEnabled(bool on) { m_qs.setValue(QStringLiteral("editing/snap"), on); emit changed(QStringLiteral("editing/snap")); }

int Settings::autosaveSeconds() const { return m_qs.value(QStringLiteral("editing/autosaveSeconds"), 60).toInt(); }
void Settings::setAutosaveSeconds(int s) { m_qs.setValue(QStringLiteral("editing/autosaveSeconds"), s); emit changed(QStringLiteral("editing/autosaveSeconds")); }

bool Settings::proxyEnabled() const { return m_qs.value(QStringLiteral("editing/proxyEnabled"), true).toBool(); }
void Settings::setProxyEnabled(bool on) { m_qs.setValue(QStringLiteral("editing/proxyEnabled"), on); emit changed(QStringLiteral("editing/proxyEnabled")); }

bool Settings::aiEnabled() const { return m_qs.value(QStringLiteral("ai/enabled"), false).toBool(); }
void Settings::setAiEnabled(bool on) { m_qs.setValue(QStringLiteral("ai/enabled"), on); emit changed(QStringLiteral("ai/enabled")); }

QString Settings::aiModel() const { return m_qs.value(QStringLiteral("ai/model"), QStringLiteral("auto")).toString(); }
void Settings::setAiModel(const QString &m) { m_qs.setValue(QStringLiteral("ai/model"), m); emit changed(QStringLiteral("ai/model")); }

int Settings::aiHourlyBudget() const { return m_qs.value(QStringLiteral("ai/hourlyBudget"), 200).toInt(); }
void Settings::setAiHourlyBudget(int n) { m_qs.setValue(QStringLiteral("ai/hourlyBudget"), n); emit changed(QStringLiteral("ai/hourlyBudget")); }

bool Settings::aiPrivacyAccepted() const { return m_qs.value(QStringLiteral("ai/privacyAccepted"), false).toBool(); }
void Settings::setAiPrivacyAccepted() { m_qs.setValue(QStringLiteral("ai/privacyAccepted"), true); emit changed(QStringLiteral("ai/privacyAccepted")); }

void Settings::setRaw(const QString &key, const QVariant &value) { m_qs.setValue(key, value); emit changed(key); }
QVariant Settings::raw(const QString &key) const { return m_qs.value(key); }

} // namespace arena
