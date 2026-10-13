#include "aipanel.h"
#include "kilogateway.h"
#include "panels.h"
#include "settings.h"
#include "i18n.h"
#include "project.h"
#include "util.h"
#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageLogger>
#include <QMessageBox>
#include <QVBoxLayout>

#include <algorithm>

namespace arena {

namespace {
// One system prompt for every structured task: machine output only.
const QString kSystemPrompt = QStringLiteral(
    "You are Arena Edit's structured-output assistant. "
    "Follow the requested output format exactly and output the "
    "machine-readable content only, with no commentary.");
} // namespace

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

// --- TaskCard ----------------------------------------------------------------

TaskCard::TaskCard(AiTaskType type, QWidget *parent) : QWidget(parent), m_type(type)
{
    setProperty("role", QStringLiteral("ai"));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(4);

    auto *head = new QHBoxLayout;
    m_title = new QLabel;
    m_title->setProperty("role", QStringLiteral("ai"));
    QFont tf = m_title->font();
    tf.setBold(true);
    m_title->setFont(tf);
    m_run = new QPushButton(arenaTr("panel.ai.task.run"));
    m_run->setProperty("accent", QStringLiteral("ai"));
    head->addWidget(m_title, 1);
    head->addWidget(m_run);
    lay->addLayout(head);

    if (m_type == AiTaskType::Translate) {
        m_sourceTrack = new QComboBox;
        m_targetLang = new QComboBox;
        for (const QString &l : subtitleLanguages()) m_targetLang->addItem(l);
        m_sourceLabel = new QLabel(arenaTr("panel.ai.task.sourceTrack"));
        m_targetLabel = new QLabel(arenaTr("panel.ai.task.targetLang"));
        m_sourceLabel->setProperty("role", QStringLiteral("muted"));
        m_targetLabel->setProperty("role", QStringLiteral("muted"));
        auto *row = new QHBoxLayout;
        row->addWidget(m_sourceLabel);
        row->addWidget(m_sourceTrack, 1);
        row->addWidget(m_targetLabel);
        row->addWidget(m_targetLang);
        lay->addLayout(row);
    }

    m_hint = new QLabel;
    m_hint->setWordWrap(true);
    m_hint->setProperty("role", QStringLiteral("muted"));
    lay->addWidget(m_hint);

    retranslateUi();
    connect(m_run, &QPushButton::clicked, this, [this] { emit runClicked(m_type); });
}

void TaskCard::retranslateUi()
{
    switch (m_type) {
    case AiTaskType::Translate:
        m_title->setText(arenaTr("panel.ai.translate"));
        m_hint->setText(arenaTr("panel.ai.task.translate.hint"));
        break;
    case AiTaskType::Chapters:
        m_title->setText(arenaTr("panel.ai.chapters"));
        m_hint->setText(arenaTr("panel.ai.task.chapters.hint"));
        break;
    case AiTaskType::Titles:
        m_title->setText(arenaTr("panel.ai.titles"));
        m_hint->setText(arenaTr("panel.ai.task.titles.hint"));
        break;
    case AiTaskType::Cuts:
        m_title->setText(arenaTr("panel.ai.cuts"));
        m_hint->setText(arenaTr("panel.ai.task.cuts.hint"));
        break;
    }
    m_run->setText(arenaTr("panel.ai.task.run"));
    if (m_sourceLabel) m_sourceLabel->setText(arenaTr("panel.ai.task.sourceTrack"));
    if (m_targetLabel) m_targetLabel->setText(arenaTr("panel.ai.task.targetLang"));
}

void TaskCard::setAvailable(bool available, const QString &hint)
{
    m_run->setEnabled(available);
    if (m_sourceTrack) m_sourceTrack->setEnabled(available);
    if (m_targetLang) m_targetLang->setEnabled(available);
    m_hint->setText(available ? QString() : hint);
}

void TaskCard::fillSourceTracks(const QList<QPair<QString, QString>> &tracks)
{
    if (!m_sourceTrack) return;
    const QString keep = m_sourceTrack->currentData().toString();
    m_sourceTrack->clear();
    for (const auto &t : tracks) m_sourceTrack->addItem(t.second, t.first);
    const int idx = m_sourceTrack->findData(keep);
    m_sourceTrack->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString TaskCard::sourceTrackId() const
{
    return m_sourceTrack ? m_sourceTrack->currentData().toString() : QString();
}

QString TaskCard::targetLang() const
{
    return m_targetLang ? m_targetLang->currentText() : QStringLiteral("en");
}

// --- TaskResultCard ----------------------------------------------------------

TaskResultCard::TaskResultCard(QWidget *parent) : QWidget(parent)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 8, 10, 10);
    lay->setSpacing(6);
    m_text = new QPlainTextEdit;
    m_text->setReadOnly(true);
    m_text->setMaximumHeight(200);
    m_text->setProperty("role", QStringLiteral("ai"));
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setProperty("role", QStringLiteral("muted"));
    m_accept = new QPushButton(arenaTr("panel.ai.accept"));
    m_accept->setProperty("accent", QStringLiteral("ai"));
    m_accept->setEnabled(false);
    m_reject = new QPushButton(arenaTr("panel.ai.reject"));
    auto *row = new QHBoxLayout;
    row->addWidget(m_accept);
    row->addStretch(1);
    row->addWidget(m_reject);
    lay->addWidget(m_text, 1);
    lay->addWidget(m_status);
    lay->addLayout(row);
    connect(m_accept, &QPushButton::clicked, this, [this] {
        m_accept->setEnabled(false);
        emit accepted();
    });
    connect(m_reject, &QPushButton::clicked, this, [this] { emit closed(); });
}

