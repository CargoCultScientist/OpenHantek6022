// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <limits>
#include <vector>

namespace Lab {
constexpr double unavailable = std::numeric_limits<double>::quiet_NaN();
struct Measurements {
    size_t count = 0;
    double span = 0;
    double minimum = unavailable, maximum = unavailable, vpp = unavailable;
    double mean = unavailable, rms = unavailable, acRms = unavailable;
    double frequency = unavailable, period = unavailable, duty = unavailable, positiveWidth = unavailable;
    double rise = unavailable, fall = unavailable;
    bool clipped = false, undersampled = false, irregular = false;
};
// All measurements use the entire supplied record. Edge times are interpolated;
// timing is withheld for clipping, <10 samples/period, or unstable periods.
Measurements measure(const std::vector<double> &samples, double interval, bool valid = true);
struct RunningStatistic {
    size_t count = 0;
    double mean = 0, m2 = 0, minimum = unavailable, maximum = unavailable;
    void add(double value);
    double deviation() const;
};
}
