// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "utils/printutils.h"
#include <QReadLocker>
#include <QReadWriteLock>
#include <QWriteLocker>
#include <vector>
#include <memory>
#include <cstdint>

struct DSOsamples {
    std::vector< std::vector< double > > data; ///< Pointer to input data from device
    double samplerate = 0.0;                   ///< The samplerate of the input data
    unsigned char clipped = 0;                 ///< Bitmask of clipped channels
    bool liveTrigger = false;                  ///< live samples are triggered
    int triggeredPosition = 0;                 ///< position for a triggered trace, 0 = not triggered
    double pulseWidth1 = 0.0;                  ///< width from trigger point to next opposite slope
    double pulseWidth2 = 0.0;                  ///< width from next opposite slope to third slope
    Unit mathVoltageUnit = UNIT_VOLTS;         ///< unless UNIT_VOLTSQUARE for some math functions
    bool freeRunning = false;                  ///< trigger: NONE, half sample count
    unsigned tag = 0;                          ///< track individual sample blocks (debug support)
    std::uint64_t armGeneration = 0;
    qint64 capturedAtMs = 0;
    mutable QReadWriteLock lock;

    std::shared_ptr<const DSOsamples> snapshot() const {
        QReadLocker locker(&lock);
        auto copy = std::make_shared<DSOsamples>();
        copy->data = data;
        copy->samplerate = samplerate;
        copy->clipped = clipped;
        copy->liveTrigger = liveTrigger;
        copy->triggeredPosition = triggeredPosition;
        copy->pulseWidth1 = pulseWidth1;
        copy->pulseWidth2 = pulseWidth2;
        copy->mathVoltageUnit = mathVoltageUnit;
        copy->freeRunning = freeRunning;
        copy->tag = tag;
        copy->armGeneration = armGeneration;
        copy->capturedAtMs = capturedAtMs;
        return copy;
    }
};

const int SAMPLESIZE = 20000;
const int SAMPLESIZE_ROLL = 40 * 256;
