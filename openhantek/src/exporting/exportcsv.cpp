// SPDX-License-Identifier: GPL-2.0-or-later

#include "exportcsv.h"
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
#include <cmath>
#include <algorithm>

static QString csvNumber(double value) {
    if (!std::isfinite(value)) return {};
    QLocale locale;
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    return locale.toString(value, 'g', 17);
}

static QString csvName(QString name) {
    return '"' + name.replace("\"", "\"\"") + '"';
}

ExporterCSV::ExporterCSV() {}

void ExporterCSV::create( ExporterRegistry *newRegistry ) {
    registry = newRegistry;
    data.reset();
}

QString ExporterCSV::name() { return tr( "Export &CSV .." ); }

QString ExporterCSV::format() { return "CSV"; }

ExporterInterface::Type ExporterCSV::type() { return Type::SnapshotExport; }

bool ExporterCSV::samples( const std::shared_ptr< PPresult > newData ) {
    data = std::move( newData );
    return false;
}

QString ExporterCSV::getFile() {
    QFileDialog fileDialog( nullptr, tr( "Save CSV" ), QString(), tr( "Comma-Separated Values (*.csv)" ) );
    fileDialog.setFileMode( QFileDialog::AnyFile );
    fileDialog.setAcceptMode( QFileDialog::AcceptSave );
    fileDialog.setDefaultSuffix("csv");
    fileDialog.setOption( QFileDialog::DontUseNativeDialog );
    if ( fileDialog.exec() != QDialog::Accepted )
        return {};
    return fileDialog.selectedFiles().first();
}

void ExporterCSV::fillHeaders( QTextStream &csvStream, const ExporterData &dto, const char *sep ) {
    std::vector< const SampleValues * > voltageData = dto.getVoltageData();
    std::vector< const SampleValues * > spectrumData = dto.getSpectrumData();

    csvStream << "\"t / s\"";

    // Channels
    for ( ChannelID channel = 0; channel < dto.getChannelsCount(); ++channel ) {
        if ( voltageData[ channel ] != nullptr ) {
            csvStream << sep << csvName(registry->settings->scope.voltage[channel].name + " / V");
        }
    }

    // Spectrums
    if ( dto.isSpectrumUsed() ) {
        csvStream << sep << "\"f / Hz\"";
        for ( ChannelID channel = 0; channel < dto.getChannelsCount(); ++channel ) {
            if ( spectrumData[ channel ] != nullptr ) {
                csvStream << sep << csvName(registry->settings->scope.spectrum[channel].name + " / dB");
            }
        }
    }

    csvStream << "\n";
}


void ExporterCSV::fillData( QTextStream &csvStream, const ExporterData &dto, const char *sep ) {
    std::vector< const SampleValues * > voltageData = dto.getVoltageData();
    std::vector< const SampleValues * > spectrumData = dto.getSpectrumData();

    for ( unsigned int row = 0; row < dto.getMaxRow(); ++row ) {

        const bool hasTime = std::any_of(voltageData.begin(), voltageData.end(), [row](const SampleValues *v) {
            return v && row < v->samples.size();
        });
        if (hasTime) csvStream << csvNumber(dto.getTimeInterval() * row);
        for ( ChannelID channel = 0; channel < dto.getChannelsCount(); ++channel ) {
            if ( voltageData[ channel ] != nullptr ) {
                csvStream << sep;
                if ( row < voltageData[ channel ]->samples.size() ) {
                    csvStream << csvNumber(voltageData[channel]->samples[row]);
                }
            }
        }
        if ( dto.isSpectrumUsed() ) {
            csvStream << sep;
            const bool hasFrequency = std::any_of(spectrumData.begin(), spectrumData.end(), [row](const SampleValues *v) {
                return v && row < v->samples.size();
            });
            if (hasFrequency) csvStream << csvNumber(dto.getFreqInterval() * row);
            for ( ChannelID channel = 0; channel < dto.getChannelsCount(); ++channel ) {
                if ( spectrumData[ channel ] != nullptr ) {
                    csvStream << sep;
                    if ( row < spectrumData[ channel ]->samples.size() ) {
                        csvStream << csvNumber(spectrumData[channel]->samples[row]);
                    }
                }
            }
        }
        csvStream << "\n";
    }
}

bool ExporterCSV::save() {
    const QString name = getFile();
    if (name.isEmpty()) return false;
    QSaveFile file(name);
    const bool saved = file.open(QIODevice::WriteOnly | QIODevice::Text) && write(file) && file.commit();
    if (!saved) QMessageBox::warning(nullptr, tr("CSV export"), tr("Could not save %1: %2").arg(name, file.errorString()));
    return saved;
}

bool ExporterCSV::write(QIODevice &device) {
    if (!data || !device.isWritable()) return false;
    QTextStream csvStream( &device );
    csvStream.setRealNumberNotation( QTextStream::FixedNotation );
    csvStream.setRealNumberPrecision( 10 );

    ExporterData dto = ExporterData( data, registry->settings->scope );

    // use semicolon as data separator if comma is already used as decimal separator - e.g. with german locale
    const char *sep = QLocale().decimalPoint() == ',' ? ";" : ",";

    fillHeaders( csvStream, dto, sep );
    fillData( csvStream, dto, sep );

    csvStream.flush();
    return csvStream.status() == QTextStream::Ok;
}


float ExporterCSV::progress() { return data ? 1.0f : 0; }
