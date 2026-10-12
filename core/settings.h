#pragma once
// Typed settings over QSettings. Changes emit changed(); UI applies live where possible.
#include <QObject>
#include <QColor>
#include <QSettings>
#include <QString>

namespace arena {

class Settings : public QObject {
    Q_OBJECT
public:
    static Settings &instance();

    // General
    QString language() const;        // BCP-47, "en" default; "" = system
    void setLanguage(const QString &bcp47);
    bool firstRunDone() const;
    void setFirstRunDone();

    // Appearance
    QString theme() const;           // "dark" | "light" | "high-contrast"
    void setTheme(const QString &theme);
    QColor accentColor() const;      // default #4C8DFF
    void setAccentColor(const QColor &c);
    int fontScale() const;           // 0 small, 1 default, 2 large, 3 extra large
    void setFontScale(int s);
    bool reduceMotion() const;       // follows Windows "reduce motion" unless overridden
    void setReduceMotion(bool on);

    // Editing / playback
    bool snapEnabled() const;
    void setSnapEnabled(bool on);
    int autosaveSeconds() const;     // default 60
    void setAutosaveSeconds(int s);
    bool proxyEnabled() const;
    void setProxyEnabled(bool on);

    // AI
    bool aiEnabled() const;          // OFF by default; core never depends on AI
    void setAiEnabled(bool on);
    QString aiModel() const;         // configurable, never hardcoded; ":free" default
    void setAiModel(const QString &model);
    int aiHourlyBudget() const;      // default 200 (gateway anonymous limit)
    void setAiHourlyBudget(int n);
    bool aiPrivacyAccepted() const;  // privacy notice shown before first request
    void setAiPrivacyAccepted();

    void setRaw(const QString &key, const QVariant &value);
    QVariant raw(const QString &key) const;

signals:
    void changed(const QString &key);

private:
    Settings();
    QSettings m_qs;
};

} // namespace arena
