// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "post/ppresult.h"
#include "scopesettings.h"
#include <QByteArray>
#include <QStringList>

namespace Lab {
// Only these fields may be changed by Auto. Probe, coupling, calibration,
// channel enablement, math, recording and stored captures are never changed.
struct SetupValues {
    struct Channel {
        unsigned gain = 0;
        double voltsPerDiv = 1, offset = 0, trigger = 0;
    };
    std::vector<Channel> channels;
    double timebase = .001, samplerate = 1e6, position = .5;
    int source = 0;
    Dso::TriggerMode mode = Dso::TriggerMode::AUTO;
    Dso::Slope slope = Dso::Slope::Positive;
    static SetupValues fromScope(const DsoSettingsScope &scope, unsigned channels);
    void writeTo(DsoSettingsScope &scope) const;
};
struct AutoSetupPlan {
    SetupValues values;
    QStringList hints;
    bool changed = false;
};
// Pure analysis of enabled physical channels. Does not use cached FFT/frequency
// labels or historical captures. Timing requires >=2 complete stable cycles.
AutoSetupPlan planAutoSetup(const PPresult &frame, const DsoSettingsScope &scope, unsigned channels);
QByteArray setupFingerprint(const DsoSettingsScope &scope);
}
