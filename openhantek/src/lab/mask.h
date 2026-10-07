// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "capture.h"

namespace Lab {
enum class MaskState { Untestable, Pass, Fail };
struct MaskSpec {
    size_t channel=0;
    double absoluteTolerance=0;
    double percentOfReferenceVpp=5;
};
struct MaskResult {
    MaskState state=MaskState::Untestable;
    size_t tested=0, outside=0;
    double tolerance=unavailable, maximumError=unavailable;
    QString reason;
};
// Whole-record, sample-by-sample test. Requires full reference coverage, matching
// units and finite, unclipped samples. No automatic phase fitting/extrapolation.
MaskResult testMask(const Capture &, const Capture *, const MaskSpec &, bool alignTrigger=false, double offset=0);
QString maskStateName(MaskState);
}