void TaskResultCard::setTaskTitle(const QString &title)
{
    m_text->setPlainText(title + QStringLiteral("\n\n"));
}

void TaskResultCard::setStatus(const QString &status)
{
    m_status->setText(status);
}

void TaskResultCard::appendStream(const QString &delta)
{
    m_text->appendPlainText(delta);
}

void TaskResultCard::showPreview(const QString &preview)
{
    m_text->setPlainText(preview);
    m_status->clear();
    m_accept->setEnabled(true);
}

void TaskResultCard::markFailed(const QString &reason)
{
    m_status->setText(QStringLiteral("[!] ") + reason);
    m_accept->setEnabled(false);
}

void TaskResultCard::markApplied(const QString &note)
{
    m_status->setText(arenaTr("panel.ai.applied") +
                      (note.isEmpty() ? QString() : QStringLiteral(" ") + note));
    m_accept->setEnabled(false);
    m_reject->setEnabled(false);
}

void TaskResultCard::setBatchRange(int rangeIndex, int first, int last, int total)
{
    Q_UNUSED(rangeIndex)
    Q_UNUSED(first)
    Q_UNUSED(last)
    Q_UNUSED(total)
}

void TaskResultCard::setTotalEntries(int total)
{
    m_translations.resize(total);
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

    // Task cards (M6.2: all four functional).
    m_cardTranslate = new TaskCard(AiTaskType::Translate);
    m_cardChapters = new TaskCard(AiTaskType::Chapters);
    m_cardTitles = new TaskCard(AiTaskType::Titles);
    m_cardCuts = new TaskCard(AiTaskType::Cuts);
    lay->addWidget(m_cardTranslate);
    lay->addWidget(m_cardChapters);
    lay->addWidget(m_cardTitles);
    lay->addWidget(m_cardCuts);
    connect(m_cardTranslate, &TaskCard::runClicked, this, [this](AiTaskType t) { runTask(t); });
    connect(m_cardChapters, &TaskCard::runClicked, this, [this](AiTaskType t) { runTask(t); });
    connect(m_cardTitles, &TaskCard::runClicked, this, [this](AiTaskType t) { runTask(t); });
    connect(m_cardCuts, &TaskCard::runClicked, this, [this](AiTaskType t) { runTask(t); });

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
    lay->addWidget(m_results, 1);

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
    m_askInput->setEnabled(on);
    m_askSend->setEnabled(on);
    if (on)
        m_modelLabel->setText(QStringLiteral("%1: %2").arg(arenaTr("settings.ai.model"),
                                                           m_client->currentModel()));
    refreshAvailability();
}

