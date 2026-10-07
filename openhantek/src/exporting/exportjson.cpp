// SPDX-License-Identifier: GPL-2.0-or-later
// Sandro Sobczyński <sandro.sobczynski@gmail.com>

#include "exportjson.h"
#include "dsosettings.h"
#include "exporterregistry.h"
#include "post/ppresult.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QLocale>
#include <QMessageBox>
#include <QTextStream>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

ExporterJSON::ExporterJSON() {}

void ExporterJSON::create( ExporterRegistry *newRegistry ) {
    registry = newRegistry;
    data.reset();
}

QString ExporterJSON::name() { return tr( "Export &JSON .." ); }

QString ExporterJSON::format() { return "JSON"; }

ExporterInterface::Type ExporterJSON::type() { return Type::SnapshotExport; }

bool ExporterJSON::samples( const std::shared_ptr< PPresult > newData ) {
    data = std::move( newData );
    return false;
}

QString ExporterJSON::getFile() {
    QFileDialog fileDialog( nullptr, tr( "Save JSON" ), QString(), tr( "Java Script Object Notation (*.json)" ) );
    fileDialog.setFileMode( QFileDialog::AnyFile );
    fileDialog.setAcceptMode( QFileDialog::AcceptSave );
    fileDialog.setDefaultSuffix("json");
    fileDialog.setOption( QFileDialog::DontUseNativeDialog );
    if ( fileDialog.exec() != QDialog::Accepted )
        return {};
    return fileDialog.selectedFiles().first();
}

void ExporterJSON::fillData( QTextStream &jsonStream, const ExporterData &dto ) {
    std::vector< const SampleValues * > voltageData = dto.getVoltageData();
    std::vector< const SampleValues * > spectrumData = dto.getSpectrumData();

    QJsonArray rows;
    for ( unsigned int row = 0; row < dto.getMaxRow(); ++row ) {
        QJsonObject object;
        const bool hasTime = std::any_of(voltageData.begin(), voltageData.end(), [row](const SampleValues *v) {
            return v && row < v->samples.size();
        });
        object["time"] = hasTime ? QJsonValue(dto.getTimeInterval() * row) : QJsonValue();
        for ( ChannelID channel = 0; channel < dto.getChannelsCount(); ++channel )
            if ( voltageData[ channel ] != nullptr ) {
                object[registry->settings->scope.voltage[channel].name] = row < voltageData[channel]->samples.size()
                    ? QJsonValue(voltageData[channel]->samples[row]) : QJsonValue();
            }
        if ( dto.isSpectrumUsed() ) {
            const bool hasFrequency = std::any_of(spectrumData.begin(), spectrumData.end(), [row](const SampleValues *v) {
                return v && row < v->samples.size();
            });
            object["freq"] = hasFrequency ? QJsonValue(dto.getFreqInterval() * row) : QJsonValue();
            for ( ChannelID channel = 0; channel < dto.getChannelsCount(); ++channel ) {
                if ( spectrumData[ channel ] != nullptr ) {
                    object[registry->settings->scope.spectrum[channel].name] = row < spectrumData[channel]->samples.size()
                        ? QJsonValue(spectrumData[channel]->samples[row]) : QJsonValue();
                }
            }
        }
        rows.append(object);
    }
    jsonStream << QString::fromUtf8(QJsonDocument(rows).toJson());
}

bool ExporterJSON::save() {
    const QString name = getFile();
    if (name.isEmpty()) return false;
    QSaveFile file(name);
    const bool saved = file.open(QIODevice::WriteOnly | QIODevice::Text) && write(file) && file.commit();
    if (!saved) QMessageBox::warning(nullptr, tr("JSON export"), tr("Could not save %1: %2").arg(name, file.errorString()));
    return saved;
}

bool ExporterJSON::write(QIODevice &device) {
    if (!data || !device.isWritable()) return false;
    QTextStream jsonStream( &device );
    jsonStream.setRealNumberNotation( QTextStream::FixedNotation );
    jsonStream.setRealNumberPrecision( 10 );

    ExporterData dto = ExporterData( data, registry->settings->scope );
    fillData( jsonStream, dto );

    jsonStream.flush();
    return jsonStream.status() == QTextStream::Ok;
}


float ExporterJSON::progress() { return data ? 1.0f : 0; }
