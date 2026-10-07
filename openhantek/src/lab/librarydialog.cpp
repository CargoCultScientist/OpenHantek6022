// SPDX-License-Identifier: GPL-2.0-or-later
#include "librarydialog.h"
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

namespace Lab {
namespace {
QLabel *plainLabel(QWidget *parent,const QString &text={}) {
    auto label=new QLabel(text,parent); label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred); return label;
}
bool editAnnotation(QWidget *parent,const QString &caption,CaptureAnnotation &annotation) {
    QDialog dialog(parent); dialog.setObjectName("labAnnotationDialog"); dialog.setWindowTitle(caption); dialog.resize(540,360);
    auto layout=new QVBoxLayout(&dialog);
    layout->addWidget(plainLabel(&dialog,QObject::tr("Describe the capture. Waveform samples and acquisition context are kept unchanged.")));
    auto form=new QFormLayout;
    auto name=new QLineEdit(annotation.name,&dialog); name->setObjectName("labAnnotationName"); name->setMaxLength(120);
    auto tags=new QLineEdit(annotation.tags.join(", "),&dialog); tags->setObjectName("labAnnotationTags"); tags->setMaxLength(560);
    tags->setPlaceholderText(QObject::tr("e.g. power-supply, baseline, 5V — up to 16 tags"));
    auto notes=new QPlainTextEdit(annotation.notes,&dialog); notes->setObjectName("labAnnotationNotes");
    form->addRow(QObject::tr("&Name"),name); form->addRow(QObject::tr("&Tags (comma-separated)"),tags);
    form->addRow(QObject::tr("N&otes"),notes); layout->addLayout(form,1);
    auto error=plainLabel(&dialog); error->setObjectName("labAnnotationError"); error->setStyleSheet("color: #ffc184;"); layout->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog); layout->addWidget(buttons);
    auto validate=[&]{
        CaptureAnnotation candidate{name->text(),notes->toPlainText(),tags->text().split(',',Qt::SkipEmptyParts)};
        QString reason; const bool valid=candidate.normalize(reason);
        error->setText(valid?QObject::tr("%1 / 4096 note characters").arg(notes->toPlainText().size()):reason);
        buttons->button(QDialogButtonBox::Save)->setEnabled(valid);
    };
    QObject::connect(name,&QLineEdit::textChanged,&dialog,validate);
    QObject::connect(tags,&QLineEdit::textChanged,&dialog,validate);
    QObject::connect(notes,&QPlainTextEdit::textChanged,&dialog,validate);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    validate(); name->selectAll(); name->setFocus();
    if(dialog.exec()!=QDialog::Accepted) return false;
    CaptureAnnotation candidate{name->text(),notes->toPlainText(),tags->text().split(',',Qt::SkipEmptyParts)};
    QString reason; if(!candidate.normalize(reason)) return false;
    annotation=std::move(candidate); return true;
}
}
LibraryDialog::LibraryDialog(QString folder,std::shared_ptr<const Capture> snapshot,QWidget *parent)
    : QDialog(parent),libraryFolder(std::move(folder)),snapshot(std::move(snapshot)) {
    setObjectName("labWorkbench"); setWindowTitle(tr("Capture library")); resize(1020,640);
    auto layout=new QVBoxLayout(this); layout->setContentsMargins(18,16,18,16); layout->setSpacing(10);
    auto heading=plainLabel(this,tr("Capture library")); heading->setStyleSheet("color: #edf4fb; font-size: 22px; font-weight: 600;"); layout->addWidget(heading);
    layout->addWidget(plainLabel(this,tr("Keep the waveform. Remember the experiment. Reopen without changing hardware settings.")));
    auto folderRow=new QHBoxLayout;
    path=plainLabel(this); path->setObjectName("labLibraryPath"); path->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred); folderRow->addWidget(path,1);
    auto button=[this](const QString &text,const QString &id,auto callback) {
        auto result=new QPushButton(text,this); result->setObjectName(id); result->setAutoDefault(false);
        connect(result,&QPushButton::clicked,this,callback); return result;
    };
    folderRow->addWidget(button(tr("Choose folder…"),"labLibraryFolder",[this]{
        const auto folder=QFileDialog::getExistingDirectory(this,tr("Choose capture library folder"),libraryFolder);
        if(!folder.isEmpty()) {libraryFolder=folder; refresh();}
    }));
    folderRow->addWidget(button(tr("Refresh"),"labLibraryRefresh",[this]{refresh(current()?current()->id:QString());}));
    layout->addLayout(folderRow);
    search=new QLineEdit(this); search->setObjectName("labLibrarySearch"); search->setClearButtonEnabled(true); search->setMaxLength(256);
    search->setPlaceholderText(tr("Search names, notes and tags…  (Ctrl+F)")); search->setAccessibleName(tr("Search capture library")); layout->addWidget(search);
    summary=plainLabel(this); summary->setObjectName("labLibrarySummary"); layout->addWidget(summary);
    auto split=new QSplitter(this); list=new QListWidget(split); list->setObjectName("labLibraryList"); list->setMinimumWidth(240);
    list->setTextElideMode(Qt::ElideRight);
    auto details=new QWidget(split); auto detailLayout=new QVBoxLayout(details); detailLayout->setContentsMargins(16,0,0,0);
    title=plainLabel(details); title->setObjectName("labLibraryTitle"); title->setStyleSheet("color: #edf4fb; font-size: 20px; font-weight: 600;"); detailLayout->addWidget(title);
    tags=plainLabel(details); tags->setObjectName("labLibraryTags"); tags->setStyleSheet("color: #93e4d9;"); detailLayout->addWidget(tags);
    info=plainLabel(details); info->setObjectName("labLibraryInfo"); detailLayout->addWidget(info);
    notes=new QPlainTextEdit(details); notes->setObjectName("labLibraryNotes"); notes->setReadOnly(true); notes->setPlaceholderText(tr("No notes for this capture.")); detailLayout->addWidget(notes,1);
    auto actions=new QHBoxLayout;
    open=button(tr("Open for analysis"),"labLibraryOpen",[this]{choose(false);}); actions->addWidget(open);
    reference=button(tr("Use as reference"),"labLibraryReference",[this]{choose(true);}); actions->addWidget(reference);
    edit=button(tr("Edit details…"),"labLibraryEdit",[this]{editEntry();}); actions->addWidget(edit);
    detailLayout->addLayout(actions); split->setStretchFactor(1,1); split->setSizes({300,680}); layout->addWidget(split,1);
    message=plainLabel(this); message->setObjectName("labLibraryMessage"); message->setStyleSheet("color: #ffc184;"); layout->addWidget(message);
    auto bottom=new QHBoxLayout;
    auto add=button(tr("Save selected…"),"labLibraryAdd",[this]{addCapture();}); add->setEnabled(bool(this->snapshot)); bottom->addWidget(add);
    bottom->addWidget(plainLabel(this,this->snapshot?tr("Snapshot #%1 — fixed when this library opened.\nLive acquisition and logging may continue.").arg(this->snapshot->tag):
        tr("Select a capture in the workbench to save it here.")),1);
    bottom->addWidget(button(tr("Close"),"labLibraryClose",[this]{reject();})); layout->addLayout(bottom);
    connect(search,&QLineEdit::textChanged,this,[this]{filter();});
    connect(search,&QLineEdit::returnPressed,this,[this]{list->setFocus();});
    connect(list,&QListWidget::currentRowChanged,this,[this]{showEntry();});
    connect(list,&QListWidget::itemActivated,this,[this]{choose(false);});
    auto find=new QShortcut(QKeySequence::Find,this); connect(find,&QShortcut::activated,this,[this]{search->setFocus(); search->selectAll();});
    refresh(); search->setFocus();
}
const LibraryEntry *LibraryDialog::current() const {
    const auto item=list->currentItem();
    if(!item || item->isHidden()) return nullptr;
    const int row=item->data(Qt::UserRole).toInt();
    return row>=0 && size_t(row)<index.entries.size()?&index.entries[size_t(row)]:nullptr;
}
void LibraryDialog::refresh(const QString &selectId) {
    const QString wanted=selectId; // May refer into the old index, about to be replaced.
    QSignalBlocker block(list); index=CaptureLibrary(libraryFolder).scan(); list->clear();
    path->setText(libraryFolder); path->setToolTip(libraryFolder.toHtmlEscaped());
    int select=-1;
    for(size_t i=0;i<index.entries.size();++i) {
        const auto &entry=index.entries[i];
        auto item=new QListWidgetItem(entry.annotation.name+"\n"+QDateTime::fromMSecsSinceEpoch(entry.capturedAtMs).toString("yyyy-MM-dd  HH:mm:ss")+tr("  ·  #%1").arg(entry.tag),list);
        item->setData(Qt::UserRole,int(i)); item->setToolTip(entry.annotation.tags.join(", ").toHtmlEscaped());
        if(entry.id==wanted) select=int(i);
    }
    if(select>=0) list->setCurrentRow(select);
    message->setText(index.error.isEmpty()?index.warnings.join('\n'):index.error+tr(". Save selected creates a new folder if needed."));
    filter();
}
void LibraryDialog::filter() {
    QSignalBlocker block(list); int count=0, first=-1;
    for(int i=0;i<list->count();++i) {
        const bool match=index.entries[size_t(i)].matches(search->text()); list->item(i)->setHidden(!match);
        if(match) {++count; if(first<0) first=i;}
    }
    if(!current()) list->setCurrentRow(first);
    summary->setText(tr("%1 matches / %2 saved captures · %3 skipped entries%4")
        .arg(count).arg(index.entries.size()).arg(index.skipped)
        .arg(index.truncated?tr(" · Scan limited to 1,000 folders; choose a smaller library"):QString()));
    showEntry();
}
void LibraryDialog::showEntry() {
    const auto entry=current(); open->setEnabled(entry); reference->setEnabled(entry); edit->setEnabled(entry);
    if(!entry) {
        title->setText(search->text().isEmpty()?tr("Your saved experiments live here"):tr("No matching captures"));
        tags->clear(); info->setText(tr("Save the selected capture with a name, notes and tags. Existing .ohl.json files can be opened in the workbench, then saved here.")); notes->clear(); return;
    }
    title->setText(entry->annotation.name); tags->setText(entry->annotation.tags.isEmpty()?tr("No tags"):entry->annotation.tags.join("   ·   "));
    info->setText(tr("Captured %1 · #%2\n%3 samples · %4\nChecksum checked when opened. No settings are restored.")
        .arg(QDateTime::fromMSecsSinceEpoch(entry->capturedAtMs).toString("yyyy-MM-dd HH:mm:ss.zzz t")).arg(entry->tag).arg(entry->samples)
        .arg(entry->channels.join(", ").left(200)));
    notes->setPlainText(entry->annotation.notes);
}
void LibraryDialog::choose(bool referenceChoice) {
    const auto entry=current(); if(!entry) return;
    QString error; auto capture=CaptureLibrary(libraryFolder).load(*entry,error);
    if(!capture) {message->setText(error); return;}
    chosen=std::move(capture); chosenName=entry->annotation.name; asReference=referenceChoice; accept();
}
void LibraryDialog::addCapture() {
    if(!snapshot) return;
    CaptureAnnotation annotation{tr("Capture #%1").arg(snapshot->tag),{}, {}};
    if(!editAnnotation(this,tr("Save selected capture to library"),annotation)) return;
    LibraryEntry entry; QString error;
    if(!CaptureLibrary(libraryFolder).add(*snapshot,annotation,entry,error)) {message->setText(error); return;}
    search->clear(); refresh(entry.id); message->setText(tr("Saved #%1 as “%2”. The waveform is a separate, immutable copy.").arg(snapshot->tag).arg(annotation.name));
}
void LibraryDialog::editEntry() {
    const auto selected=current(); if(!selected) return;
    const auto original=*selected; auto annotation=original.annotation;
    if(!editAnnotation(this,tr("Edit capture description"),annotation)) return;
    LibraryEntry entry; QString error;
    if(!CaptureLibrary(libraryFolder).update(original,annotation,entry,error)) {message->setText(error); return;}
    refresh(entry.id); message->setText(tr("Description saved. Waveform bytes were not changed."));
}
}
