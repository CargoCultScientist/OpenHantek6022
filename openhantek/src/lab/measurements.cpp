// SPDX-License-Identifier: GPL-2.0-or-later
#include "measurements.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Lab {
SampleRange sampleRange(size_t count, double dt, double origin, TimeSpan span) {
    if(!count || !std::isfinite(dt) || dt<=0 || !std::isfinite(origin) ||
       std::isnan(span.start) || std::isnan(span.end) || span.end<span.start) return {0,0};
    // Snap tiny floating-point errors at sample boundaries, without admitting
    // appreciably out-of-gate samples. Clamp before conversion to size_t.
    auto position=[&](double t) {
        const double p=(t-origin)/dt;
        return std::isfinite(p) && std::abs(p-std::round(p))<1e-9 ? std::round(p) : p;
    };
    const double first=std::clamp(std::ceil(position(span.start)),0.,double(count));
    const double end=std::clamp(std::floor(position(span.end))+1,0.,double(count));
    return {size_t(first),size_t(std::max(first,end))};
}
struct Samples {
    const double *values;
    size_t count;
    size_t size() const {return count;}
    double front() const {return values[0];}
    double operator[](size_t i) const {return values[i];}
    const double *begin() const {return values;}
    const double *end() const {return values+count;}
};
static double median(std::vector<double> values) {
    if (values.empty()) return unavailable;
    std::sort(values.begin(), values.end());
    return (values[(values.size()-1)/2] + values[values.size()/2])/2;
}
// A midpoint crossing is accepted only after traversing the hysteresis band.
static std::vector<double> crossings(Samples s, double mid, double hysteresis, int sign) {
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
static double transitionTime(Samples s, double low, double high, int sign) {
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
Measurements measure(const std::vector<double> &samples, double dt, bool valid, SampleRange range) {
    Measurements m; m.clipped=!valid;
    range.end=std::min(range.end,samples.size());
    if(range.begin>=range.end) return m;
    const Samples s{samples.data()+range.begin,range.end-range.begin};
    if(!std::isfinite(dt) || dt<=0 ||
       !std::all_of(s.begin(),s.end(),[](double v){return std::isfinite(v);})) return m;
    m.count=s.size(); m.span=(s.size()-1)*dt;
    RunningStatistic levels;
    for(double v:s) levels.add(v);
    m.minimum=levels.minimum; m.maximum=levels.maximum; m.vpp=m.maximum-m.minimum;
    m.mean=levels.mean; m.acRms=std::sqrt(std::max(0.0,levels.m2/double(levels.count)));
    m.rms=std::hypot(m.mean,m.acRms);
    if(m.rms>0) m.crestFactor=std::max(std::abs(m.minimum),std::abs(m.maximum))/m.rms;
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
    std::vector<double> widths, duties, negativeWidths, negativeDuties;
    for(size_t i=1;i<rising.size();++i) {
        auto fall=std::upper_bound(falling.begin(),falling.end(),rising[i-1]);
        if(fall!=falling.end() && *fall<rising[i]) {
            if(fall+1!=falling.end() && *(fall+1)<rising[i]) {m.irregular=true; return m;}
            widths.push_back((*fall-rising[i-1])*dt);
            duties.push_back((*fall-rising[i-1])/(rising[i]-rising[i-1]));
            negativeWidths.push_back((rising[i]-*fall)*dt);
            negativeDuties.push_back((rising[i]-*fall)/(rising[i]-rising[i-1]));
        } else {m.irregular=true; return m;}
    }
    m.period=period*dt; m.frequency=1/m.period;
    m.positiveWidth=median(widths); m.duty=median(duties);
    m.negativeWidth=median(negativeWidths); m.negativeDuty=median(negativeDuties);
    // Integrate each linear segment analytically, including fractional boundary
    // segments. No rounded edge index or partial cycle biases the cycle average.
    long double integral=0, squareIntegral=0;
    for(size_t i=size_t(std::floor(rising.front())); i<s.size()-1 && double(i)<rising.back(); ++i) {
        const double left=std::max(double(i),rising.front()), right=std::min(double(i+1),rising.back());
        const long double a=s[i]+(s[i+1]-s[i])*(left-i), b=s[i]+(s[i+1]-s[i])*(right-i);
        integral+=(right-left)*(a+b)/2;
        squareIntegral+=(right-left)*(a*a+a*b+b*b)/3;
    }
    m.cycles=periods.size(); m.cycleSpan=(rising.back()-rising.front())*dt;
    m.cycleMean=double(integral/(rising.back()-rising.front()));
    m.cycleRms=std::sqrt(double(squareIntegral/(rising.back()-rising.front())));
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
