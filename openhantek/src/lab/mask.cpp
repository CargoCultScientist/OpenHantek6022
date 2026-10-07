// SPDX-License-Identifier: GPL-2.0-or-later
#include "mask.h"
#include <algorithm>
#include <cmath>

namespace Lab {
QString maskStateName(MaskState state) {
    return state==MaskState::Pass?QStringLiteral("PASS"):state==MaskState::Fail?QStringLiteral("FAIL"):QStringLiteral("NOT TESTABLE");
}
MaskResult testMask(const Capture &current, const Capture *reference, const MaskSpec &spec, bool align, double offset) {
    MaskResult result;
    auto invalid=[&](const QString &reason){result.reason=reason; return result;};
    if(!reference) return invalid("Pin a reference first");
    if(spec.channel>=current.channels.size() || spec.channel>=reference->channels.size()) return invalid("Missing channel");
    const auto &a=current.channels[spec.channel]; const auto &b=reference->channels[spec.channel];
    if(a.unit!=b.unit) return invalid("Incompatible units");
    if(!a.valid || !b.valid) return invalid("Clipped or invalid capture");
    if(align && (!current.triggered || !reference->triggered)) return invalid("Both records must have a trigger");
    if(a.signal.samples.empty() || b.signal.samples.size()<2 || !std::isfinite(a.signal.interval) || a.signal.interval<=0)
        return invalid("No usable samples");
    auto finite=[](double v){return std::isfinite(v);};
    if(!std::all_of(a.signal.samples.begin(),a.signal.samples.end(),finite) ||
       !std::all_of(b.signal.samples.begin(),b.signal.samples.end(),finite)) return invalid("Non-finite samples");
    if(!finite(spec.absoluteTolerance) || !finite(spec.percentOfReferenceVpp) || !finite(offset) ||
       spec.absoluteTolerance<0 || spec.percentOfReferenceVpp<0) return invalid("Invalid tolerance or time shift");
    const auto bounds=std::minmax_element(b.signal.samples.begin(),b.signal.samples.end());
    result.tolerance=spec.absoluteTolerance+spec.percentOfReferenceVpp/100*(*bounds.second-*bounds.first);
    if(!finite(result.tolerance)) return invalid("Tolerance overflow");
    const double a0=timeOrigin(current,a,align), b0=timeOrigin(*reference,b,align)+offset;
    result.maximumError=0;
    for(size_t i=0;i<a.signal.samples.size();++i) {
        double expected=0;
        if(!interpolatedSample(b.signal,a0+i*a.signal.interval,b0,expected)) return invalid("Reference does not cover the whole record");
        const double error=std::abs(a.signal.samples[i]-expected);
        if(!finite(error)) return invalid("Difference overflow");
        // Permit round-off at a tolerance boundary, not an arbitrary voltage floor.
        const double roundoff=32*std::numeric_limits<double>::epsilon()*
            std::max({std::abs(a.signal.samples[i]),std::abs(expected),result.tolerance});
        if(error>result.tolerance && error-result.tolerance>roundoff) ++result.outside;
        result.maximumError=std::max(result.maximumError,error); ++result.tested;
    }
    result.state=result.outside?MaskState::Fail:MaskState::Pass;
    return result;
}
}
