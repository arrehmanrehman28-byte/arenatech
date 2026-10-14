#include "themer.h"
#include "logger.h"
#include <QApplication>
#include <QFile>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStyle>

namespace arena {

bool Themer::load(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot open theme file: %1").arg(path);
        return false;
    }
    QJsonParseError pe{};
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("Invalid theme JSON: %1").arg(pe.errorString());
        return false;
    }
    const QJsonObject o = doc.object();
    m_accentDefault = o.value(QStringLiteral("accentDefault")).toString(m_accentDefault);
    m_aiAccent = o.value(QStringLiteral("aiAccent")).toString(m_aiAccent);
    const QJsonObject radius = o.value(QStringLiteral("radius")).toObject();
    m_radiusControl = radius.value(QStringLiteral("control")).toInt(6);
    m_radiusPanel = radius.value(QStringLiteral("panel")).toInt(10);

    const QJsonObject font = o.value(QStringLiteral("font")).toObject();
    for (const QJsonValue &v : font.value(QStringLiteral("families")).toArray())
        if (!v.toString().isEmpty()) m_fontFamilies.append(v.toString());
    for (const QJsonValue &v : font.value(QStringLiteral("mono")).toArray())
        if (!v.toString().isEmpty()) m_monoFamilies.append(v.toString());
    if (m_fontFamilies.isEmpty()) m_fontFamilies = {QStringLiteral("Segoe UI Variable"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")};
    if (m_monoFamilies.isEmpty()) m_monoFamilies = {QStringLiteral("Cascadia Mono"), QStringLiteral("Noto Sans Mono"), QStringLiteral("DejaVu Sans Mono")};

    m_tokens.clear();
    m_themeOrder.clear();
    const QJsonObject themes = o.value(QStringLiteral("themes")).toObject();
    for (auto it = themes.constBegin(); it != themes.constEnd(); ++it) {
        QHash<QString, QString> t;
        const QJsonObject colors = it.value().toObject();
        for (auto c = colors.constBegin(); c != colors.constEnd(); ++c)
            t.insert(c.key(), c.value().toString());
        m_tokens.insert(it.key(), std::move(t));
        m_themeOrder.append(it.key());
    }
    if (m_tokens.isEmpty()) {
        if (error) *error = QStringLiteral("theme.json contains no themes");
        return false;
    }
    return true;
}

QStringList Themer::themeNames() const { return m_themeOrder; }
bool Themer::hasTheme(const QString &name) const { return m_tokens.contains(name); }
const QHash<QString, QString> &Themer::tokens(const QString &theme) const
{
    static const QHash<QString, QString> empty;
    const auto it = m_tokens.constFind(theme);
    return it != m_tokens.constEnd() ? it.value() : empty;
}
QColor Themer::themeColor(const QString &theme, const QString &token, const QColor &fallback) const
{
    const auto t = m_tokens.constFind(theme);
    if (t == m_tokens.constEnd()) return fallback;
    const QString v = t->value(token);
    if (v.isEmpty()) return fallback;
    QColor c(v);
    return c.isValid() ? c : fallback;
}

QString Themer::buildQss(const QString &theme, const QColor &accent) const
{
    auto c = [&](const char *token, const char *fb) {
        return themeColor(theme, token, QColor(fb)).name();
    };
    const QString s0 = c("surface0", "#14161A"), s1 = c("surface1", "#1B1E24"), s2 = c("surface2", "#232730"),
          s3 = c("surface3", "#2C313C"), s4 = c("surface4", "#363C49"),
          txt = c("text", "#E8EAF0"), muted = c("textMuted", "#9AA3B2"),
          border = c("border", "#3A4150"), ok = c("success", "#3DBE7B"),
          warn = c("warning", "#E5B454"), err = c("error", "#E5534B"),
          accentHex = accent.name();
    Q_UNUSED(ok) Q_UNUSED(warn) Q_UNUSED(err)

    QString qss;
    qss += QString("QMainWindow, QDialog { background-color: %1; }").arg(s0);
    qss += QString("QWidget { color: %1; font-size: 13px; }").arg(txt);
    qss += QString("QLabel { background: transparent; }");
    qss += QString("QLabel[role=\"muted\"] { color: %1; }").arg(muted);
    qss += QString("QLabel[role=\"error\"] { color: %1; }").arg(err);
    qss += QString("QLabel[role=\"success\"] { color: %1; }").arg(ok);
    qss += QString("QLabel[role=\"warning\"] { color: %1; }").arg(warn);
    qss += QString("QLabel[role=\"ai\"] { color: %1; }").arg(m_aiAccent);
    qss += QString("QPushButton { background-color: %1; border: 1px solid %2; border-radius: %3px; "
                   "padding: 6px 14px; color: %4; }")
                 .arg(s2)
                 .arg(border)
                 .arg(m_radiusControl)
                 .arg(txt);
    qss += QString("QPushButton:hover { background-color: %1; }").arg(s3);
    qss += QString("QPushButton:pressed { background-color: %1; }").arg(s4);
    qss += QString("QPushButton:disabled { color: %1; background-color: %2; }").arg(muted, s1);
    qss += QString(
                 "QPushButton[accent=\"true\"] { background-color: %1; color: #FFFFFF; border: none; }")
                 .arg(accentHex);
    qss += QString("QPushButton[accent=\"true\"]:hover { background-color: %1; }").arg(accent.darker(110).name());
    qss += QString(
                 "QPushButton[accent=\"ai\"] { background-color: %1; color: #FFFFFF; border: none; }")
                 .arg(m_aiAccent);
    qss += QString("QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit { "
                   "background-color: %1; border: 1px solid %2; border-radius: %3px; padding: 5px 8px; color: %4; "
                   "selection-background-color: %5; }")
                 .arg(s1)
                 .arg(border)
                 .arg(m_radiusControl)
                 .arg(txt)
                 .arg(accentHex);
    qss += QString("QComboBox::drop-down { border: none; width: 24px; }");
    qss += QString("QComboBox QAbstractItemView { background-color: %1; border: 1px solid %2; color: %3; "
                   "selection-background-color: %4; }")
                 .arg(s2, border, txt, accentHex);
    qss += QString("QMenuBar { background-color: %1; color: %2; }").arg(s1, txt);
    qss += QString("QMenuBar::item:selected { background: %1; border-radius: 4px; }").arg(s3);
    qss += QString("QMenu { background-color: %1; border: 1px solid %2; padding: 4px; }").arg(s2, border);
    qss += QString("QMenu::item { padding: 6px 24px; border-radius: 4px; }");
    qss += QString("QMenu::item:selected { background-color: %1; }").arg(accentHex);
    qss += QString("QMenu::separator { height: 1px; background: %1; margin: 4px 8px; }").arg(border);
    qss += QString("QToolBar { background-color: %1; border: none; spacing: 4px; }").arg(s1);
    qss += QString("QToolBar QToolButton { background: transparent; border-radius: 4px; padding: 4px; }");
    qss += QString("QToolBar QToolButton:hover { background: %1; }").arg(s3);
    qss += QString("QStatusBar { background-color: %1; color: %2; }").arg(s1, muted);
    qss += QString("QStatusBar::item { border: none; }");
    qss += QString("QDockWidget { titlebar-close-icon: none; }");
    qss += QString("QDockWidget::title { background: %1; padding: 6px 10px; color: %2; }").arg(s1, txt);
    qss += QString("QTreeWidget, QListView, QTableView { background-color: %1; alternate-background-color: %2; "
                   "border: none; color: %3; }")
                 .arg(s0, s1, txt);
    qss += QString("QTreeWidget::item:selected, QListView::item:selected, QTableView::item:selected { "
                   "background-color: %1; color: #FFFFFF; }")
                 .arg(accentHex);
    qss += QString("QHeaderView::section { background-color: %1; color: %2; padding: 4px; border: none; "
                   "border-bottom: 1px solid %3; }")
                 .arg(s1, muted, border);
    qss += QString("QSplitter::handle { background: %1; }").arg(border);
    qss += QString("QScrollBar:vertical { background: transparent; width: 10px; }");
    qss += QString("QScrollBar::handle:vertical { background: %1; border-radius: 5px; min-height: 24px; }").arg(s3);
    qss += QString("QScrollBar:horizontal { background: transparent; height: 10px; }");
    qss += QString("QScrollBar::handle:horizontal { background: %1; border-radius: 5px; min-width: 24px; }").arg(s3);
    qss += QString("QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }");
    qss += QString("QTabWidget::pane { border: 1px solid %1; border-radius: %2px; }")
                 .arg(border)
                 .arg(m_radiusControl);
    qss += QString("QTabBar::tab { background: %1; padding: 6px 14px; border: none; color: %2; }").arg(s1, muted);
    qss += QString("QTabBar::tab:selected { background: %1; color: %2; }").arg(s3, txt);
    qss += QString("QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; border-radius: 4px; "
                   "border: 1px solid %1; background: %2; }")
                 .arg(border, s1);
    qss += QString("QCheckBox::indicator:checked { background: %1; border-color: %1; }").arg(accentHex);
    // M8 (D-034/D-041): always-visible keyboard focus rings. QSS has no
    // `outline`, so a 2 px accent border is the focus indicator. Default accent
    // #4C8DFF clears WCAG 2.4.7 (3:1 vs adjacent colours) on every shipped
    // theme; users may re-pick the accent, so keep this rule, not a fixed hue.
    qss += QString("QPushButton:focus, QToolButton:focus, QComboBox:focus, "
                   "QSpinBox:focus, QDoubleSpinBox:focus, QCheckBox:focus, "
                   "QRadioButton:focus, QLineEdit:focus, QPlainTextEdit:focus, "
                   "QTextEdit:focus, QListView:focus, QTreeView:focus, "
                   "QTableView:focus, QAbstractSlider:focus { border: 2px solid %1; }")
                 .arg(accentHex);
    qss += QString("TimelineCanvas:focus, PreviewSurface:focus, "
                   "WaveformStrip:focus, ThumbnailStrip:focus { border: 2px solid %1; }")
                 .arg(accentHex);
    // M8 tour target highlight (set/unset by Tour via the "tourTarget" prop).
    qss += QString("QWidget[tourTarget=\"true\"] { border: 2px solid %1; }").arg(accentHex);
    return qss;
}

void Themer::apply(QApplication *app, const QString &themeName, const QColor &accent, int fontScaleIndex) const
{
    QFont f;
    f.setFamilies(m_fontFamilies);
    static const int sizes[] = {11, 13, 15, 17}; // small, default, large, extra large
    f.setPointSize(qBound(sizes[0], sizes[qBound(0, fontScaleIndex, 3)], sizes[3]));
    app->setFont(f);
    app->setPalette(app->style()->standardPalette());
    app->setStyleSheet(buildQss(themeName, accent));
}

} // namespace arena
