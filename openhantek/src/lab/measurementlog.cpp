// SPDX-License-Identifier: GPL-2.0-or-later
#include "measurementlog.h"
#include <QDateTime>
#include <QIODevice>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>

namespace Lab {
void MeasurementLog::clear() {rows.clear(); context={}; segment=removed=0; seen=false;}
bool MeasurementLog::append(const Capture &capture, const LogOptions &options) {
    if(!capacity || options.channel>=capture.channels.size() || options.intervalMs<0 || capture.capturedAtMs<0 ||
       std::isnan(options.span.start) || std::isnan(options.span.end) || options.span.end<options.span.start) return false;
    if(seen && lastTag==capture.tag && lastSeenMs==capture.capturedAtMs) return false;
    const bool clockReversed=seen && capture.capturedAtMs<lastSeenMs;
    seen=true; lastTag=capture.tag; lastSeenMs=capture.capturedAtMs;
    QJsonObject next{{"setup",captureSetup(capture)},{"channelIndex",int(options.channel)},
        {"gateStartSeconds",std::isfinite(options.span.start)?QJsonValue(options.span.start):QJsonValue()},
        {"gateEndSeconds",std::isfinite(options.span.end)?QJsonValue(options.span.end):QJsonValue()},
        {"triggerAligned",options.alignTrigger},{"minimumIntervalMs",double(options.intervalMs)},
        {"continuity","displayed acquisitions only; gaps possible"}};
    const bool changed=rows.empty() || context!=next || clockReversed;
    if(!changed && options.intervalMs>0 && capture.capturedAtMs-rows.back().capturedAtMs<options.intervalMs) return false;
    if(changed) {++segment; context=std::move(next);}
    const auto &ch=capture.channels[options.channel];
    const double origin=timeOrigin(capture,ch,options.alignTrigger);
    const auto range=sampleRange(ch.signal.samples.size(),ch.signal.interval,origin,options.span);
    MeasurementLogEntry row;
    row.capturedAtMs=capture.capturedAtMs; row.tag=capture.tag; row.segment=segment; row.channel=ch.name; row.unit=ch.unit; row.context=context;
    row.values=measure(ch.signal.samples,ch.signal.interval,ch.valid,range);
    if(row.values.count) {row.start=origin+range.begin*ch.signal.interval; row.end=origin+(range.end-1)*ch.signal.interval;}
    rows.push_back(std::move(row));
    while(rows.size()>capacity) {rows.pop_front(); ++removed;}
    return true;
}
static QString csvText(QString value) {
    // Quote delimiters and prevent channel labels from becoming spreadsheet formulas.
    const auto trimmed=value.trimmed();
    if((!value.isEmpty() && QString("=+-@\t\r\n").contains(value.front())) ||
       (!trimmed.isEmpty() && QString("=+-@").contains(trimmed.front()))) value.prepend('\'');
    value.replace('"',"\"\""); return '"'+value+'"';
}
static QString scalar(double value) {return std::isfinite(value)?QString::number(value,'g',17):QString();}
bool MeasurementLog::writeCsv(QIODevice &device) const {
    auto write=[&](const QString &line){const auto bytes=line.toUtf8(); return device.write(bytes)==bytes.size();};
    if(!write("timestamp_utc,tag,segment,channel,unit,samples,span_start_s,span_end_s,minimum,maximum,vpp,mean,rms,ac_rms,frequency_hz,period_s,duty_positive,width_positive_s,duty_negative,width_negative_s,rise_s,fall_s,cycle_mean,cycle_rms,complete_cycles,clipped,undersampled,irregular,context_json\r\n")) return false;
    for(const auto &row:rows) {
        const auto &m=row.values;
        const QString unit=row.unit==UNIT_VOLTS?"V":row.unit==UNIT_VOLTSQUARE?"V^2":row.unit==UNIT_WATTS?"W":"1";
        const QStringList fields={csvText(QDateTime::fromMSecsSinceEpoch(row.capturedAtMs).toUTC().toString(Qt::ISODateWithMs)),
            QString::number(row.tag),QString::number(row.segment),csvText(row.channel),csvText(unit),QString::number(m.count),
            scalar(row.start),scalar(row.end),scalar(m.minimum),scalar(m.maximum),scalar(m.vpp),scalar(m.mean),scalar(m.rms),scalar(m.acRms),
            scalar(m.frequency),scalar(m.period),scalar(m.duty),scalar(m.positiveWidth),scalar(m.negativeDuty),scalar(m.negativeWidth),
            scalar(m.rise),scalar(m.fall),scalar(m.cycleMean),scalar(m.cycleRms),QString::number(m.cycles),
            m.clipped?"1":"0",m.undersampled?"1":"0",m.irregular?"1":"0",csvText(QString::fromUtf8(QJsonDocument(row.context).toJson(QJsonDocument::Compact)))};
        if(!write(fields.join(',')+"\r\n")) return false;
    }
    return true;
}
bool MeasurementLog::saveCsv(const QString &filename, QString &error) const {
    QSaveFile file(filename);
    if(!file.open(QIODevice::WriteOnly) || !writeCsv(file) || !file.commit()) {error=file.errorString(); return false;}
    return true;
}
}
