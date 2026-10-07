// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <limits>
#include <vector>

namespace Lab {
constexpr double unavailable = std::numeric_limits<double>::quiet_NaN();
struct TimeSpan {
    double start = -std::numeric_limits<double>::infinity();
    double end = std::numeric_limits<double>::infinity();
};
// Half-open sample range. Time gates include sample centres at both boundaries;
// a reversed or invalid gate is empty (the UI sorts its two cursors first).
struct SampleRange {
    size_t begin = 0, end = std::numeric_limits<size_t>::max();
};
SampleRange sampleRange(size_t count, double interval, double origin, TimeSpan span = {});
struct Measurements {
    size_t count = 0;
    double span = 0;
    double minimum = unavailable, maximum = unavailable, vpp = unavailable;
    double mean = unavailable, rms = unavailable, acRms = unavailable;
    double frequency = unavailable, period = unavailable, duty = unavailable, positiveWidth = unavailable;
    double negativeDuty = unavailable, negativeWidth = unavailable, crestFactor = unavailable;
    double cycleMean = unavailable, cycleRms = unavailable, cycleSpan = 0;
    size_t cycles = 0;
    double rise = unavailable, fall = unavailable;
    bool clipped = false, undersampled = false, irregular = false;
};
// All measurements use the same supplied range. Edge times are interpolated;
// timing is withheld for clipping, <10 samples/period, or unstable periods.
// Cycle mean/RMS integrate the piecewise-linear signal between the first and last
// accepted rising edges, excluding incomplete cycles at the gate boundaries.
Measurements measure(const std::vector<double> &samples, double interval, bool valid = true, SampleRange range = {});
struct RunningStatistic {
    size_t count = 0;
    double mean = 0, m2 = 0, minimum = unavailable, maximum = unavailable;
    void add(double value);
    double deviation() const;
};
}
