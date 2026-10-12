#pragma once
// Dialogs: New Project (with live preview), About, Welcome (first run),
// Settings (grouped, searchable), and the standard error dialog with
// "Show details" + "Copy diagnostics".
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>

#include "kilogateway.h"

namespace arena {

struct NewProjectResult {
    QString name;
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    QString aspect = QStringLiteral("16:9");
    int sampleRate = 48000;
    bool proxy = true;
};

class NewProjectDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewProjectDialog(QWidget *parent = nullptr);
    NewProjectResult result() const;

private:
    void updatePreview();
    QLineEdit *m_name;
    QComboBox *m_resolution;
    QComboBox *m_fps;
    QComboBox *m_aspect;
    QComboBox *m_sampleRate;
    QCheckBox *m_proxy;
    QLabel *m_preview;
};

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeDialog(QWidget *parent = nullptr);
    QString chosenLanguage() const; // "" = system default

private:
    QComboBox *m_language;
};

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const QList<KiloModel> &models, QWidget *parent = nullptr);
    void retranslateUi();

signals:
    void themeChanged();
    void languageChanged(const QString &bcp47);
    void fontScaleChanged();
    void accentChanged();
    void aiToggled(bool on);
    void aiModelChanged(const QString &model);
    void aiBudgetChanged(int n);

private:
    QWidget *buildPage(const QString &groupKey);
    void applySearch();

    QListWidget *m_groups;
    QStackedWidget *m_stack;
    QLineEdit *m_search;
    QList<KiloModel> m_models;
    // controls kept for retranslate + live apply
    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_fontScaleCombo = nullptr;
    QComboBox *m_languageCombo = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QSpinBox *m_budgetSpin = nullptr;
    QCheckBox *m_reduceMotion = nullptr;
    QCheckBox *m_aiEnable = nullptr;
};

// Plain-language error with expandable details + copy diagnostics.
class ErrorDialog : public QDialog {
    Q_OBJECT
public:
    ErrorDialog(const QString &message, const QString &details, QWidget *parent = nullptr);

private:
    QPlainTextEdit *m_details;
    QPushButton *m_toggle;
};

} // namespace arena
