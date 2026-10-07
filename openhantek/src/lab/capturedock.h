// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "capture.h"
#include "measurements.h"
#include <QDockWidget>
#include <array>

class QListWidget;
class QLabel;
class QTableWidget;
class QCheckBox;
class QDoubleSpinBox;
class DsoSettings;
namespace Lab {
class CapturePlot;
class CaptureDock : public QDockWidget {
    Q_OBJECT
public:
    explicit CaptureDock(const DsoSettings *settings, QWidget *parent=nullptr);
    void ingest(const std::shared_ptr<PPresult> &frame);
private:
    const DsoSettings *settings;
    CaptureHistory history;
    std::shared_ptr<const Capture> selected, reference;
    std::array<RunningStatistic,3> vppStatistics;
    QListWidget *list;
    CapturePlot *plot;
    QLabel *status;
    QTableWidget *measurements;
    QCheckBox *live, *record, *align;
    QDoubleSpinBox *offset;
    void refreshList();
    void refresh();
    void saveCapture();
    void openCapture();
};
}