void AiPanel::setProject(Project *project)
{
    const bool changed = (m_project != project);
    m_project = project;
    if (changed) {
        QList<QPair<QString, QString>> tracks;
        if (project) {
            for (const SubtitleTrack &t : project->subtitleTracks)
                tracks.append({t.id, QStringLiteral("%1 (%2) — %3")
                                             .arg(t.name, t.language,
                                                  QString::number(t.cues.size()))});
        }
        m_cardTranslate->fillSourceTracks(tracks);
    }
    refreshAvailability();
}

int AiPanel::subtitleCueCount() const
{
    if (!m_project) return 0;
    int n = 0;
    for (const SubtitleTrack &t : m_project->subtitleTracks) n += t.cues.size();
    return n;
}

qint64 AiPanel::timelineDurationFrames() const
{
    if (!m_project) return 0;
    qint64 d = 0;
    for (const Track &t : m_project->tracks)
        if (t.type == TrackType::Video)
            for (const Clip &c : t.clips) d = qMax<qint64>(d, c.offset + c.duration());
    return d;
}

bool AiPanel::hasVideoTrack() const
{
    if (!m_project) return false;
    for (const Track &t : m_project->tracks)
        if (t.type == TrackType::Video && !t.clips.isEmpty()) return true;
    return false;
}

void AiPanel::refreshAvailability()
{
    const bool on = m_settings.aiEnabled();
    const bool proj = m_project != nullptr;
    const int cues = subtitleCueCount();
    const qint64 dur = timelineDurationFrames();

    const QString none = arenaTr("panel.ai.needsProject");
    const QString noSubs = arenaTr("panel.ai.needsSubtitles");
    const QString noVideo = arenaTr("panel.ai.noVideoTrack");

    m_cardTranslate->setAvailable(on && proj && cues > 0, !proj ? none : noSubs);
    m_cardChapters->setAvailable(on && proj && cues > 0 && dur > 0, !proj ? none : noSubs);
    m_cardTitles->setAvailable(on && proj, none);
    m_cardCuts->setAvailable(on && proj && cues > 0 && dur > 0 && hasVideoTrack(),
                             !proj ? none : (cues > 0 ? noVideo : noSubs));
}

