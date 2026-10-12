#pragma once
// AI panel (violet accent). All AI is opt-in; nothing is applied without
// user confirmation. Phase M0 scope: privacy notice, Enable/Disable,
// "Ask AI" with live streaming from the Kilo Gateway, reviewable result
// cards (Accept / Edit / Reject). Task cards land with their phases.
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>

class QObject;

namespace arena {

class KiloClient;
class Settings;

class AskCard : public QWidget {
    Q_OBJECT
public:
    explicit AskCard(QWidget *parent = nullptr);
    void appendText(const QString &delta);
    void markDone(bool ok);
    bool ok() const { return m_ok; }

signals:
    void closed();

private:
    QPlainTextEdit *m_text;
    QPushButton *m_accept;
    QPushButton *m_edit;
    QPushButton *m_reject;
    bool m_done = false;
    bool m_ok = false;
};

class AiPanel : public QWidget {
    Q_OBJECT
public:
    AiPanel(KiloClient *client, Settings &settings, QWidget *parent = nullptr);
    void retranslateUi();
    void setEnabledUi(bool enabled);
    void askText(const QString &text); // used by the palette's /ai mode

signals:
    void enableRequested();   // main window shows the privacy notice
    void disableRequested();

private:
    void ask();
    void refreshStatus();

    KiloClient *m_client;
    Settings &m_settings;
    QWidget *m_offCard;
    QWidget *m_onCard;
    QLabel *m_modelLabel;
    QPushButton *m_enableBtn;
    QListWidget *m_tasks;
    QLineEdit *m_askInput;
    QPushButton *m_askSend;
    QListWidget *m_results;
};

} // namespace arena
