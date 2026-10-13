#pragma once
// M4 export queue: runs Exporter::exportProject jobs (blocking) on a worker
// thread, one at a time, so the UI never freezes (spec). The queue is
// QtCore-only (no widgets) so it is unit-testable headless.
//
// Thread model: jobs are added on the UI thread before start(). At start()
// the queue deep-copies the pending job data into the worker; afterwards the
// worker never touches the job list. All state changes flow back to the UI
// thread as queued signals, which is also where Job::state is updated.
#include <QAtomicInt>
#include <QList>
#include <QObject>
#include <QString>
#include <QThread>

#include "exporter.h"
#include "project.h"

namespace arena {

class ExportQueue : public QObject {
    Q_OBJECT
public:
    struct Job {
        enum class State { Queued, Running, Done, Failed, Cancelled };
        QString name;                 // display name (usually the preset name)
        ExportSettings settings;      // full settings incl. output path
        Project project;              // snapshot; never mutated after addJob
        Compositor::FrameFetcher fetch; // thread-safe (stateless decoders)
        Exporter::AudioFetcher audio; // may be null (silent export)
        State state = State::Queued;
        qint64 framesDone = 0;
        qint64 framesTotal = 0;
        QString error;
    };

    explicit ExportQueue(QObject *parent = nullptr);
    ~ExportQueue() override;

    // Adds a job in the Queued state; returns its index. Add while running
    // to queue it for the next start().
    int addJob(Job job);
    int jobCount() const;
    Job job(int i) const;
    // Removes Done/Failed/Cancelled jobs (never the running one); returns the
    // new index of the job that was at `firstKept` (-1 if removed).
    QList<int> clearFinished();
    bool isRunning() const;
    bool hasPending() const;
    void start();
    void cancelCurrent();

    double progress() const; // 0..1 over the finished + running jobs

signals:
    void jobStarted(int index);
    void jobProgress(int index, qint64 framesDone, qint64 framesTotal);
    void jobFinished(int index, bool ok, const QString &error);
    void queueFinished();

private slots:
    void onWorkerStarted(int index);
    void onWorkerProgress(int index, qint64 done, qint64 total);
    void onWorkerFinished(int index, bool ok, const QString &error);
    void onWorkerDone();

private:
    QList<Job> m_jobs;
    QThread *m_worker = nullptr;
};

} // namespace arena
