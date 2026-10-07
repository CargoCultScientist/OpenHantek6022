// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "post/ppresult.h"
#include <QJsonObject>
#include <QStringList>
#include <deque>
#include <memory>

class DsoSettings;
namespace Lab {
struct CaptureChannel {
    QString name;
    Unit unit=UNIT_VOLTS;
    SampleValues signal;
    bool valid=true;
};
struct Capture {
    unsigned tag=0;
    qint64 capturedAtMs=0;
    int triggerPosition=0;
    bool triggered=false;
    std::vector<CaptureChannel> channels;
    QJsonObject metadata;
    size_t bytes() const;
    static std::shared_ptr<const Capture> fromResult(const PPresult &, const DsoSettings &);
    bool save(const QString &filename, QString &error) const;
    static std::shared_ptr<const Capture> load(const QString &filename, QString &error);
};
class CaptureHistory {
public:
    explicit CaptureHistory(size_t budget=64*1024*1024, size_t capacity=128) : budget(budget), capacity(capacity) {}
    bool append(std::shared_ptr<const Capture> capture);
    void clear();
    const std::deque<std::shared_ptr<const Capture>> &frames() const { return records; }
    size_t bytes() const { return used; }
    quint64 skippedTags() const { return skipped; }
private:
    size_t budget, capacity, used=0;
    quint64 skipped=0;
    std::deque<std::shared_ptr<const Capture>> records;
};
struct Difference { size_t count=0; double rms=0, maximum=0; QString error; };
// Reference time is shifted by offset. Interpolation is linear and only within
// overlap; no extrapolation or automatic phase fitting hides actual differences.
Difference compare(const Capture &, const Capture &, size_t channel, bool alignTrigger, double offset=0);
double timeOrigin(const Capture &, const CaptureChannel &, bool alignTrigger);
}
