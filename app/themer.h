#pragma once
// Design tokens live in assets/theme/theme.json (single source of truth).
// Themer turns them into QSS for the three themes and applies accent + fonts.
#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>

class QApplication;

namespace arena {

class Themer {
public:
    bool load(const QString &themeJsonPath, QString *error = nullptr);

    QStringList themeNames() const;
    bool hasTheme(const QString &name) const;
    QColor themeColor(const QString &theme, const QString &token, const QColor &fallback) const;
    const QHash<QString, QString> &tokens(const QString &theme) const;
    int radiusControl() const { return m_radiusControl; }
    int radiusPanel() const { return m_radiusPanel; }
    QString accentDefault() const { return m_accentDefault; }
    QString aiAccent() const { return m_aiAccent; }
    QStringList fontFamilies() const { return m_fontFamilies; }
    QStringList monoFamilies() const { return m_monoFamilies; }

    // Applies fonts + QSS to the whole application.
    void apply(QApplication *app, const QString &themeName, const QColor &accent, int fontScaleIndex) const;
    QString buildQss(const QString &themeName, const QColor &accent) const;

private:
    QHash<QString, QHash<QString, QString>> m_tokens; // theme -> token -> color
    QStringList m_themeOrder;
    QString m_accentDefault = QStringLiteral("#4C8DFF");
    QString m_aiAccent = QStringLiteral("#9B6DFF");
    int m_radiusControl = 6;
    int m_radiusPanel = 10;
    QStringList m_fontFamilies;
    QStringList m_monoFamilies;
};

} // namespace arena
