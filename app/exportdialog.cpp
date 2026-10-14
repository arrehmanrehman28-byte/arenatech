#include "exportdialog.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>
#include <QVBoxLayout>

#include "i18n.h"
#include "util.h"

namespace arena {
namespace {

constexpr int kColPreset = 0;
constexpr int kColFile = 1;
constexpr int kColStatus = 2;
constexpr int kColProgress = 3;
constexpr int kColSize = 4;
constexpr int kColCount = 5;

QString sanitize(const QString &s)
{
    QString out;
    for (const QChar &c : s)
        out.append(c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')
                       ? c
                       : QLatin1Char('-'));
    return out.isEmpty() ? QStringLiteral("project") : out;
}

} // namespace

ExportDialog::ExportDialog(const ExportJobSource &source, QWidget *parent)
    : QDialog(parent), m_source(source)
{
    buildUi();
    updateEstimates();
}

ExportDialog::~ExportDialog() = default;

void ExportDialog::buildUi()
{
    setWindowTitle(arenaTr("export.title"));
    setMinimumWidth(640);
    auto *root = new QVBoxLayout(this);

    // --- presets -------------------------------------------------------------
    auto *presetBox = new QGroupBox(arenaTr("export.presets"), this);
    auto *presetGrid = new QGridLayout(presetBox);
    m_presetList = new QListWidget(presetBox);
    m_presetList->setSelectionMode(QAbstractItemView::NoSelection);
    m_presetList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    int i = 0;
    for (const QString &name : Exporter::presetNames()) {
        auto *item = new QListWidgetItem(name);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        m_presetList->addItem(item);
        ++i;
    }
    presetGrid->addWidget(m_presetList, 0, 0);
    m_totalEstimate = new QLabel(QString(), presetBox);
    presetGrid->addWidget(m_totalEstimate, 1, 0);
    root->addWidget(presetBox);

    // --- options -------------------------------------------------------------
    auto *optBox = new QGroupBox(arenaTr("export.options"), this);
    auto *optForm = new QFormLayout(optBox);

    m_scaleCombo = new QComboBox(optBox);
    for (int pct : {25, 50, 75, 100, 150, 200}) {
        m_scaleCombo->addItem(QStringLiteral("%1%").arg(pct), pct);
    }
    m_scaleCombo->setCurrentIndex(3); // 100%
    optForm->addRow(arenaTr("export.scale"), m_scaleCombo);

    m_hwCheck = new QCheckBox(arenaTr("export.hwEncode"), optBox);
    m_hwCheck->setChecked(true);
    optForm->addRow(QString(), m_hwCheck);

    auto *qualityRow = new QWidget(optBox);
    auto *qualityLayout = new QHBoxLayout(qualityRow);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    m_customQualityCheck = new QCheckBox(arenaTr("export.customQuality"), qualityRow);
    m_qualitySlider = new QSlider(Qt::Horizontal, qualityRow);
    m_qualitySlider->setRange(0, 100);
    m_qualitySlider->setValue(50);
    m_qualitySlider->setEnabled(false);
    m_qualityValue = new QLabel(QStringLiteral("—"), qualityRow);
    m_qualityValue->setMinimumWidth(24);
    qualityLayout->addWidget(m_customQualityCheck);
    qualityLayout->addWidget(m_qualitySlider, 1);
    qualityLayout->addWidget(m_qualityValue);
    optForm->addRow(arenaTr("export.quality"), qualityRow);

    m_bitrateCombo = new QComboBox(optBox);
    m_bitrateCombo->addItem(arenaTr("export.audioBitratePreset"), -1);
    for (int kbps : {96, 128, 192, 256, 320})
        m_bitrateCombo->addItem(QStringLiteral("%1 kbps").arg(kbps), kbps * 1000);
    optForm->addRow(arenaTr("export.audioBitrate"), m_bitrateCombo);

    root->addWidget(optBox);

    connect(m_customQualityCheck, &QCheckBox::toggled, m_qualitySlider, &QSlider::setEnabled);
    connect(m_customQualityCheck, &QCheckBox::toggled, this, &ExportDialog::updateEstimates);
    connect(m_qualitySlider, &QSlider::valueChanged, this, [this](int) { updateEstimates(); });
    connect(m_scaleCombo, &QComboBox::currentIndexChanged, this, &ExportDialog::updateEstimates);
    connect(m_bitrateCombo, &QComboBox::currentIndexChanged, this, &ExportDialog::updateEstimates);

    // --- output ---------------------------------------------------------------
    auto *outBox = new QGroupBox(arenaTr("export.output"), this);
    auto *outRow = new QHBoxLayout(outBox);
    m_dirEdit = new QLineEdit(outBox);
    const QString projDir =
        m_source.project.filePath.isEmpty() ? QString() : QFileInfo(m_source.project.filePath).absolutePath();
    m_dirEdit->setText(projDir.isEmpty()
                           ? QDir::homePath() + QStringLiteral("/Videos")
                           : projDir);
    auto *browseBtn = new QPushButton(arenaTr("export.browse"), outBox);
    outRow->addWidget(m_dirEdit, 1);
    outRow->addWidget(browseBtn);
    root->addWidget(outBox);

    // --- queue table ------------------------------------------------------------
    auto *queueBox = new QGroupBox(arenaTr("export.queue"), this);
    auto *queueLayout = new QVBoxLayout(queueBox);
    m_table = new QTableWidget(0, kColCount, queueBox);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int c = 0; c < kColCount; ++c) {
        m_table->setColumnWidth(c, 100);
    }
    QStringList headers;
    headers << arenaTr("export.col.preset") << arenaTr("export.col.file")
            << arenaTr("export.col.status") << arenaTr("export.col.progress")
            << arenaTr("export.col.size");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    queueLayout->addWidget(m_table);

