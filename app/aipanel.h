#pragma once
// AI panel (violet accent). All AI is opt-in; nothing is applied without
// user confirmation. M0 scope: privacy notice, Enable/Disable, "Ask AI"
// with live streaming from the Kilo Gateway.
// M6.2: the four task cards are functional. Each task builds a prompt from
// project TEXT only (subtitle cues, metadata, derived silence map), streams
// the model response into a review card, validates it offline
// (aifeatures), and only then enables Accept. Accept emits a typed signal;
// the main window turns it into an undoable command. The panel itself
// never mutates the project.
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "aifeatures.h"

namespace arena {

class KiloClient;
class Settings;
class Project;

// The four M6 AI tasks.
enum class AiTaskType { Translate, Chapters, Titles, Cuts };

// Generic free-form "Ask AI" card: streams the answer, offers
// copy-to-clipboard / reject.
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

// One task card: title, muted hint, optional source-track / target-language
// combos (translation only), Run button.
class TaskCard : public QWidget {
    Q_OBJECT
public:
    explicit TaskCard(AiTaskType type, QWidget *parent = nullptr);
    void retranslateUi();
    // available=false greys the card out and shows `hint` explaining why.
    void setAvailable(bool available, const QString &hint);
    void fillSourceTracks(const QList<QPair<QString, QString>> &tracks);

    QString sourceTrackId() const;
    QString targetLang() const;

signals:
    void runClicked(AiTaskType type);

private:
    AiTaskType m_type;
    QLabel *m_title;
    QLabel *m_hint;
    QLabel *m_sourceLabel = nullptr; // translation only
    QLabel *m_targetLabel = nullptr; // translation only
    QComboBox *m_sourceTrack = nullptr; // translation only
    QComboBox *m_targetLang = nullptr;  // translation only
    QPushButton *m_run;
};

// Review card for a task result: streams raw response text while working,
// then shows a validated preview and an Accept/Reject row. Accept is only
// enabled once parsing succeeded; the payload lives on the card and is
// carried to AiPanel through accepted().
class TaskResultCard : public QWidget {
    Q_OBJECT
public:
    explicit TaskResultCard(QWidget *parent = nullptr);
    void setTaskTitle(const QString &title);
    void setStatus(const QString &status);
    void appendStream(const QString &delta);
    void showPreview(const QString &preview);
    void markFailed(const QString &reason);
    void markApplied(const QString &note);

    // Translation batch bookkeeping (batches of ~40, one retry each).
    void setBatchRange(int rangeIndex, int first, int last, int total);
    bool batchRetried(int rangeIndex) const { return m_retried.contains(rangeIndex); }
    void setBatchRetried(int rangeIndex) { m_retried.insert(rangeIndex); }
    void setTotalEntries(int total);

    // Payload (filled at parse time; read by AiPanel on accept).
    void setAiTaskType(AiTaskType t) { m_type = t; }
    AiTaskType taskType() const { return m_type; }
    void setSourceTrackId(const QString &s) { m_sourceTrackId = s; }
    QString sourceTrackId() const { return m_sourceTrackId; }
    void setTargetLang(const QString &l) { m_targetLang = l; }
    QString targetLang() const { return m_targetLang; }
    void setTranslations(const QStringList &v) { m_translations = v; }
    QStringList translations() const { return m_translations; }
    void setChapters(const QVector<ChapterSuggestion> &v) { m_chapters = v; }
    QVector<ChapterSuggestion> chapters() const { return m_chapters; }
    void setTitleResult(const TitleResult &v) { m_title = v; }
    TitleResult titleResult() const { return m_title; }
    void setCuts(const QVector<CutSuggestion> &v) { m_cuts = v; }
    QVector<CutSuggestion> cuts() const { return m_cuts; }

signals:
    void accepted();
    void closed();

private:
    QPlainTextEdit *m_text;
    QLabel *m_status;
    QPushButton *m_accept;
    QPushButton *m_reject;
    AiTaskType m_type = AiTaskType::Titles;
    QString m_sourceTrackId;
    QString m_targetLang;
    QStringList m_translations;
    QVector<ChapterSuggestion> m_chapters;
    TitleResult m_title;
    QVector<CutSuggestion> m_cuts;
    QSet<int> m_retried;
};

class AiPanel : public QWidget {
    Q_OBJECT
public:
    AiPanel(KiloClient *client, Settings &settings, QWidget *parent = nullptr);
    void retranslateUi();
    void setEnabledUi(bool enabled);
    // Attach/detach the project the task cards work on (nullptr = none).
    // Cheap: called on every project view refresh.
    void setProject(Project *project);
    void askText(const QString &text); // used by the palette's /ai mode

signals:
    void enableRequested();   // main window shows the privacy notice
    void disableRequested();
    // M6.2 accept payloads (validated by aifeatures; applied by the
    // main window as undoable commands).
    void translationAccepted(const QString &sourceTrackId,
                             const QString &targetLang,
                             const QStringList &texts);
    void chaptersAccepted(const QVector<ChapterSuggestion> &chapters);
    void titleAccepted(const TitleResult &title);
    void cutsAccepted(const QVector<CutSuggestion> &cuts);

private:
    void ask();
    void refreshStatus();
    void refreshAvailability();
    void runTask(AiTaskType type);
    void runTranslateBatch(TaskResultCard *card, const QString &sourceTrackId,
                           const QString &targetLang, int rangeIndex);
    void finishTranslation(TaskResultCard *card, const QString &sourceTrackId,
                           const QString &targetLang);
    void onCardAccepted(TaskResultCard *card);
    // Transcript / silence inputs (project text only).
    QStringList transcriptLines() const; // "HH:MM:SS:FF text"
    QString silenceMapText() const;
    qint64 timelineDurationFrames() const;
    int subtitleCueCount() const;
    bool hasVideoTrack() const;

    KiloClient *m_client;
    Settings &m_settings;
    Project *m_project = nullptr;
    QWidget *m_offCard;
    QWidget *m_onCard;
    QLabel *m_modelLabel;
    QPushButton *m_enableBtn;
    TaskCard *m_cardTranslate = nullptr;
    TaskCard *m_cardChapters = nullptr;
    TaskCard *m_cardTitles = nullptr;
    TaskCard *m_cardCuts = nullptr;
    QLineEdit *m_askInput;
    QPushButton *m_askSend;
    QListWidget *m_results;
};

} // namespace arena
