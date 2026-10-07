// SPDX-License-Identifier: GPL-2.0-or-later
#include "measurements.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Lab {
static double median(std::vector<double> values) {
    if (values.empty()) return unavailable;
    std::sort(values.begin(), values.end());
    return (values[(values.size()-1)/2] + values[values.size()/2])/2;
}
// A midpoint crossing is accepted only after traversing the hysteresis band.
static std::vector<double> crossings(const std::vector<double> &s, double mid, double hysteresis, int sign) {
    std::vector<double> edges;
    bool armed = sign*(s.front()-mid) <= -hysteresis;
    double pending = unavailable;
    for (size_t i=1; i<s.size(); ++i) {
        const double before=sign*(s[i-1]-mid), after=sign*(s[i]-mid);
        if (after <= -hysteresis) { armed=true; pending=unavailable; }
        if (armed && before<=0 && after>0) pending=double(i-1)-before/(after-before);
        if (armed && after>=hysteresis && std::isfinite(pending)) {
            edges.push_back(pending); armed=false; pending=unavailable;
        }
    }
    return edges;
}
static double transitionTime(const std::vector<double> &s, double low, double high, int sign) {
    std::vector<double> durations;
    double start=unavailable;
    for(size_t i=1; i<s.size(); ++i) {
        const double a=sign*s[i-1], b=sign*s[i];
        if (b<=low) start=unavailable;
        if (a<=low && b>low) start=double(i-1)+(low-a)/(b-a);
        if (a<high && b>=high && std::isfinite(start)) {
            const double duration=double(i-1)+(high-a)/(b-a)-start;
            durations.push_back(duration); start=unavailable;
        }
    }
    return median(std::move(durations));
}
Measurements measure(const std::vector<double> &s, double dt, bool valid) {
    Measurements m; m.clipped=!valid;
    if(s.empty() || !std::isfinite(dt) || dt<=0 ||
       !std::all_of(s.begin(),s.end(),[](double v){return std::isfinite(v);})) return m;
    m.count=s.size(); m.span=(s.size()-1)*dt;
    RunningStatistic levels;
    for(double v:s) levels.add(v);
    m.minimum=levels.minimum; m.maximum=levels.maximum; m.vpp=m.maximum-m.minimum;
    m.mean=levels.mean; m.acRms=std::sqrt(std::max(0.0,levels.m2/double(levels.count)));
    m.rms=std::hypot(m.mean,m.acRms);
    if (!valid || s.size()<3 || m.vpp<=1e-12) return m;
    const double midpoint=(m.minimum+m.maximum)/2;
    auto rising=crossings(s,midpoint,.05*m.vpp,1);
    auto falling=crossings(s,midpoint,.05*m.vpp,-1);
    std::vector<double> periods;
    for(size_t i=1;i<rising.size();++i) periods.push_back(rising[i]-rising[i-1]);
    const double period=median(periods);
    if(!std::isfinite(period)) return m;
    if(period<10) {m.undersampled=true; return m;}
    if(std::any_of(periods.begin(),periods.end(),[period](double p){return std::abs(p-period)>.2*period;})) {
        m.irregular=true; return m;
    }
    m.period=period*dt; m.frequency=1/m.period;
    std::vector<double> widths, duties;
    for(size_t i=1;i<rising.size();++i) {
        auto fall=std::upper_bound(falling.begin(),falling.end(),rising[i-1]);
        if(fall!=falling.end() && *fall<rising[i]) {
            widths.push_back((*fall-rising[i-1])*dt);
            duties.push_back((*fall-rising[i-1])/(rising[i]-rising[i-1]));
        }
    }
    m.positiveWidth=median(widths); m.duty=median(duties);
    const double rise=transitionTime(s,m.minimum+.1*m.vpp,m.minimum+.9*m.vpp,1);
    const double fall=transitionTime(s,-m.maximum+.1*m.vpp,-m.maximum+.9*m.vpp,-1);
    if(rise>=2) m.rise=rise*dt;
    if(fall>=2) m.fall=fall*dt;
    if((std::isfinite(rise) && rise<2)||(std::isfinite(fall)&&fall<2)) m.undersampled=true;
    return m;
}
void RunningStatistic::add(double value) {
    if(!std::isfinite(value)) return;
    if(!count) minimum=maximum=value;
    minimum=std::min(minimum,value); maximum=std::max(maximum,value);
    const double delta=value-mean;
    mean+=delta/double(++count); m2+=delta*(value-mean);
}
double RunningStatistic::deviation() const { return count>1 ? std::sqrt(std::max(0.0,m2/double(count-1))) : unavailable; }
}