    auto *btnRow = new QHBoxLayout();
    m_startBtn = new QPushButton(arenaTr("export.start"), queueBox);
    m_cancelBtn = new QPushButton(arenaTr("export.cancel"), queueBox);
    m_cancelBtn->setEnabled(false);
    auto *closeBtn = new QPushButton(arenaTr("dlg.close"), queueBox);
    btnRow->addStretch(1);
    btnRow->addWidget(m_startBtn);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(closeBtn);
    queueLayout->addLayout(btnRow);
    root->addWidget(queueBox, 1);

    connect(browseBtn, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, arenaTr("export.browse"), m_dirEdit->text());
        if (!dir.isEmpty())
            m_dirEdit->setText(dir);
    });
    connect(m_presetList, &QListWidget::itemChanged, this, &ExportDialog::updateEstimates);
    connect(m_startBtn, &QPushButton::clicked, this, &ExportDialog::onStart);
    connect(m_cancelBtn, &QPushButton::clicked, this, &ExportDialog::onCancel);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    connect(&m_queue, &ExportQueue::jobStarted, this, &ExportDialog::onJobStarted);
    connect(&m_queue, &ExportQueue::jobProgress, this, &ExportDialog::onJobProgress);
    connect(&m_queue, &ExportQueue::jobFinished, this, &ExportDialog::onJobFinished);
    connect(&m_queue, &ExportQueue::queueFinished, this, &ExportDialog::onQueueFinished);
}

ExportSettings ExportDialog::settingsFor(const QString &presetName, bool *availableOut) const
{
    ExportSettings s = Exporter::preset(presetName, m_source.project);
    s.scalePercent = m_scaleCombo->currentData().toInt();
    s.hwEncode = m_hwCheck->isChecked();
    const int bitrate = m_bitrateCombo->currentData().toInt();
    if (bitrate > 0)
        s.audioBitrate = double(bitrate);
    if (m_customQualityCheck->isChecked()) {
        // Slider 0..100 -> CRF 51..1 (higher slider = higher quality).
        const int q = m_qualitySlider->value();
        s.crf = 51.0 - 50.0 * double(q) / 100.0;
    }
    if (availableOut)
        *availableOut = !s.videoCodec.isEmpty();
    return s;
}

