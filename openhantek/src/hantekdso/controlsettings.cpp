// SPDX-License-Identifier: GPL-2.0-or-later

#include "controlsettings.h"
#include "hantekprotocol/definitions.h"
#include <cstring>

namespace Dso {

ControlSettings::ControlSettings( const ControlSamplerateLimits *limits, size_t channelCount ) : cmdGetCalibration() {
    samplerate.limits = limits;
    trigger.level.resize( channelCount + 1 ); // two physical + math channel
    voltage.resize( channelCount + 1 );       // two physical + math channel
    calibrationValues = new Hantek::CalibrationValues;
    correctionValues = new Hantek::CalibrationValues;
    std::memset(calibrationValues, 0xFF, sizeof(*calibrationValues));
    std::memset(correctionValues, 0xFF, sizeof(*correctionValues));
}

ControlSettings::~ControlSettings() {
    delete calibrationValues;
    delete correctionValues;
}

} // namespace Dso
