// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "capturelibrary.h"
#include <QDialog>

class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QLabel;
class QPushButton;
namespace Lab {
class LibraryDialog : public QDialog {
public:
    LibraryDialog(QString folder,std::shared_ptr<const Capture> snapshot,QWidget *parent=nullptr);
    QString folder() const {return libraryFolder;}
    std::shared_ptr<const Capture> chosen;
    QString chosenName;
    bool asReference=false;
private:
    QString libraryFolder;
    std::shared_ptr<const Capture> snapshot;
    LibraryIndex index;
    QListWidget *list;
    QLineEdit *search;
    QPlainTextEdit *notes;
    QLabel *path, *summary, *title, *tags, *info, *message;
    QPushButton *open, *reference, *edit;
    const LibraryEntry *current() const;
    void refresh(const QString &selectId={});
    void filter();
    void showEntry();
    void choose(bool reference);
    void addCapture();
    void editEntry();
};
}
