// SPDX-License-Identifier: GPL-2.0-or-later

#include "postprocessing.h"

PostProcessing::PostProcessing( ChannelID channelCount, int verboseLevel )
    : channelCount( channelCount ), verboseLevel( verboseLevel ) {
    qRegisterMetaType< std::shared_ptr< PPresult > >();
}


void PostProcessing::registerProcessor( Processor *processor ) { processors.push_back( processor ); }

void PostProcessing::enqueue(std::shared_ptr<const DSOsamples> data) {
    QMutexLocker locker(&pendingMutex);
    if(!processing) return;
    pending=std::move(data);
    if(drainScheduled) return;
    drainScheduled=true;
    QMetaObject::invokeMethod(this,[this] {
        std::shared_ptr<const DSOsamples> next;
        {
            QMutexLocker lock(&pendingMutex);
            next=std::move(pending); drainScheduled=false;
        }
        input(std::move(next));
    },Qt::QueuedConnection);
}


// static
void PostProcessing::convertData( const DSOsamples *source, PPresult *destination ) {
    // printf( "PostProcessing::convertData()\n" );
    QReadLocker locker( &source->lock );
    if ( source->triggeredPosition ) {
        destination->softwareTriggerTriggered = source->liveTrigger;
        destination->triggeredPosition = source->triggeredPosition;
        destination->pulseWidth1 = source->pulseWidth1;
        destination->pulseWidth2 = source->pulseWidth2;
    } else {
        destination->softwareTriggerTriggered = false;
        destination->triggeredPosition = 0;
        destination->pulseWidth1 = 0;
        destination->pulseWidth2 = 0;
    }

    for ( ChannelID channel = 0; channel < source->data.size(); ++channel ) {
        const std::vector< double > &rawChannelData = source->data.at( channel );

        if ( rawChannelData.empty() ) {
            continue;
        }
        DataChannel *const channelData = destination->modifiableData( channel );
        channelData->voltage.interval = 1.0 / source->samplerate;
        channelData->voltage.samples = rawChannelData;
        // printf( "PP CH%d: %d\n", channel+1, source->clipped );
        channelData->valid = !( source->clipped & ( 0x01 << channel ) );
    }
    destination->modifiableData( 2 )->voltageUnit = source->mathVoltageUnit; // MATH channel unit
    destination->tag = source->tag;
    destination->capturedAtMs = source->capturedAtMs;
}


void PostProcessing::input( std::shared_ptr<const DSOsamples> data ) {
    if ( data && processing ) {
        if ( verboseLevel > 4 )
            qDebug() << "    PostProcessing::input()" << data->tag;
        currentData.reset( new PPresult( channelCount ) ); // start with a fresh data structure
        convertData( data.get(), currentData.get() );      // copy from this acquisition's owned snapshot
        emit rawSamplesReady(std::make_shared<PPresult>(*currentData));
        for ( Processor *p : processors )                  // feed it into the PP chain
            p->process( currentData.get() );
        std::shared_ptr< PPresult > res = std::move( currentData );
        emit processingFinished( res );
    }
}
