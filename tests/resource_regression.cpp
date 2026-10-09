// SPDX-License-Identifier: GPL-2.0-or-later

#include <QtTest>
#include <QSettings>
#include <QTemporaryDir>
#include <array>
#include <cmath>

#include "dsosettings.h"
#include "post/graphgenerator.h"
#include "post/spectrumgenerator.h"

int verboseLevel = 0;

namespace {
PPresult signalFrame(const std::array<int, 3> &lengths) {
    PPresult frame(3);
    frame.pulseWidth1 = 0.0002;
    frame.pulseWidth2 = 0.0003;
    for (unsigned channel = 0; channel < lengths.size(); ++channel) {
        auto *data = frame.modifiableData(channel);
        data->voltage.interval = 1e-5;
        data->voltage.samples.reserve(size_t(lengths[channel]));
        for (int i = 0; i < lengths[channel]; ++i) {
            const double phase = 2 * M_PI * 1234 * i * data->voltage.interval + channel * 0.2;
            data->voltage.samples.push_back(0.25 * channel + std::sin(phase) + 0.01 * std::sin(2 * phase));
        }
    }
    return frame;
}
}

class ResourceRegressionTests : public QObject {
    Q_OBJECT
    QTemporaryDir config;

private slots:
    void initTestCase() {
        QVERIFY(config.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, config.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        QCoreApplication::setOrganizationName("OpenHantekResourceRegression");
        QCoreApplication::setApplicationName("ResourceRegression");
    }

    void fftScratchReusePreservesResults() {
        ScopeDevice device;
        DsoSettings settings(&device, 0, true);
        settings.alwaysSave = false;
        settings.scope.analysis.calculateTHD = true;
        settings.scope.analysis.showNoteValue = true;
        SpectrumGenerator reused(&settings.scope, &settings.analysis);

        // Exercise growth, smaller records, unchanged sizes, mixed channel
        // lengths, odd/even FFT endpoints and empty/single-sample channels.
        const std::array<std::array<int, 3>, 8> sizes = {{
            {{2, 3, 0}}, {{31, 32, 1}}, {{20000, 20000, 20000}},
            {{20000, 20000, 20000}}, {{10243, 13311, 4096}},
            {{3, 2, 31}}, {{20000, 20000, 20000}}, {{0, 1, 2}}
        }};
        for (bool reusePlans : {true, false, true}) {
            settings.analysis.reuseFftPlan = reusePlans;
            for (auto window : {Dso::WindowFunction::HANN, Dso::WindowFunction::RECTANGULAR}) {
                settings.analysis.spectrumWindow = window;
                for (const auto &lengths : sizes) {
                    auto actual = signalFrame(lengths);
                    auto expected = actual;
                    SpectrumGenerator fresh(&settings.scope, &settings.analysis);
                    static_cast<Processor &>(reused).process(&actual);
                    static_cast<Processor &>(fresh).process(&expected);
                    for (unsigned channel = 0; channel < lengths.size(); ++channel) {
                        const auto *a = actual.data(channel);
                        const auto *b = expected.data(channel);
                        QVERIFY(a && b);
                        QCOMPARE(a->voltage.samples, b->voltage.samples);
                        QCOMPARE(a->spectrum.samples, b->spectrum.samples);
                        QCOMPARE(a->spectrum.interval, b->spectrum.interval);
                        QCOMPARE(a->vmin, b->vmin);
                        QCOMPARE(a->vmax, b->vmax);
                        QCOMPARE(a->dc, b->dc);
                        QCOMPARE(a->ac, b->ac);
                        QCOMPARE(a->rms, b->rms);
                        QCOMPARE(a->dB, b->dB);
                        QCOMPARE(a->dBmin, b->dBmin);
                        QCOMPARE(a->dBmax, b->dBmax);
                        QCOMPARE(a->frequency, b->frequency);
                        QCOMPARE(a->thd, b->thd);
                        QCOMPARE(a->note, b->note);
                        QCOMPARE(a->pulseWidth1, b->pulseWidth1);
                        QCOMPARE(a->pulseWidth2, b->pulseWidth2);
                        if (lengths[channel] >= 2) {
                            const size_t bins = size_t(lengths[channel] / 2 + 1);
                            QCOMPARE(a->spectrum.samples.size(), bins);
                            // Allow modest container over-allocation, but not
                            // retention of the discarded mirrored spectrum.
                            QVERIFY(a->spectrum.samples.capacity() <= bins * 3 / 2);
                            for (double value : a->spectrum.samples)
                                QVERIFY(std::isfinite(value));
                        } else {
                            QVERIFY(a->spectrum.samples.empty());
                        }
                    }
                }
            }
        }
    }

    void graphStorageMatchesOutput() {
        ScopeDevice device;
        DsoSettings settings(&device, 0, true);
        settings.alwaysSave = false;
        settings.view.interpolation = Dso::INTERPOLATION_LINEAR;
        settings.scope.histogram = false;
        settings.scope.horizontal.format = Dso::GraphFormat::TY;
        for (unsigned channel = 0; channel < settings.scope.voltage.size(); ++channel) {
            settings.scope.voltage[channel].used = channel < 2;
            settings.scope.spectrum[channel].used = channel < 2;
        }
        GraphGenerator graphs(&settings.scope, &settings.view);
        auto ty = signalFrame({512, 512, 0});
        ty.modifiableData(0)->spectrum = {std::vector<double>(33, -40.0), 1000.0};
        ty.modifiableData(1)->spectrum = {std::vector<double>(17, -20.0), 2000.0};
        static_cast<Processor &>(graphs).process(&ty);
        for (unsigned channel = 0; channel < 2; ++channel) {
            const auto &spectrum = ty.data(channel)->spectrum;
            const auto &vertices = ty.vaChannelSpectrum[channel];
            QCOMPARE(vertices.size(), spectrum.samples.size());
            QVERIFY(vertices.capacity() <= vertices.size() * 3 / 2);
            QVERIFY(ty.vaChannelHistogram[channel].empty());
            QCOMPARE(ty.vaChannelHistogram[channel].capacity(), size_t(0));
            for (size_t i = 0; i < vertices.size(); ++i) {
                const QVector3D expected(
                    float(i * spectrum.interval / settings.scope.horizontal.frequencybase - DIVS_TIME / 2),
                    float(spectrum.samples[i] / settings.scope.spectrum[channel].magnitude +
                          settings.scope.spectrum[channel].offset), 0.0f);
                QCOMPARE(vertices[i], expected);
            }
        }

        settings.scope.horizontal.format = Dso::GraphFormat::XY;
        auto xy = signalFrame({31, 27, 0});
        static_cast<Processor &>(graphs).process(&xy);
        const auto &vertices = xy.vaChannelVoltage[1];
        QCOMPARE(vertices.size(), size_t(27));
        QVERIFY(vertices.capacity() <= vertices.size() * 3 / 2);
        for (size_t i = 0; i < vertices.size(); ++i) {
            const QVector3D expected(
                float(xy.data(0)->voltage.samples[i] / settings.scope.gain(0) +
                      (settings.scope.trigger.position - 0.5) * DIVS_TIME),
                float(xy.data(1)->voltage.samples[i] / settings.scope.gain(1) +
                      settings.scope.voltage[1].offset), 0.0f);
            QCOMPARE(vertices[i], expected);
        }
    }

    void fftPlanReusePreferencePreserved() {
        ScopeDevice device;
        DsoSettings settings(&device, 0, true);
        settings.alwaysSave = false;
        QVERIFY(settings.analysis.reuseFftPlan);
        settings.analysis.reuseFftPlan = false;
        const auto path = config.filePath("reuse-disabled.ini");
        QVERIFY(settings.saveToFile(path));
        QSettings saved(path, QSettings::IniFormat);
        QVERIFY(saved.contains("scope/analysis/reuseFftPlan"));
        QVERIFY(!saved.value("scope/analysis/reuseFftPlan").toBool());

        DsoSettings restored(&device, 0, true);
        restored.alwaysSave = false;
        QVERIFY(restored.analysis.reuseFftPlan);
        QVERIFY(restored.loadFromFile(path));
        QVERIFY(!restored.analysis.reuseFftPlan);
        const auto roundTrip = config.filePath("reuse-disabled-roundtrip.ini");
        QVERIFY(restored.saveToFile(roundTrip));
        QSettings resaved(roundTrip, QSettings::IniFormat);
        QVERIFY(!resaved.value("scope/analysis/reuseFftPlan", true).toBool());
    }
};

QTEST_MAIN(ResourceRegressionTests)
#include "resource_regression.moc"