QString ExportDialog::fileNameFor(const QString &presetName) const
{
    const ExportSettings s = settingsFor(presetName);
    const QString ext = s.container == "webm" ? QStringLiteral("webm") : QStringLiteral("mp4");
    return QStringLiteral("%1-%2.%3")
        .arg(sanitize(m_source.project.name), sanitize(presetName), ext);
}

void ExportDialog::updateEstimates()
{
    if (m_updatingEstimates)
        return;
    m_updatingEstimates = true;
    qint64 totalBytes = 0;
    double totalSeconds = 0.0;
    int checked = 0;
    for (int i = 0; i < m_presetList->count(); ++i) {
        QListWidgetItem *item = m_presetList->item(i);
        const QString name = item->data(Qt::UserRole).toString().isEmpty()
                                 ? item->text()
                                 : item->data(Qt::UserRole).toString();
        if (item->checkState() != Qt::Checked) {
            item->setText(name);
            item->setToolTip(QString());
            continue;
        }
        ++checked;
        const ExportSettings s = settingsFor(name);
        const ExportEstimate est = Exporter::estimate(s, m_source.project);
        totalBytes += est.totalBytes();
        totalSeconds += est.estEncodeSeconds;
        const QString estText = arenaTr("export.estimateRow",
                                        {{QStringLiteral("size"), formatBytes(est.totalBytes())},
                                         {QStringLiteral("time"),
                                          QString::number(est.estEncodeSeconds, 'g', 2)}});
        item->setData(Qt::UserRole, name);
        item->setText(name + QStringLiteral("     ") + estText);
        item->setToolTip(estText);
    }
    if (checked > 0) {
        m_totalEstimate->setText(
            arenaTr("export.totalEstimate",
                    {{QStringLiteral("size"), formatBytes(totalBytes)},
                     {QStringLiteral("time"), QString::number(totalSeconds, 'g', 2)}}));
    } else {
        m_totalEstimate->setText(QString());
    }
    // Show the CRF the slider would produce (informational; presets' CRF
    // applies until custom quality is ticked).
    if (m_customQualityCheck->isChecked())
        m_qualityValue->setText(QString::number(51.0 - 50.0 * double(m_qualitySlider->value()) / 100.0,
                                                'g', 3));
    else
        m_qualityValue->setText(QStringLiteral("—"));
    m_updatingEstimates = false;
}

void ExportDialog::preselectPreset(const QString &presetName)
{
    if (presetName.isEmpty() || !m_presetList) return;
    for (int i = 0; i < m_presetList->count(); ++i) {
        auto *item = m_presetList->item(i);
        const bool isTarget = item->text() == presetName;
        item->setCheckState(isTarget ? Qt::Checked : Qt::Unchecked);
    }
    updateEstimates();
}

void ExportDialog::startForTest()
{
    onStart();
}

void ExportDialog::onCancel()
{
    m_queue.cancelCurrent(); // the running job stops at its next frame boundary
}

