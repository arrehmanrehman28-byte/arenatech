#include "aipanel.h"
#include "kilogateway.h"
#include "settings.h"
#include "i18n.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>

namespace arena {

// --- AskCard -----------------------------------------------------------------

AskCard::AskCard(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 10);
    lay->setSpacing(6);
    m_text = new QPlainTextEdit;
    m_text->setReadOnly(true);
    m_text->setMaximumHeight(160);
    m_text->setProperty("role", QStringLiteral("ai"));
    lay->addWidget(m_text, 1);
    auto *row = new QHBoxLayout;
    m_accept = new QPushButton(arenaTr("panel.ai.accept"));
    m_edit = new QPushButton(arenaTr("panel.ai.edit"));
    m_reject = new QPushButton(arenaTr("panel.ai.reject"));
    m_accept->setEnabled(false);
    m_edit->setEnabled(false);
    row->addWidget(m_accept);
    row->addWidget(m_edit);
    row->addStretch(1);
    row->addWidget(m_reject);
    lay->addLayout(row);
    connect(m_accept, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(m_text->toPlainText());
        emit closed();
    });
    connect(m_reject, &QPushButton::clicked, this, [this] { emit closed(); });
}

void AskCard::appendText(const QString &delta)
{
    m_text->appendPlainText(delta);
}

void AskCard::markDone(bool ok)
{
    m_done = true;
    m_ok = ok;
    m_accept->setEnabled(ok);
    m_edit->setEnabled(ok);
    if (!ok) m_text->appendPlainText(QStringLiteral("\n[!] ") + arenaTr("panel.ai.failed"));
}

// --- AiPanel -----------------------------------------------------------------

AiPanel::AiPanel(KiloClient *client, Settings &settings, QWidget *parent)
    : QWidget(parent), m_client(client), m_settings(settings)
{
    setObjectName(QStringLiteral("AiPanel"));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(10);

    // Status card: off (neutral) vs on (violet).
    m_offCard = new QWidget;
    {
        auto *l = new QVBoxLayout(m_offCard);
        l->setContentsMargins(12, 12, 12, 12);
        l->setSpacing(6);
        auto *t = new QLabel(arenaTr("panel.ai.off.title"));
        QFont tf = t->font();
        tf.setBold(true);
        t->setFont(tf);
        auto *b = new QLabel(arenaTr("panel.ai.off.body"));
        b->setWordWrap(true);
        b->setProperty("role", QStringLiteral("muted"));
        m_enableBtn = new QPushButton(arenaTr("panel.ai.enable"));
        m_enableBtn->setProperty("accent", QStringLiteral("ai"));
        l->addWidget(t);
        l->addWidget(b);
        l->addWidget(m_enableBtn, 0, Qt::AlignLeft);
    }
    m_onCard = new QWidget;
    {
        auto *l = new QVBoxLayout(m_onCard);
        l->setContentsMargins(12, 12, 12, 12);
        l->setSpacing(6);
        auto *t = new QLabel(arenaTr("panel.ai.on.title"));
        t->setProperty("role", QStringLiteral("ai"));
        QFont tf = t->font();
        tf.setBold(true);
        t->setFont(tf);
        m_modelLabel = new QLabel;
        m_modelLabel->setProperty("role", QStringLiteral("muted"));
        l->addWidget(t);
        l->addWidget(m_modelLabel);
    }
    lay->addWidget(m_offCard);
    lay->addWidget(m_onCard);
    m_onCard->hide();

    // Task cards (each becomes functional in its own milestone).
    m_tasks = new QListWidget;
    m_tasks->setFrameShape(QFrame::NoFrame);
    const QStringList taskKeys = {
        QStringLiteral("panel.ai.translate"), QStringLiteral("panel.ai.chapters"),
        QStringLiteral("panel.ai.titles"), QStringLiteral("panel.ai.cuts")};
    for (const QString &k : taskKeys) {
        auto *item = new QListWidgetItem(arenaTr(k));
        m_tasks->addItem(item);
    }
    lay->addWidget(m_tasks, 1);

    // Ask AI.
    m_askInput = new QLineEdit;
    m_askInput->setPlaceholderText(arenaTr("panel.ai.ask.placeholder"));
    m_askSend = new QPushButton(arenaTr("panel.ai.ask.send"));
    auto *row = new QHBoxLayout;
    row->addWidget(m_askInput, 1);
    row->addWidget(m_askSend);
    lay->addLayout(row);
    m_results = new QListWidget;
    m_results->setFrameShape(QFrame::NoFrame);
    m_results->setMaximumHeight(220);
    lay->addWidget(m_results);

    connect(m_enableBtn, &QPushButton::clicked, this, &AiPanel::enableRequested);
    connect(m_askSend, &QPushButton::clicked, this, &AiPanel::ask);
    connect(m_askInput, &QLineEdit::returnPressed, this, &AiPanel::ask);
    refreshStatus();
}

