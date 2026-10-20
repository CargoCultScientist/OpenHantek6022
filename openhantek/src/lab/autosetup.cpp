// SPDX-License-Identifier: GPL-2.0-or-later
#include "autosetup.h"
#include "measurements.h"
#include <QDataStream>
#include <QIODevice>
#include <algorithm>
#include <cmath>

namespace Lab {
SetupValues SetupValues::fromScope(const DsoSettingsScope &scope, unsigned count) {
    SetupValues result;
    for (unsigned ch = 0; ch < std::min(count, unsigned(scope.voltage.size())); ++ch) {
        const auto &v = scope.voltage[ch];
        result.channels.push_back({v.gainStepIndex, scope.gain(ch), v.offset, v.trigger});
    }
    result.timebase = scope.horizontal.timebase;
    result.samplerate = scope.horizontal.samplerate;
    result.position = scope.trigger.position;
    result.source = scope.trigger.source;
    result.mode = scope.trigger.mode;
    result.slope = scope.trigger.slope;
    return result;
}
void SetupValues::writeTo(DsoSettingsScope &scope) const {
    for (unsigned ch = 0; ch < std::min(channels.size(), scope.voltage.size()); ++ch) {
        scope.voltage[ch].gainStepIndex = channels[ch].gain;
        scope.voltage[ch].offset = channels[ch].offset;
        scope.voltage[ch].trigger = channels[ch].trigger;
    }
    scope.horizontal.timebase = timebase;
    scope.trigger.position = position;
    scope.trigger.source = source;
    scope.trigger.mode = mode;
    scope.trigger.slope = slope;
}
QByteArray setupFingerprint(const DsoSettingsScope &scope) {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    // Calculated samplerate/dots and display-only spectrum settings are excluded.
    // Explicit user samplerate changes invalidate the assistant via dock signals.
    out << scope.horizontal.timebase << int(scope.horizontal.format)
        << int(scope.trigger.mode) << scope.trigger.source << int(scope.trigger.slope)
        << scope.trigger.position << scope.trigger.smooth << scope.liveCalibrationActive;
    for (const auto &v : scope.voltage)
        out << v.used << v.gainStepIndex << v.probeAttn << v.couplingOrMathIndex
            << v.inverted << v.offset << v.trigger;
    for (const auto &s : scope.spectrum) out << s.used;
    for (double gain : scope.gainSteps) out << gain;
    return bytes;
}
static double step125(double value) {
    const double decade = std::pow(10., std::floor(std::log10(value)));
    for (double step : {1., 2., 5., 10.})
        if (step * decade >= value * (1. - 1e-12)) return step * decade;
    return 10. * decade;
}
AutoSetupPlan planAutoSetup(const PPresult &frame, const DsoSettingsScope &scope, unsigned count) {
    AutoSetupPlan plan;
    plan.values = SetupValues::fromScope(scope, count);
    const auto before = setupFingerprint(scope);
    int best = -1;
    double bestScore = 0, bestPeriod = 0, bestMidpoint = 0;
    bool any = false;
    for (unsigned ch = 0; ch < std::min(count, frame.channelCount()) && ch < scope.voltage.size(); ++ch) {
        if (!scope.voltage[ch].used) continue;
        any = true;
        const auto &input = scope.voltage[ch];
        auto &target = plan.values.channels[ch];
        const auto *data = frame.data(ch);
        const auto m = measure(data->voltage.samples, data->voltage.interval, data->valid);
        const QString prefix = QStringLiteral("CH%1: ").arg(ch + 1);
        const double gain = target.voltsPerDiv;
        if (m.count < 32 || !std::isfinite(m.vpp) || !std::isfinite(gain) || gain <= 0 ||
            !std::isfinite(input.probeAttn) || input.probeAttn <= 0 || scope.gainSteps.empty()) {
            plan.hints << prefix + QStringLiteral("not enough valid samples; check acquisition.");
            continue;
        }
        if (m.clipped) {
            if (target.gain + 1 < scope.gainSteps.size()) {
                ++target.gain;
                target.voltsPerDiv = scope.gainSteps[target.gain] * input.probeAttn;
                target.offset = 0;
                plan.hints << prefix + QStringLiteral("clipped; Auto widens one range. Acquire again before trusting amplitude or timing.");
            } else {
                plan.hints << prefix + QStringLiteral("clipped at the widest range. Check the input and correctly rated probe/attenuator; Auto cannot make an unsafe input safe.");
            }
            continue;
        }
        if (m.vpp < .08 * gain) {
            plan.hints << prefix + QStringLiteral("little variation: DC, a small signal, or no signal. Check source, ground and probe factor; range left unchanged.");
            continue;
        }
        // Six display divisions with DC headroom. Software centering cannot
        // recover an input outside the ADC range.
        const double required = std::max(m.vpp / 6., std::max(std::abs(m.minimum), std::abs(m.maximum)) / 3.);
        unsigned index = 0;
        while (index + 1 < scope.gainSteps.size() && scope.gainSteps[index] * input.probeAttn < required) ++index;
        target.gain = index;
        target.voltsPerDiv = scope.gainSteps[index] * input.probeAttn;
        target.offset = std::clamp(-(m.minimum + m.maximum) / (2 * target.voltsPerDiv), -3., 3.);
        if (m.vpp < gain)
            plan.hints << prefix + QStringLiteral("small on screen; Auto can fit the vertical range.");
        if (scope.trigger.source == int(ch) && (input.trigger <= m.minimum || input.trigger >= m.maximum))
            plan.hints << prefix + QStringLiteral("trigger level is outside the signal; Auto can center it when timing is stable.");
        if (m.irregular)
            plan.hints << prefix + QStringLiteral("irregular/noisy crossings; timebase and trigger are not inferred from this channel.");
        else if (!std::isfinite(m.period) || m.cycles < 2) {
            plan.hints << prefix + (m.undersampled
                ? QStringLiteral("too few samples per cycle; increase sample rate. Aliasing may hide the true frequency.")
                : QStringLiteral("not enough repeatable cycles; adjust timebase or check the source."));
        } else {
            if (m.undersampled)
                plan.hints << prefix + QStringLiteral("period is measurable, but fast edges are unresolved; increase sample rate for edge measurements.");
            const double score = m.vpp / gain;
            if (score > bestScore) {
                best = int(ch); bestScore = score; bestPeriod = m.period;
                bestMidpoint = (m.minimum + m.maximum) / 2;
            }
        }
    }
    if (!any) plan.hints << QStringLiteral("Enable a physical voltage channel before using Auto.");
    if (best >= 0) {
        plan.values.source = best;
        plan.values.mode = Dso::TriggerMode::AUTO;
        plan.values.slope = Dso::Slope::Positive;
        plan.values.position = .5;
        plan.values.channels[unsigned(best)].trigger = bestMidpoint;
        plan.values.timebase = std::clamp(step125(3 * bestPeriod / DIVS_TIME), 1e-9,
                                          std::max(1e-9, scope.horizontal.maxTimebase));
    }
    auto proposed = scope;
    plan.values.writeTo(proposed);
    plan.changed = setupFingerprint(proposed) != before;
    if (plan.hints.empty())
        plan.hints << QStringLiteral("Stable sampled signal. Auto can fit range, timebase and rising-edge trigger. This does not rule out aliasing.");
    return plan;
}
}