void ExportDialog::onStart()
{
    if (m_queue.isRunning())
        return;
    const QString dir = m_dirEdit->text().trimmed();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        QMessageBox::warning(this, arenaTr("export.title"),
                             arenaTr("export.dirError"));
        return;
    }
    QStringList names;
    for (int i = 0; i < m_presetList->count(); ++i) {
        QListWidgetItem *item = m_presetList->item(i);
        if (item->checkState() == Qt::Checked)
            names.append(item->text());
    }
    if (names.isEmpty()) {
        QMessageBox::information(this, arenaTr("export.title"),
                                 arenaTr("export.noSelection"));
        return;
    }
    // One row per job; remember the row to update it from queue signals.
    for (const QString &name : names) {
        const ExportSettings s = settingsFor(name);
        const QString out = dir + QStringLiteral("/") + fileNameFor(name);
        ExportQueue::Job job;
        job.name = name;
        job.settings = s;
        job.settings.outputPath = out;
        job.project = m_source.project;
        job.fetch = m_source.fetch;
        job.audio = m_source.audio;
        const int jobIndex = m_queue.addJob(std::move(job));
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *presetItem = new QTableWidgetItem(name);
        presetItem->setData(Qt::UserRole, jobIndex); // maps row -> queue job
        m_table->setItem(row, kColPreset, presetItem);
        m_table->setItem(row, kColFile,
                         new QTableWidgetItem(QFileInfo(out).fileName()));
        m_table->setItem(row, kColStatus,
                         new QTableWidgetItem(arenaTr("export.status.queued")));
        auto *bar = new QProgressBar();
        bar->setRange(0, 100);
        bar->setValue(0);
        m_table->setCellWidget(row, kColProgress, bar);
        m_table->setItem(row, kColSize, new QTableWidgetItem(QStringLiteral("—")));
    }
    m_startBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
    m_queue.start();
}

int ExportDialog::rowForJob(int jobIndex) const
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (const QTableWidgetItem *item = m_table->item(row, kColPreset);
            item && item->data(Qt::UserRole).toInt() == jobIndex)
            return row;
    }
    return -1;
}

void ExportDialog::onJobStarted(int jobIndex)
{
    const int row = rowForJob(jobIndex);
    if (row >= 0)
        m_table->item(row, kColStatus)->setText(arenaTr("export.status.running"));
}

void ExportDialog::onJobProgress(int jobIndex, qint64 done, qint64 total)
{
    const int row = rowForJob(jobIndex);
    if (row < 0)
        return;
    auto *bar = qobject_cast<QProgressBar *>(m_table->cellWidget(row, kColProgress));
    if (bar && total > 0)
        bar->setValue(int(100.0 * double(done) / double(total)));
}

void ExportDialog::onJobFinished(int jobIndex, bool ok, const QString &error)
{
    const int row = rowForJob(jobIndex);
    if (row < 0)
        return;
    ExportQueue::Job job = m_queue.job(jobIndex);
    QString status;
    if (ok)
        status = arenaTr("export.status.done");
    else if (job.state == ExportQueue::Job::State::Cancelled)
        status = arenaTr("export.status.cancelled");
    else
        status = arenaTr("export.status.failed");
    auto *item = m_table->item(row, kColStatus);
    if (item) {
        item->setText(status);
        if (!error.isEmpty())
            item->setToolTip(error);
    }
    auto *bar = qobject_cast<QProgressBar *>(m_table->cellWidget(row, kColProgress));
    if (bar)
        bar->setValue(ok ? 100 : 0);
    if (ok) {
        const QFileInfo fi(job.settings.outputPath);
        m_table->item(row, kColSize)->setText(formatBytes(fi.size()));
    }
}

void ExportDialog::onQueueFinished()
{
    m_startBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
}

bool ExportDialog::running() const
{
    return m_queue.isRunning();
}

void ExportDialog::closeEvent(QCloseEvent *ev)
{
    if (running()) {
        auto box = QMessageBox(this);
        box.setIcon(QMessageBox::Question);
        box.setText(arenaTr("export.runningTitle"));
        box.setInformativeText(arenaTr("export.runningBody"));
        auto *yes = box.addButton(arenaTr("dlg.yes"), QMessageBox::YesRole);
        box.addButton(arenaTr("dlg.no"), QMessageBox::NoRole);
        box.exec();
        if (box.clickedButton() != yes) {
            ev->ignore();
            return;
        }
        m_queue.cancelCurrent();
    }
    ev->accept();
}

} // namespace arena
