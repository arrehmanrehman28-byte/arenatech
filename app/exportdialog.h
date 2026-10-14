#pragma once
// M4 export dialog: pick one or more presets, adjust options (scale, custom
// quality, audio bitrate, hardware encode), choose the output folder, and
// run them through the ExportQueue (worker thread; the UI stays live).
// Every string goes through arenaTr (spec: no hardcoded user-facing text).
#include <QDialog>

#include "exporter.h"
#include "exportqueue.h"
#include "project.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QSlider;

namespace arena {

// Everything the dialog needs from the app, snapshotted on the UI thread so
// the export worker never races with live editing.
struct ExportJobSource {
    Project project;
    Compositor::FrameFetcher fetch;
    Exporter::AudioFetcher audio;
};

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(const ExportJobSource &source, QWidget *parent = nullptr);
    ~ExportDialog() override;

    ExportQueue *queue() { return &m_queue; }
    bool running() const;

    // Programmatic control used by the headless GUI smoke test (drives the
    // same code path as the buttons).
    QListWidget *presetListForTest() const { return m_presetList; }
    QComboBox *scaleComboForTest() const { return m_scaleCombo; }
    QLineEdit *dirEditForTest() const { return m_dirEdit; }
    void startForTest(); // same as the Start button

    // Select a single preset (NL command bar "export" action): unchecks
    // the others and checks this one. Unknown names are ignored.
    void preselectPreset(const QString &presetName);

protected:
    void closeEvent(QCloseEvent *ev) override;

private slots:
    void updateEstimates();
    void onStart();
    void onCancel();
    void onJobStarted(int index);
    void onJobProgress(int index, qint64 done, qint64 total);
    void onJobFinished(int index, bool ok, const QString &error);
    void onQueueFinished();

private:
    void buildUi();
    // Preset settings with the dialog options (scale/quality/bitrate/HW)
    // applied.
    ExportSettings settingsFor(const QString &presetName, bool *availableOut = nullptr) const;
    int rowForJob(int jobIndex) const; // -1 when the row was cleared
    QString fileNameFor(const QString &presetName) const;

    ExportJobSource m_source;
    ExportQueue m_queue;

    // widgets
    QListWidget *m_presetList = nullptr;
    QCheckBox *m_hwCheck = nullptr;
    QComboBox *m_scaleCombo = nullptr;
    QCheckBox *m_customQualityCheck = nullptr;
    QSlider *m_qualitySlider = nullptr;
    QLabel *m_qualityValue = nullptr;
    QComboBox *m_bitrateCombo = nullptr;
    QLineEdit *m_dirEdit = nullptr;
    QLabel *m_totalEstimate = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    // Re-entrancy guard: updating the list items emits itemChanged, which
    // would otherwise re-enter updateEstimates forever.
    bool m_updatingEstimates = false;
};

} // namespace arena