void AiPanel::refreshStatus()
{
    const bool on = m_settings.aiEnabled();
    m_offCard->setVisible(!on);
    m_onCard->setVisible(on);
    m_tasks->setEnabled(on);
    m_askInput->setEnabled(on);
    m_askSend->setEnabled(on);
    if (on)
        m_modelLabel->setText(QStringLiteral("%1: %2").arg(arenaTr("settings.ai.model"),
                                                           m_client->currentModel()));
}

void AiPanel::retranslateUi()
{
    // Rebuild task labels + buttons (kept simple: relabel in place).
    const QStringList taskKeys = {
        QStringLiteral("panel.ai.translate"), QStringLiteral("panel.ai.chapters"),
        QStringLiteral("panel.ai.titles"), QStringLiteral("panel.ai.cuts")};
    for (int i = 0; i < m_tasks->count() && i < taskKeys.size(); ++i)
        m_tasks->item(i)->setText(arenaTr(taskKeys.at(i)));
    m_enableBtn->setText(arenaTr("panel.ai.enable"));
    m_askInput->setPlaceholderText(arenaTr("panel.ai.ask.placeholder"));
    m_askSend->setText(arenaTr("panel.ai.ask.send"));
    refreshStatus();
}

void AiPanel::setEnabledUi(bool enabled)
{
    m_settings.setAiEnabled(enabled);
    refreshStatus();
    if (!enabled)
        emit disableRequested();
}

void AiPanel::ask()
{
    const QString text = m_askInput->text().trimmed();
    if (text.isEmpty()) return;
    m_askInput->clear();
    askText(text);
}

void AiPanel::askText(const QString &text)
{
    if (text.isEmpty()) return;
    if (!m_settings.aiEnabled()) {
        QMessageBox::warning(this, arenaTr("panel.ai"), arenaTr("panel.ai.off.title"));
        return;
    }
    m_askSend->setEnabled(false);

    auto *card = new AskCard;
    auto *item = new QListWidgetItem;
    m_results->addItem(item);
    m_results->setItemWidget(item, card);
    const int row = m_results->count() - 1;
    Q_UNUSED(item)
    connect(card, &AskCard::closed, this, [this, row] {
        if (m_results->count() > row)
            delete m_results->takeItem(row);
    });

    card->appendText(QStringLiteral("→ ") + text + QStringLiteral("\n"));
    QString streamed;
    m_client->chat(
        QStringLiteral("You are Arena Edit, a video editor assistant. Answer concisely in the user's language."),
        text,
        [card, &streamed](const QString &delta) {
            streamed += delta;
            card->appendText(delta);
        },
        [card, this](const QString &, const QString &err) {
            card->markDone(err.isEmpty());
            if (!err.isEmpty()) qWarning() << "AI request failed:" << err;
            m_askSend->setEnabled(m_settings.aiEnabled());
        });
}

} // namespace arena
