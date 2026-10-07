// SPDX-License-Identifier: GPL-2.0-or-later
#include "capturelibrary.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace Lab {
namespace {
constexpr qint64 manifestLimit=64*1024, captureLimit=32*1024*1024;
QByteArray digest(const QByteArray &bytes) {return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex();}
bool validId(const QString &id) {
    if(!id.startsWith("capture-")) return false;
    const auto uuid=QUuid(id.mid(8));
    return !uuid.isNull() && id.mid(8)==uuid.toString(QUuid::WithoutBraces);
}
bool readFile(const QString &path,qint64 limit,QByteArray &bytes,QString &error) {
    const QFileInfo info(path);
    if(info.isSymLink() || !info.isFile() || info.size()>limit) {error="Missing, linked or oversized file"; return false;}
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) {error=file.errorString(); return false;}
    bytes=file.read(limit+1);
    if(bytes.size()>limit || file.error()!=QFileDevice::NoError) {error="Could not read a complete bounded file"; return false;}
    return true;
}
bool writeManifest(const QString &path,const QJsonObject &object,QString &error) {
    const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Indented);
    if(bytes.size()>manifestLimit) {error="Library metadata exceeds 64 KiB"; return false;}
    QSaveFile file(path); // Keep atomic replacement; never fall back to direct writes.
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {error=file.errorString(); return false;}
    return true;
}
void annotate(QJsonObject &object,const CaptureAnnotation &annotation) {
    object["name"]=annotation.name; object["notes"]=annotation.notes;
    object["tags"]=QJsonArray::fromStringList(annotation.tags);
}
bool integer(const QJsonValue &value,double maximum) {
    return value.isDouble() && value.toDouble()>=0 && value.toDouble()<=maximum && std::floor(value.toDouble())==value.toDouble();
}
}
bool CaptureAnnotation::normalize(QString &error) {
    error.clear(); name=name.trimmed();
    if(name.isEmpty() || name.size()>120 || name.contains('\n') || name.contains('\r') || notes.size()>4096) {
        error="Use a single-line name (1–120 characters) and notes up to 4096 characters"; return false;
    }
    if(tags.size()>16) {error="Use at most 16 tags"; return false;}
    QStringList normalized;
    for(auto tag:tags) {
        tag=tag.trimmed(); if(tag.isEmpty()) continue;
        if(tag.size()>32 || tag.contains(',') || tag.contains('\n') || tag.contains('\r')) {error="Tags must be single-line, comma-free and at most 32 characters"; return false;}
        if(!normalized.contains(tag,Qt::CaseInsensitive)) normalized.append(tag);
    }
    tags=normalized; return true;
}
bool LibraryEntry::matches(const QString &query) const {
    const auto haystack=annotation.name+"\n"+annotation.notes+"\n"+annotation.tags.join(' ');
    for(const auto &term:query.simplified().split(' ',Qt::SkipEmptyParts))
        if(!haystack.contains(term,Qt::CaseInsensitive)) return false;
    return true;
}
QString CaptureLibrary::root(QString &error,bool create) const {
    error.clear();
    if(folder.isEmpty()) {error="Choose a library folder first"; return {};}
    QDir dir(folder);
    if(!dir.exists() && (!create || !dir.mkpath("."))) {error="Library folder does not exist or cannot be created"; return {};}
    const QFileInfo info(dir.absolutePath());
    if(!info.isDir() || !info.isReadable()) {error="Library folder is not readable"; return {};}
    return dir.canonicalPath();
}
bool CaptureLibrary::readEntry(const QString &base,const QString &id,LibraryEntry &entry,QString &error) const {
    if(!validId(id)) {error="Invalid library entry ID"; return false;}
    const QFileInfo directory(QDir(base).filePath(id));
    if(directory.isSymLink() || !directory.isDir() || QFileInfo(directory.canonicalFilePath()).absolutePath()!=base) {
        error="Missing or linked library entry directory"; return false;
    }
    QByteArray bytes;
    if(!readFile(QDir(directory.absoluteFilePath()).filePath("entry.json"),manifestLimit,bytes,error)) return false;
    QJsonParseError parse; const auto document=QJsonDocument::fromJson(bytes,&parse); const auto o=document.object();
    if(parse.error!=QJsonParseError::NoError || !document.isObject() || o["format"]!="OpenHantekLabLibraryEntry" || o["version"]!=1 || o["id"]!=id) {
        error="Invalid library manifest or unsupported version"; return false;
    }
    if(!o["name"].isString() || !o["notes"].isString() || !o["tags"].isArray() || !o["channels"].isArray()) {error="Invalid annotation or channels"; return false;}
    LibraryEntry candidate; candidate.id=id; candidate.annotation.name=o["name"].toString(); candidate.annotation.notes=o["notes"].toString();
    for(const auto &tag:o["tags"].toArray()) {if(!tag.isString()) {error="Invalid tag"; return false;} candidate.annotation.tags.append(tag.toString());}
    if(!candidate.annotation.normalize(error)) return false;
    for(const auto &channel:o["channels"].toArray()) {
        if(!channel.isString() || channel.toString().size()>256) {error="Invalid channel label"; return false;}
        candidate.channels.append(channel.toString());
    }
    bool capturedOk=false,savedOk=false;
    candidate.capturedAtMs=o["capturedAtMs"].toString().toLongLong(&capturedOk);
    candidate.savedAtMs=o["savedAtMs"].toString().toLongLong(&savedOk);
    const auto hash=o["sha256"].toString().toLatin1();
    if(!capturedOk || !savedOk || candidate.capturedAtMs<0 || candidate.savedAtMs<0 ||
       !integer(o["tag"],UINT32_MAX) || !integer(o["samples"],1000000) || o["samples"].toDouble()==0 ||
       candidate.channels.isEmpty() || candidate.channels.size()>3 || hash.size()!=64 || QByteArray::fromHex(hash).toHex()!=hash) {
        error="Invalid capture summary or checksum"; return false;
    }
    const QFileInfo payload(QDir(directory.absoluteFilePath()).filePath("capture.ohl.json"));
    if(payload.isSymLink() || !payload.isFile() || payload.size()>captureLimit) {error="Missing, linked or oversized capture"; return false;}
    candidate.tag=unsigned(o["tag"].toDouble()); candidate.samples=size_t(o["samples"].toDouble());
    candidate.revision=digest(bytes); candidate.document=o; entry=std::move(candidate); return true;
}
LibraryIndex CaptureLibrary::scan(size_t limit) const {
    LibraryIndex index; const auto base=root(index.error); if(base.isEmpty()) return index;
    limit=std::min(limit,size_t(1000)); size_t examined=0;
    QDirIterator directories(base,{"capture-*"},QDir::Dirs|QDir::NoDotAndDotDot,QDirIterator::NoIteratorFlags);
    while(directories.hasNext() && examined<limit) {
        directories.next(); ++examined;
        LibraryEntry entry; QString error;
        if(readEntry(base,directories.fileName(),entry,error)) index.entries.push_back(std::move(entry));
        else {++index.skipped; if(index.warnings.size()<6) index.warnings.append(directories.fileName()+": "+error);}
    }
    index.truncated=directories.hasNext();
    std::sort(index.entries.begin(),index.entries.end(),[](const auto &a,const auto &b){return a.savedAtMs!=b.savedAtMs?a.savedAtMs>b.savedAtMs:a.id<b.id;});
    return index;
}
bool CaptureLibrary::add(const Capture &capture,CaptureAnnotation annotation,LibraryEntry &result,QString &error) const {
    if(!annotation.normalize(error)) return false;
    const auto base=root(error,true); if(base.isEmpty()) return false;
    QLockFile lock(QDir(base).filePath(".library.lock"));
    if(!lock.tryLock(0)) {error="Library is busy or not writable; try again"; return false;}
    QTemporaryDir pending(QDir(base).filePath(".pending-XXXXXX"));
    if(!pending.isValid()) {error=pending.errorString(); return false;}
    const auto capturePath=pending.filePath("capture.ohl.json");
    if(!capture.save(capturePath,error)) return false;
    QByteArray bytes; if(!readFile(capturePath,captureLimit,bytes,error)) return false;
    // Validate the saved bytes with the same strict reader used on opening.
    if(!Capture::fromJson(bytes,error)) return false;
    const auto id="capture-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    QStringList channels; size_t samples=0;
    for(const auto &channel:capture.channels) {channels.append(channel.name); samples+=channel.signal.samples.size();}
    QJsonObject manifest{{"format","OpenHantekLabLibraryEntry"},{"version",1},{"id",id},
        {"capturedAtMs",QString::number(capture.capturedAtMs)},{"savedAtMs",QString::number(QDateTime::currentMSecsSinceEpoch())},
        {"tag",double(capture.tag)},{"channels",QJsonArray::fromStringList(channels)},{"samples",double(samples)},{"sha256",QString::fromLatin1(digest(bytes))}};
    annotate(manifest,annotation);
    if(!writeManifest(pending.filePath("entry.json"),manifest,error)) return false;
    // Publish a complete bundle in one same-parent rename; never overwrite a capture.
    if(!QDir(base).rename(QFileInfo(pending.path()).fileName(),id)) {error="Could not publish the completed library entry"; return false;}
    pending.setAutoRemove(false);
    return readEntry(base,id,result,error);
}
bool CaptureLibrary::update(const LibraryEntry &entry,CaptureAnnotation annotation,LibraryEntry &result,QString &error) const {
    if(!annotation.normalize(error)) return false;
    const auto base=root(error); if(base.isEmpty()) return false;
    QLockFile lock(QDir(base).filePath(".library.lock"));
    if(!lock.tryLock(0)) {error="Library is busy or not writable; try again"; return false;}
    LibraryEntry current; if(!readEntry(base,entry.id,current,error)) return false;
    if(current.revision!=entry.revision) {error="This entry was changed elsewhere. Refresh before editing again"; return false;}
    auto manifest=current.document; annotate(manifest,annotation);
    if(!writeManifest(QDir(base).filePath(entry.id+"/entry.json"),manifest,error)) return false;
    return readEntry(base,entry.id,result,error);
}
std::shared_ptr<const Capture> CaptureLibrary::load(const LibraryEntry &entry,QString &error) const {
    const auto base=root(error); if(base.isEmpty()) return {};
    LibraryEntry current; if(!readEntry(base,entry.id,current,error)) return {};
    if(current.revision!=entry.revision) {error="This entry was changed elsewhere. Refresh before opening again"; return {};}
    QByteArray bytes; if(!readFile(QDir(base).filePath(entry.id+"/capture.ohl.json"),captureLimit,bytes,error)) return {};
    if(digest(bytes)!=current.document["sha256"].toString().toLatin1()) {error="Capture checksum mismatch; file may be damaged or changed"; return {};}
    auto capture=Capture::fromJson(bytes,error); if(!capture) return {};
    QStringList channels; size_t samples=0;
    for(const auto &channel:capture->channels) {channels.append(channel.name); samples+=channel.signal.samples.size();}
    if(capture->tag!=current.tag || capture->capturedAtMs!=current.capturedAtMs || channels!=current.channels || samples!=current.samples) {
        error="Capture does not match its library summary"; return {};
    }
    return capture;
}
}
