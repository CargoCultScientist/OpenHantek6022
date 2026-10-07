// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "capture.h"

namespace Lab {
struct CaptureAnnotation {
    QString name, notes;
    QStringList tags;
    bool normalize(QString &error);
};
struct LibraryEntry {
    QString id;
    CaptureAnnotation annotation;
    qint64 capturedAtMs=0, savedAtMs=0;
    unsigned tag=0;
    QStringList channels;
    size_t samples=0;
    QByteArray revision; // Hash of the manifest as read; detects conflicting edits.
    QJsonObject document;
    bool matches(const QString &query) const;
};
struct LibraryIndex {
    std::vector<LibraryEntry> entries;
    QString error;
    QStringList warnings;
    size_t skipped=0;
    bool truncated=false;
};
// Append-only waveform bundles. Editing annotations changes only entry.json.
// No deletion, automatic saving, recursive scanning, or hardware configuration.
class CaptureLibrary {
public:
    explicit CaptureLibrary(QString folder) : folder(std::move(folder)) {}
    LibraryIndex scan(size_t limit=1000) const;
    bool add(const Capture &, CaptureAnnotation, LibraryEntry &result, QString &error) const;
    bool update(const LibraryEntry &, CaptureAnnotation, LibraryEntry &result, QString &error) const;
    std::shared_ptr<const Capture> load(const LibraryEntry &, QString &error) const;
private:
    QString folder;
    QString root(QString &error, bool create=false) const;
    bool readEntry(const QString &root, const QString &id, LibraryEntry &, QString &error) const;
};
}
