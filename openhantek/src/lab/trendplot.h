// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "measurementlog.h"
#include <QWidget>
namespace Lab {
class TrendPlot : public QWidget {
public:
    explicit TrendPlot(const MeasurementLog *log, QWidget *parent=nullptr) : QWidget(parent),log(log) {setMinimumSize(420,180);}
    int metric=0; // Vpp, RMS, frequency, mean
protected:
    void paintEvent(QPaintEvent *) override;
private:
    const MeasurementLog *log;
};
}
