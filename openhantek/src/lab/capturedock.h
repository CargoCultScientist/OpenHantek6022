// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "capture.h"
#include "measurements.h"
#include "mask.h"
#include "measurementlog.h"
#include <QDockWidget>
#include <array>
#include <map>

class QListWidget;
class QLabel;
class QTableWidget;
class QCheckBox;
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class DsoSettings;
namespace Lab {
class CapturePlot;
class TrendPlot;
class MeasurementCard;
class CaptureDock : public QDockWidget {
    Q_OBJECT
public:
    explicit CaptureDock(const DsoSettings *settings, QWidget *parent=nullptr);
    void ingest(const std::shared_ptr<PPresult> &frame);
    bool confirmDiscardLog();
    void setEmbedded();
    QString activitySummary() const;
signals:
    void activityChanged(const QString &summary);
private:
    const DsoSettings *settings;
    CaptureHistory history;
    std::shared_ptr<const Capture> selected, reference;
    // Weak identities keep UI caches from extending the history's memory budget.
    std::deque<std::weak_ptr<const Capture>> listedFrames;
    struct SelectionAnalysis {
        std::weak_ptr<const Capture> capture, reference;
        TimeSpan span;
        bool aligned=false, measured=false, compared=false, hadReference=false;
        double offset=0;
        std::array<Measurements,3> values;
        std::array<Difference,3> differences;
    } selectedAnalysis;
    QString selectedName, referenceName;
    CaptureStatistics statistics;
    MeasurementLog measurementLog;
    quint64 logRevision=0, exportedLogRevision=0;
    bool received=false;
    unsigned lastReceivedTag=0;
    qint64 lastReceivedTime=0;
    QListWidget *list;
    CapturePlot *plot;
    TrendPlot *trend;
    QLabel *status, *selectionBadge, *maskSummary, *logSummary, *logBadge;
    std::array<MeasurementCard*,3> quickMetrics{};
    QPushButton *saveButton, *referenceButton, *clearReferenceButton;
    QTableWidget *measurements;
    QCheckBox *live, *record, *align, *details, *maskEnabled, *freezeOnFailure, *collectLog;
    QDoubleSpinBox *offset, *cursorA, *cursorB;
    QComboBox *spanMode, *statisticMetric, *maskChannel, *toleranceMode, *historyFilter;
    QDoubleSpinBox *maskTolerance;
    QDoubleSpinBox *logInterval;
    QComboBox *logChannel, *trendMetric;
    std::map<std::shared_ptr<const Capture>,MaskResult> maskResults;
    size_t maskPassed=0, maskFailed=0, maskUntestable=0;
    MaskSpec maskSpec() const;
    MaskResult maskResult(const Capture &) const;
    void resetMask();
    TimeSpan measurementSpan() const;
    void resetStatistics();
    void refreshList();
    void refresh();
    void saveCapture();
    void openCapture();
    void openLibrary();
    void clearHistory();
    bool exportLog();
};
}