void AiPanel::retranslateUi()
{
    m_cardTranslate->retranslateUi();
    m_cardChapters->retranslateUi();
    m_cardTitles->retranslateUi();
    m_cardCuts->retranslateUi();
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

// --- transcript / silence inputs (text only) --------------------------------

QStringList AiPanel::transcriptLines() const
{
    struct Line {
        qint64 frame;
        QString text;
    };
    QList<Line> all;
    if (m_project)
        for (const SubtitleTrack &t : m_project->subtitleTracks)
            for (const TimelineCue &c : t.cues)
                all.append({c.startFrame, c.text.trimmed()});
    std::sort(all.begin(), all.end(),
              [](const Line &a, const Line &b) { return a.frame < b.frame; });
    const double fps = m_project ? m_project->fps : 30.0;
    QStringList out;
    for (const Line &l : all)
        out << framesToTimecode(l.frame, fps) + QLatin1Char(' ') + l.text;
    return out;
}

QString AiPanel::silenceMapText() const
{
    if (!m_project) return QString();
    const double fps = m_project->fps;
    const qint64 dur = timelineDurationFrames();
    const qint64 minGap = qMax<qint64>(1, qRound(fps)); // gaps of 1 s or more
    QStringList out;
    auto addGap = [&](qint64 a, qint64 b) {
        if (b - a >= minGap)
            out << QStringLiteral("%1 - %2 (%3s)")
                     .arg(framesToTimecode(a, fps), framesToTimecode(b, fps),
                          QString::number(double(b - a) / fps, 'f', 1));
    };
    for (const SubtitleTrack &t : m_project->subtitleTracks) {
        if (t.cues.isEmpty()) continue;
        addGap(0, t.cues.first().startFrame);
        for (int i = 1; i < t.cues.size(); ++i)
            addGap(t.cues[i - 1].endFrame, t.cues[i].startFrame);
        addGap(t.cues.last().endFrame, dur);
    }
    return out.join(QLatin1Char('\n'));
}

// --- task execution ----------------------------------------------------------

void AiPanel::runTask(AiTaskType type)
{
    if (!m_settings.aiEnabled()) {
        QMessageBox::warning(this, arenaTr("panel.ai"), arenaTr("panel.ai.off.title"));
        return;
    }
    if (!m_project) return;
    m_askSend->setEnabled(false);

    auto *card = new TaskResultCard;
    card->setAiTaskType(type);
    QListWidgetItem *item = nullptr;
    switch (type) {
    case AiTaskType::Translate: {
        const QString srcId = m_cardTranslate->sourceTrackId();
        const QString lang = m_cardTranslate->targetLang();
        const SubtitleTrack *src = nullptr;
        for (const SubtitleTrack &t : m_project->subtitleTracks)
            if (t.id == srcId) { src = &t; break; }
        if (!src || src->cues.isEmpty()) {
            delete card;
            QMessageBox::warning(this, arenaTr("panel.ai"),
                                 arenaTr("panel.ai.needsSubtitles"));
            return;
        }
        card->setTaskTitle(arenaTr("panel.ai.translate"));
        card->setSourceTrackId(srcId);
        card->setTargetLang(lang);
        card->setTotalEntries(src->cues.size());
        card->setStatus(arenaTr("panel.ai.streaming"));
        item = new QListWidgetItem;
        break;
    }
    case AiTaskType::Chapters:
        card->setTaskTitle(arenaTr("panel.ai.chapters"));
        card->setStatus(arenaTr("panel.ai.streaming"));
        item = new QListWidgetItem;
        break;
    case AiTaskType::Titles:
        card->setTaskTitle(arenaTr("panel.ai.titles"));
        card->setStatus(arenaTr("panel.ai.streaming"));
        item = new QListWidgetItem;
        break;
    case AiTaskType::Cuts:
        card->setTaskTitle(arenaTr("panel.ai.cuts"));
        card->setStatus(arenaTr("panel.ai.streaming"));
        item = new QListWidgetItem;
        break;
    }
    m_results->addItem(item);
    m_results->setItemWidget(item, card);
    const int row = m_results->count() - 1;
    connect(card, &TaskResultCard::closed, this, [this, row] {
        if (m_results->count() > row)
            delete m_results->takeItem(row);
    });
    connect(card, &TaskResultCard::accepted, this, [this, card] { onCardAccepted(card); });

    if (type == AiTaskType::Translate) {
        runTranslateBatch(card, card->sourceTrackId(), card->targetLang(), 0);
        return;
    }

    const double fps = m_project->fps;
    const qint64 dur = timelineDurationFrames();
    const double durSec = double(dur) / fps;

    if (type == AiTaskType::Chapters) {
        const QString user = aifeatures::chaptersPrompt(transcriptLines().join('\n'));
        m_client->chat(kSystemPrompt, user,
                       [card](const QString &d) { card->appendStream(d); },
                       [this, card, user, fps, durSec](const QString &full, const QString &err) {
                           QVector<ChapterSuggestion> chapters;
                           QString verr;
                           if (err.isEmpty() &&
                               aifeatures::parseChapters(full, durSec, &chapters, &verr)) {
                               card->setChapters(chapters);
                               QString p = arenaTr("panel.ai.preview.chapters",
                                                   {{QStringLiteral("count"), QString::number(chapters.size())}});
                               for (const ChapterSuggestion &c : chapters)
                                   p += QStringLiteral("\n%1  %2")
                                            .arg(framesToTimecode(
                                                     static_cast<qint64>(qRound(c.startSec * fps)),
                                                     fps),
                                                 c.title);
                               card->showPreview(p);
                               m_askSend->setEnabled(m_settings.aiEnabled());
                           } else {
                               invalidateFor(user); // cached bad text blocks re-runs
                               card->markFailed(err.isEmpty() ? verr : err);
                               m_askSend->setEnabled(m_settings.aiEnabled());
                           }
                       },
                       aifeatures::kChaptersMaxTokens);
        return;
    }

    if (type == AiTaskType::Titles) {
        const QString context = QStringLiteral(
            "Project name: %1\nDuration: %2 s\nResolution: %3x%4 @ %5 fps\n"
            "Captions (first 20 lines):\n%6")
                                    .arg(m_project->name)
                                    .arg(QString::number(durSec, 'f', 1))
                                    .arg(m_project->width)
                                    .arg(m_project->height)
                                    .arg(QString::number(fps, 'g', 3))
                                    .arg(transcriptLines().value(0, QString()).isEmpty()
                                             ? QStringLiteral("(none)")
                                             : transcriptLines().mid(0, 20).join('\n'));
        const QString user = aifeatures::titlePrompt(context);
        m_client->chat(kSystemPrompt, user,
                       [card](const QString &d) { card->appendStream(d); },
                       [this, card, user](const QString &full, const QString &err) {
                           TitleResult t;
                           QString verr;
                           if (err.isEmpty() && aifeatures::parseTitle(full, &t, &verr)) {
                               card->setTitleResult(t);
                               QString p = arenaTr("panel.ai.preview.titles") +
                                           QStringLiteral("\n%1: %2\n%3: %4")
                                               .arg(arenaTr("panel.ai.title.label"), t.title)
                                               .arg(arenaTr("panel.ai.description.label"),
                                                    t.description);
                               card->showPreview(p);
                               m_askSend->setEnabled(m_settings.aiEnabled());
                           } else {
                               invalidateFor(user); // cached bad text blocks re-runs
                               card->markFailed(err.isEmpty() ? verr : err);
                               m_askSend->setEnabled(m_settings.aiEnabled());
                           }
                       },
                       aifeatures::kTitleMaxTokens);
        return;
    }

    // Cuts.
    const QString user = aifeatures::cutsPrompt(transcriptLines().join('\n'),
                                                silenceMapText(), fps);
    m_client->chat(kSystemPrompt, user,
                   [card](const QString &d) { card->appendStream(d); },
                   [this, card, user, fps, durSec](const QString &full, const QString &err) {
                       QVector<CutSuggestion> cuts;
                       QString verr;
                       if (err.isEmpty() &&
                           aifeatures::parseCuts(full, durSec, fps, &cuts, &verr)) {
                           card->setCuts(cuts);
                           QString p = arenaTr("panel.ai.preview.cuts",
                                               {{QStringLiteral("count"), QString::number(cuts.size())}});
                           for (const CutSuggestion &c : cuts) {
                               const qint64 s = static_cast<qint64>(qRound(c.startSec * fps));
                               const qint64 e = static_cast<qint64>(qRound(c.endSec * fps));
                               p += QStringLiteral("\n%1 → %2   %3 (%4%)")
                                        .arg(framesToTimecode(s, fps), framesToTimecode(e, fps),
                                             arenaTr(c.reason == "silence"
                                                         ? "panel.ai.reason.silence"
                                                         : "panel.ai.reason.filler"),
                                             QString::number(int(c.confidence * 100.0)));
                           }
                           card->showPreview(p);
                           m_askSend->setEnabled(m_settings.aiEnabled());
                       } else {
                           invalidateFor(user); // cached bad text blocks re-runs
                           card->markFailed(err.isEmpty() ? verr : err);
                           m_askSend->setEnabled(m_settings.aiEnabled());
                       }
                   },
                   aifeatures::kCutsMaxTokens);
}

void AiPanel::invalidateFor(const QString &userPrompt)
{
    if (m_client)
        m_client->invalidateCache(kSystemPrompt, userPrompt);
}

void AiPanel::runTranslateBatch(TaskResultCard *card, const QString &sourceTrackId,
                                const QString &targetLang, int rangeIndex)
{
    const SubtitleTrack *src = nullptr;
    if (m_project)
        for (const SubtitleTrack &t : m_project->subtitleTracks)
            if (t.id == sourceTrackId) { src = &t; break; }
    if (!src || src->cues.isEmpty()) {
        card->markFailed(arenaTr("panel.ai.translationStale"));
        m_askSend->setEnabled(m_settings.aiEnabled());
        return;
    }
    const QVector<QPair<int, int>> ranges = aifeatures::batchRanges(src->cues.size());
    if (rangeIndex >= ranges.size()) {
        finishTranslation(card, sourceTrackId, targetLang);
        return;
    }
    const int first = ranges[rangeIndex].first;
    const int last = ranges[rangeIndex].second; // exclusive

    QVector<QJsonObject> entries;
    const double fps = m_project->fps;
    for (int i = first; i < last; ++i) {
        QJsonObject o;
        o[QStringLiteral("index")] = i;
        o[QStringLiteral("start")] = framesToTimecode(src->cues[i].startFrame, fps);
        o[QStringLiteral("end")] = framesToTimecode(src->cues[i].endFrame, fps);
        o[QStringLiteral("text")] = src->cues[i].text;
        entries.append(o);
    }
    card->setBatchRange(rangeIndex, first, last, src->cues.size());
    card->setStatus(arenaTr(
        "panel.ai.batch.running",
        {{QStringLiteral("first"), QString::number(first + 1)},
         {QStringLiteral("last"), QString::number(last)},
         {QStringLiteral("n"), QString::number(rangeIndex + 1)},
         {QStringLiteral("batches"), QString::number(ranges.size())}}));

    const QString user = aifeatures::translatePrompt(entries, targetLang);
    m_client->chat(kSystemPrompt, user,
                   [card](const QString &d) { card->appendStream(d); },
                   [this, card, sourceTrackId, targetLang, rangeIndex, first, last, user](
                       const QString &full, const QString &err) {
                       QStringList batch;
                       QString verr;
                       if (err.isEmpty() &&
                           aifeatures::parseTranslation(full, last - first, &batch, &verr)) {
                           QStringList all = card->translations();
                           for (int i = 0; i < batch.size() && first + i < all.size(); ++i)
                               all[first + i] = batch[i];
                           card->setTranslations(all);
                           runTranslateBatch(card, sourceTrackId, targetLang, rangeIndex + 1);
                       } else if (!card->batchRetried(rangeIndex)) {
                           // One retry per batch; valid batches are never re-sent.
                           // Drop the cached (unparseable) response first, or the
                           // retry would be served from cache and fail again.
                           card->setBatchRetried(rangeIndex);
                           invalidateFor(user);
                           runTranslateBatch(card, sourceTrackId, targetLang, rangeIndex);
                       } else {
                           invalidateFor(user); // so a later re-run re-queries
                           card->markFailed(arenaTr(
                               "panel.ai.batch.failed",
                               {{QStringLiteral("n"), QString::number(rangeIndex + 1)}}));
                           m_askSend->setEnabled(m_settings.aiEnabled());
                       }
                   },
                   aifeatures::kTranslateBatchMaxTokens);
}

void AiPanel::finishTranslation(TaskResultCard *card, const QString &sourceTrackId,
                                const QString &targetLang)
{
    Q_UNUSED(sourceTrackId)
    const QStringList texts = card->translations();
    QString p = arenaTr("panel.ai.preview.translate",
                        {{QStringLiteral("count"), QString::number(texts.size())},
                         {QStringLiteral("lang"), targetLang}});
    for (int i = 0; i < texts.size() && i < 12; ++i)
        p += QStringLiteral("\n%1. %2").arg(i + 1).arg(texts[i]);
    if (texts.size() > 12)
        p += QStringLiteral("\n… (%1)").arg(QString::number(texts.size() - 12));
    card->showPreview(p);
}

void AiPanel::onCardAccepted(TaskResultCard *card)
{
    switch (card->taskType()) {
    case AiTaskType::Translate:
        emit translationAccepted(card->sourceTrackId(), card->targetLang(),
                                 card->translations());
        break;
    case AiTaskType::Chapters:
        emit chaptersAccepted(card->chapters());
        break;
    case AiTaskType::Titles:
        emit titleAccepted(card->titleResult());
        break;
    case AiTaskType::Cuts:
        emit cutsAccepted(card->cuts());
        break;
    }
    card->markApplied(QString());
    m_askSend->setEnabled(m_settings.aiEnabled());
}

// --- free-form ask -----------------------------------------------------------

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
