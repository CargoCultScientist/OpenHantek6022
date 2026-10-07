// SPDX-License-Identifier: GPL-2.0-or-later
#include "capture.h"
#include "dsosettings.h"
#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

namespace Lab {
static constexpr size_t maxSamples=1000000; // 8 MiB of samples per record
static constexpr qint64 maxFileBytes=32*1024*1024;
size_t Capture::bytes() const {
    size_t size=sizeof(Capture)+size_t(QJsonDocument(metadata).toJson().size());
    for(const auto &ch:channels) size+=sizeof(ch)+ch.signal.samples.size()*sizeof(double)+size_t(ch.name.size())*2;
    return size;
}
std::shared_ptr<const Capture> Capture::fromResult(const PPresult &frame, const DsoSettings &settings) {
    if(frame.channelCount()>3) return {};
    auto capture=std::make_shared<Capture>();
    capture->tag=frame.tag; capture->capturedAtMs=frame.capturedAtMs;
    capture->triggerPosition=frame.triggeredPosition; capture->triggered=frame.triggeredPosition>0;
    QJsonArray channelsMeta;
    size_t total=0;
    for(unsigned i=0;i<frame.channelCount();++i) {
        const auto &ch=*frame.data(i);
        if(i>=settings.scope.voltage.size()) return {};
        const auto &v=settings.scope.voltage[i];
        total+=ch.voltage.samples.size();
        if(total>maxSamples) return {};
        capture->channels.push_back({v.name,ch.voltageUnit,ch.voltage,ch.valid});
        channelsMeta.append(QJsonObject{{"probeAttenuation",v.probeAttn},{"gainIndex",int(v.gainStepIndex)},
            {"couplingOrMathIndex",int(v.couplingOrMathIndex)},{"inverted",v.inverted},
            {"displayOffsetDiv",v.offset},{"triggerLevel",v.trigger}});
    }
    if(!total) return {};
    capture->metadata=QJsonObject{{"deviceModel",settings.deviceName},{"deviceSerial",settings.deviceID},
        {"deviceFirmware",int(settings.deviceFW)},{"channelSettings",channelsMeta},
        {"triggerSource",settings.scope.trigger.source},{"triggerMode",int(settings.scope.trigger.mode)},
        {"triggerSlope",int(settings.scope.trigger.slope)},
        {"calibration","calibrated values; factory/local coefficients not embedded"},
        {"settingsProvenance","UI state at receipt; not an atomic acquisition-settings snapshot"},
        {"continuity","displayed acquisitions only; gaps possible"}};
    return capture;
}
bool Capture::save(const QString &filename, QString &error) const {
    QJsonArray channelArray;
    size_t total=0;
    for(const auto &ch:channels) {
        total+=ch.signal.samples.size();
        if(total>maxSamples) {error="Capture exceeds the sample limit"; return false;}
        QJsonArray samples;
        for(double value:ch.signal.samples) {
            if(!std::isfinite(value)) {error="Cannot save non-finite samples"; return false;}
            samples.append(value);
        }
        channelArray.append(QJsonObject{{"name",ch.name},{"unit",int(ch.unit)},
            {"interval",ch.signal.interval},{"valid",ch.valid},{"samples",samples}});
    }
    QJsonObject document{{"format","OpenHantekLabCapture"},{"version",1},{"tag",double(tag)},
        {"capturedAtMs",QString::number(capturedAtMs)},{"triggerPosition",triggerPosition},
        {"triggered",triggered},{"channels",channelArray},{"metadata",metadata}};
    const auto bytes=QJsonDocument(document).toJson(QJsonDocument::Compact);
    QSaveFile file(filename);
    if(bytes.size()>maxFileBytes) {error="Capture exceeds the file-size limit"; return false;}
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()) {
        error=file.errorString(); return false;
    }
    return true;
}
std::shared_ptr<const Capture> Capture::load(const QString &filename, QString &error) {
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly)) {error=file.errorString(); return {};}
    if(file.size()>maxFileBytes) {error="Capture exceeds the 32 MiB file-size limit"; return {};}
    const auto contents=file.read(maxFileBytes+1);
    QJsonParseError parse;
    const auto document=QJsonDocument::fromJson(contents,&parse);
    auto fail=[&error](const QString &reason)->std::shared_ptr<const Capture>{error=reason; return {};};
    if(contents.size()>maxFileBytes || parse.error!=QJsonParseError::NoError || !document.isObject())
        return fail("Invalid capture JSON");
    const auto root=document.object();
    if(root["format"]!="OpenHantekLabCapture"||root["version"]!=1) return fail("Unsupported capture format or version");
    if(!root["channels"].isArray()) return fail("Missing channels");
    const auto channels=root["channels"].toArray();
    if(channels.isEmpty()||channels.size()>3) return fail("Expected one to three channels");
    auto capture=std::make_shared<Capture>();
    const double tag=root["tag"].toDouble(-1);
    if(tag<0 || tag>UINT32_MAX || std::floor(tag)!=tag) return fail("Invalid acquisition tag");
    capture->tag=unsigned(tag);
    bool timestampOk=false;
    capture->capturedAtMs=root["capturedAtMs"].toString().toLongLong(&timestampOk);
    if(!timestampOk || capture->capturedAtMs<0) return fail("Invalid capture timestamp");
    capture->triggerPosition=root["triggerPosition"].toInt(-1);
    if(capture->triggerPosition<0) return fail("Invalid trigger position");
    capture->triggered=root["triggered"].toBool();
    capture->metadata=root["metadata"].toObject();
    size_t total=0;
    for(const auto &value:channels) {
        const auto object=value.toObject();
        CaptureChannel ch;
        ch.name=object["name"].toString();
        const int unit=object["unit"].toInt(-1);
        if(unit!=UNIT_NONE && unit!=UNIT_VOLTS && unit!=UNIT_VOLTSQUARE && unit!=UNIT_WATTS) return fail("Unsupported channel unit");
        ch.unit=Unit(unit); ch.valid=object["valid"].toBool(false);
        ch.signal.interval=object["interval"].toDouble(-1);
        if(!object["samples"].isArray()||ch.name.size()>256) return fail("Invalid channel metadata");
        const auto samples=object["samples"].toArray();
        total+=size_t(samples.size());
        if(total>maxSamples) return fail("Capture exceeds one million samples");
        if(!samples.isEmpty() && (!(ch.signal.interval>0)||!std::isfinite(ch.signal.interval*double(samples.size()))))
            return fail("Invalid sample interval");
        if(!samples.isEmpty() && capture->triggerPosition>=samples.size()) return fail("Trigger outside the record");
        ch.signal.samples.reserve(size_t(samples.size()));
        for(const auto &sample:samples) {
            if(!sample.isDouble()||!std::isfinite(sample.toDouble())) return fail("Invalid sample value");
            ch.signal.samples.push_back(sample.toDouble());
        }
        capture->channels.push_back(std::move(ch));
    }
    if(!total) return fail("Empty capture");
    return capture;
}
bool CaptureHistory::append(std::shared_ptr<const Capture> frame) {
    if(!frame || !capacity || frame->bytes()>budget) return false;
    if(!records.empty()) {
        const auto &last=*records.back();
        if(frame->tag==last.tag && frame->capturedAtMs==last.capturedAtMs) return false;
        const unsigned gap=frame->tag-last.tag;
        if(gap>1 && gap<UINT32_MAX/2) skipped+=gap-1;
    }
    used+=frame->bytes(); records.push_back(std::move(frame));
    while(used>budget || records.size()>capacity) {used-=records.front()->bytes(); records.pop_front();}
    return true;
}
void CaptureHistory::clear() {records.clear(); used=0; skipped=0;}
double timeOrigin(const Capture &capture, const CaptureChannel &ch, bool alignTrigger) {
    return alignTrigger && capture.triggered ? -capture.triggerPosition*ch.signal.interval : 0;
}
Difference compare(const Capture &current, const Capture &reference, size_t index, bool align, double offset, TimeSpan span) {
    Difference d;
    if(index>=current.channels.size()||index>=reference.channels.size()) {d.error="Missing reference channel"; return d;}
    const auto &a=current.channels[index]; const auto &b=reference.channels[index];
    if(a.unit!=b.unit) {d.error="Incompatible units"; return d;}
    if(a.signal.samples.empty()||b.signal.samples.size()<2 || !(a.signal.interval>0) || !(b.signal.interval>0)) {
        d.error="No samples"; return d;
    }
    if(align && (!current.triggered||!reference.triggered)) {d.error="Both records must have a trigger"; return d;}
    const double a0=timeOrigin(current,a,align), b0=timeOrigin(reference,b,align)+offset;
    const auto range=sampleRange(a.signal.samples.size(),a.signal.interval,a0,span);
    if(range.begin==range.end) {d.error="No samples in measurement span"; return d;}
    long double squares=0;
    for(size_t i=range.begin;i<range.end;++i) {
        double position=(a0+i*a.signal.interval-b0)/b.signal.interval;
        if(std::abs(position-std::round(position))<1e-9) position=std::round(position);
        if(!std::isfinite(position)||position<0||position>double(b.signal.samples.size()-1)) continue;
        const size_t left=std::min(size_t(position),b.signal.samples.size()-2);
        const double fraction=position-left;
        const double delta=a.signal.samples[i]-(b.signal.samples[left]*(1-fraction)+b.signal.samples[left+1]*fraction);
        if(!std::isfinite(delta)) continue;
        squares+=static_cast<long double>(delta)*delta; d.maximum=std::max(d.maximum,std::abs(delta)); ++d.count;
    }
    if(!d.count) d.error="No time overlap";
    else d.rms=std::sqrt(double(squares/d.count));
    return d;
}
static QJsonObject statisticsSetup(const Capture &capture) {
    QJsonArray channels;
    for(const auto &ch:capture.channels)
        channels.append(QJsonObject{{"unit",int(ch.unit)},{"interval",ch.signal.interval},
            {"count",double(ch.signal.samples.size())},{"name",ch.name}});
    // Include every channel's settings: math can depend on both physical inputs.
    // Metadata is explicitly receipt-time context, not atomic hardware provenance.
    return {{"channels",channels},{"metadata",capture.metadata}};
}
bool CaptureStatistics::matches(const Capture &capture, TimeSpan span, bool align) const {
    return initialized && setup==statisticsSetup(capture) && gate.start==span.start && gate.end==span.end && aligned==align;
}
void CaptureStatistics::clear() {channels={}; setup={}; initialized=false;}
void CaptureStatistics::add(const Capture &capture, TimeSpan span, bool align) {
    if(initialized && capture.tag==lastTag && capture.capturedAtMs==lastTime) return;
    if(!matches(capture,span,align)) clear();
    setup=statisticsSetup(capture); gate=span; aligned=align; initialized=true;
    lastTag=capture.tag; lastTime=capture.capturedAtMs;
    for(size_t c=0;c<std::min(channels.size(),capture.channels.size());++c) {
        const auto &ch=capture.channels[c];
        if(!ch.valid) continue;
        const auto range=sampleRange(ch.signal.samples.size(),ch.signal.interval,timeOrigin(capture,ch,align),span);
        const auto m=measure(ch.signal.samples,ch.signal.interval,true,range);
        channels[c].vpp.add(m.vpp); channels[c].rms.add(m.rms); channels[c].frequency.add(m.frequency);
    }
}
}
