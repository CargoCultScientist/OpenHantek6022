// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "capture.h"
class QIODevice;
namespace Lab {
struct LogOptions {
    size_t channel=0;
    TimeSpan span;
    bool alignTrigger=false;
    qint64 intervalMs=0;
};
struct MeasurementLogEntry {
    qint64 capturedAtMs=0;
    unsigned tag=0;
    quint64 segment=0;
    QString channel;
    Unit unit=UNIT_VOLTS;
    double start=unavailable, end=unavailable;
    Measurements values;
    QJsonObject context;
};
// RAM-only bounded session log. Export is explicit and atomic; not a crash-safe
// recorder. A new setup/gate/channel/clock epoch starts a distinct trend segment.
class MeasurementLog {
public:
    explicit MeasurementLog(size_t capacity=10000) : capacity(capacity) {}
    bool append(const Capture &, const LogOptions &);
    void clear();
    void breakSegment() {context={};}
    const std::deque<MeasurementLogEntry> &entries() const {return rows;}
    quint64 evicted() const {return removed;}
    bool writeCsv(QIODevice &) const;
    bool saveCsv(const QString &filename, QString &error) const;
private:
    size_t capacity;
    std::deque<MeasurementLogEntry> rows;
    QJsonObject context;
    quint64 segment=0, removed=0;
    bool seen=false;
    unsigned lastTag=0;
    qint64 lastSeenMs=0;
};
}
