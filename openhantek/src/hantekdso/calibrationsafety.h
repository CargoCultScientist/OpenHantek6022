// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cmath>

inline bool validCalibrationOffset(int minimum, int maximum, double offset) {
    return minimum > 0 && maximum < 255 && maximum >= minimum &&
           maximum - minimum <= 10 && std::isfinite(offset) && std::abs(offset) <= 20;
}
