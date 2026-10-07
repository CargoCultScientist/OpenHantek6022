// SPDX-License-Identifier: GPL-2.0-or-later

#include "exporterregistry.h"
#include "exporterinterface.h"

#include <algorithm>

#include "controlspecification.h"
#include "dsosettings.h"
#include "post/ppresult.h"

ExporterRegistry::ExporterRegistry( const Dso::ControlSpecification *deviceSpecification, DsoSettings *settings, QObject *parent )
    : QObject( parent ), deviceSpecification( deviceSpecification ), settings( settings ) {}

bool ExporterRegistry::processData( std::shared_ptr< PPresult > &data, ExporterInterface *const &exporter ) {
    if ( !exporter->samples( data ) ) {
        waitToSaveExporters.insert( exporter );
        return true;
    }
    return false;
}

void ExporterRegistry::addRawSamples( PPresult *d ) {
    if ( settings->exportProcessedSamples )
        return;
    std::shared_ptr< PPresult > data = std::make_shared<PPresult>(*d);
    inputRaw(std::move(data));
}

void ExporterRegistry::inputRaw(std::shared_ptr<PPresult> data) {
    if (settings->exportProcessedSamples) return;
    enabledExporters.remove_if( [ &data, this ]( ExporterInterface *const &i ) { return processData( data, i ); } );
    if (!waitToSaveExporters.empty()) emit exporterProgressChanged();
}

void ExporterRegistry::input( std::shared_ptr< PPresult > data ) {
    if ( !settings->exportProcessedSamples )
        return;
    enabledExporters.remove_if( [ &data, this ]( ExporterInterface *const &i ) { return processData( data, i ); } );
    if (!waitToSaveExporters.empty()) emit exporterProgressChanged();
}

void ExporterRegistry::registerExporter( ExporterInterface *exporter ) {
    exporters.push_back( exporter );
    exporter->create( this );
}

void ExporterRegistry::setExporterEnabled( ExporterInterface *exporter, bool enabled ) {
    if (savingExporters.count(exporter)) return; // A modal save owns its snapshot until it closes.
    bool wasInList = false;
    enabledExporters.remove_if( [ exporter, &wasInList ]( ExporterInterface *inlist ) {
        if ( inlist == exporter ) {
            wasInList = true;
            return true;
        } else
            return false;
    } );

    if ( enabled ) {
        // If the exporter was waiting for the user save confirmation,
        // reset it instead.
        auto localFind = waitToSaveExporters.find( exporter );
        if ( localFind != waitToSaveExporters.end() ) {
            waitToSaveExporters.erase( localFind );
            exporter->create( this );
        }
        enabledExporters.push_back( exporter );
    } else if ( wasInList ) {
        // If exporter made some progress: Add to waiting for GUI list
        if ( exporter->progress() > 0 ) {
            waitToSaveExporters.insert( exporter );
            emit exporterProgressChanged();
        } else // Reset exporter
            exporter->create( this );
    }
}

void ExporterRegistry::checkForWaitingExporters() {
    // Modal save dialogs run a nested event loop. Detach this batch before entering it.
    const auto waiting = std::move(waitToSaveExporters);
    waitToSaveExporters.clear();
    savingExporters.insert(waiting.begin(), waiting.end());
    for ( ExporterInterface *exporter : waiting ) {
        if ( exporter->save() ) {
            emit exporterStatusChanged( exporter->name(), tr( "Data saved" ) );
        } else {
            emit exporterStatusChanged( exporter->name(), tr( "No data exported" ) );
        }
        exporter->create( this );
        savingExporters.erase(exporter);
    }
}

std::vector< ExporterInterface * >::const_iterator ExporterRegistry::begin() { return exporters.begin(); }

std::vector< ExporterInterface * >::const_iterator ExporterRegistry::end() { return exporters.end(); }
