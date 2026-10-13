#include "exportqueue.h"

#include <QMetaType>

namespace arena {
namespace {

// Deep copy of a job plus the queue index the worker reports back.
struct WorkItem {
    int index;
    ExportQueue::Job data;
};

// Worker thread: owns deep copies of the pending jobs (no shared mutable
// state with the UI thread) and runs them one after another. Signals cross
// to the UI thread as queued connections; the queue updates Job state there.
class ExportWorker : public QThread {
    Q_OBJECT
public:
    explicit ExportWorker(QList<WorkItem> items) : m_items(std::move(items)) {}

    void cancel() { m_cancel = 1; }

signals:
    void started(int index);
    void progress(int index, qint64 framesDone, qint64 framesTotal);
    void finished(int index, bool ok, const QString error);
    void allDone();

protected:
    void run() override
    {
        for (const WorkItem &item : m_items) {
            const int idx = item.index;
            emit started(idx);
            m_cancel = 0; // a cancel between jobs only applies to the running one
            const ExportJobResult r = Exporter::exportProject(
                item.data.project, item.data.settings, item.data.fetch, item.data.audio,
                [this, idx](qint64 done, qint64 total) { emit progress(idx, done, total); },
                [this]() { return m_cancel.loadRelaxed() != 0; });
            if (r.cancelled)
                emit finished(idx, false, QString());
            else
                emit finished(idx, r.ok, r.error);
        }
        emit allDone();
    }

private:
    QList<WorkItem> m_items;
    QAtomicInt m_cancel{0};
};

} // namespace

ExportQueue::ExportQueue(QObject *parent) : QObject(parent) {}

ExportQueue::~ExportQueue()
{
    cancelCurrent();
    if (m_worker) {
        m_worker->wait(10000); // cancelled worker exits after the current frame
        // A QThread object may only be deleted after its thread has fully
        // exited; deleting a still-running QThread is a hard error.
        if (!m_worker->isRunning())
            m_worker->deleteLater();
        m_worker = nullptr;
    }
}

int ExportQueue::addJob(Job job)
{
    m_jobs.append(std::move(job));
    return m_jobs.size() - 1;
}

int ExportQueue::jobCount() const
{
    return m_jobs.size();
}

ExportQueue::Job ExportQueue::job(int i) const
{
    return m_jobs.value(i);
}

QList<int> ExportQueue::clearFinished()
{
    QList<int> newIndices;
    QList<Job> kept;
    for (int i = 0; i < m_jobs.size(); ++i) {
        const Job &j = m_jobs[i];
        const bool finished =
            j.state == Job::State::Done || j.state == Job::State::Failed
            || j.state == Job::State::Cancelled;
        if (finished)
            continue;
        newIndices.append(i);
        kept.append(j);
    }
    if (newIndices.size() != m_jobs.size())
        m_jobs = std::move(kept);
    return newIndices;
}

bool ExportQueue::isRunning() const
{
    return m_worker && m_worker->isRunning();
}

bool ExportQueue::hasPending() const
{
    for (const Job &j : m_jobs)
        if (j.state == Job::State::Queued)
            return true;
    return false;
}

double ExportQueue::progress() const
{
    if (m_jobs.isEmpty())
        return 0.0;
    double done = 0.0;
    double total = 0.0;
    for (const Job &j : m_jobs) {
        total += 1.0;
        switch (j.state) {
        case Job::State::Done:
        case Job::State::Failed:
        case Job::State::Cancelled:
            done += 1.0;
            break;
        case Job::State::Running:
            done += (j.framesTotal > 0) ? double(j.framesDone) / double(j.framesTotal) : 0.0;
            break;
        case Job::State::Queued:
            break;
        }
    }
    return total > 0.0 ? done / total : 0.0;
}

void ExportQueue::start()
{
    if (isRunning())
        return;
    QList<WorkItem> items;
    for (int i = 0; i < m_jobs.size(); ++i)
        if (m_jobs[i].state == Job::State::Queued)
            items.append(WorkItem{i, m_jobs[i]});
    if (items.isEmpty())
        return;
    auto *worker = new ExportWorker(std::move(items));
    m_worker = worker;
    connect(worker, &ExportWorker::started, this, &ExportQueue::onWorkerStarted,
            Qt::QueuedConnection);
    connect(worker, &ExportWorker::progress, this, &ExportQueue::onWorkerProgress,
            Qt::QueuedConnection);
    connect(worker, &ExportWorker::finished, this, &ExportQueue::onWorkerFinished,
            Qt::QueuedConnection);
    connect(worker, &ExportWorker::allDone, this, &ExportQueue::onWorkerDone,
            Qt::QueuedConnection);
    m_worker->start();
}

void ExportQueue::cancelCurrent()
{
    if (auto *w = qobject_cast<ExportWorker *>(m_worker))
        w->cancel();
}

void ExportQueue::onWorkerStarted(int index)
{
    if (index >= 0 && index < m_jobs.size())
        m_jobs[index].state = Job::State::Running;
    emit jobStarted(index);
}

void ExportQueue::onWorkerProgress(int index, qint64 done, qint64 total)
{
    if (index < 0 || index >= m_jobs.size())
        return;
    Job &j = m_jobs[index];
    if (j.state != Job::State::Running)
        j.state = Job::State::Running;
    j.framesDone = done;
    j.framesTotal = total;
    emit jobProgress(index, done, total);
}

void ExportQueue::onWorkerFinished(int index, bool ok, const QString &error)
{
    if (index >= 0 && index < m_jobs.size()) {
        Job &j = m_jobs[index];
        j.state = ok ? Job::State::Done : (error.isEmpty() ? Job::State::Cancelled
                                                           : Job::State::Failed);
        j.error = error;
    }
    emit jobFinished(index, ok, error);
}

void ExportQueue::onWorkerDone()
{
    if (m_worker) {
        // allDone is the last signal run() emits; the worker thread is still
        // unwinding when this queued handler runs. Wait for full exit before
        // deleting — destroying a still-running QThread is a hard error.
        m_worker->wait();
        m_worker->deleteLater();
        m_worker = nullptr;
    }
    emit queueFinished();
}

} // namespace arena

#include "exportqueue.moc"
