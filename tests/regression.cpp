// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <limits>
#include "dsosettings.h"
#include "exporting/exportcsv.h"
#include "exporting/exportjson.h"
#include "exporting/exporterregistry.h"
#include "post/spectrumgenerator.h"
#include "hantekdso/calibrationsafety.h"
#include "hantekdso/singlecapture.h"
#include "hantekdso/hantekdsocontrol.h"

int verboseLevel = 0;

class RegressionTests : public QObject {
    Q_OBJECT
    QTemporaryDir config;
private slots:
    void initTestCase() {
        QVERIFY(config.isValid());
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, config.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
        QCoreApplication::setOrganizationName("OpenHantekRegression");
        QCoreApplication::setApplicationName("Regression");
    }
    void settingsPreserved() {
        ScopeDevice device;
        DsoSettings settings(&device);
        QSettings global;
        global.setValue("sentinel", "preserve me");
        const auto path = config.filePath("old.ini");
        { QSettings old(path, QSettings::IniFormat); old.setValue("configuration/version", 1);
          old.setValue("sentinel", "valuable"); old.sync(); }
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto before=file.readAll(); file.close();
        const auto previous = settings.scope.horizontal.timebase;
        QVERIFY(!settings.loadFromFile(path));
        QCOMPARE(settings.scope.horizontal.timebase, previous);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), before);
        QCOMPARE(global.value("sentinel").toString(), QString("preserve me"));
        const auto valid = config.filePath("valid.ini");
        settings.scope.horizontal.timebase = .002;
        settings.configVersion = 0;
        QVERIFY(settings.saveToFile(valid));
        QCOMPARE(settings.configVersion, 0u);
        DsoSettings reset(&device, 0, true);
        QVERIFY(reset.loadFromFile(valid));
        QCOMPARE(reset.scope.horizontal.timebase, .002);
        QVERIFY(!settings.saveToFile(config.path()));
    }
    void calibrationBounds() {
        QVERIFY(validCalibrationOffset(125, 130, 1.5));
        QVERIFY(!validCalibrationOffset(0, 0, 0));
        QVERIFY(!validCalibrationOffset(255, 255, 0));
        QVERIFY(!validCalibrationOffset(100, 140, 0));
        QVERIFY(!validCalibrationOffset(128, 128, 21));
        QVERIFY(!validCalibrationOffset(128, 128, std::numeric_limits<double>::quiet_NaN()));
    }
    void singleRequiresNewArm() {
        SingleCapture single;
        QVERIFY(!single.accepts(0));
        const auto first = single.arm();
        QVERIFY(single.accepts(first));
        const auto next = single.arm();
        QVERIFY(!single.accepts(first));
        QVERIFY(single.accepts(next));
    }
    void singleStateMachineRejectsOldBlock() {
        ScopeDevice device; DsoSettings settings(&device);
        HantekDsoControl control(&device, device.getModel(), 0);
        control.applySettings(&settings.scope);
        control.stateMachineRunning = false;
        control.setTriggerMode(Dso::TriggerMode::SINGLE);
        control.enableSamplingUI(true);
        const auto oldArm = control.singleCapture.current();
        control.enableSamplingUI(true);
        control.samplingStarted = true;
        control.controlsettings.samplerate.current = 1e6;
        control.controlsettings.samplerate.target.duration = .001;
        control.controlsettings.trigger.level[0] = 0;
        auto &raw = control.raw;
        raw.valid = true; raw.channels = 2; raw.oversampling = 1; raw.samplerate = 1e6;
        raw.gainIndex[0] = raw.gainIndex[1] = 5;
        raw.data.resize(2*control.grossSampleCount(20000));
        for (size_t i=0; i<raw.data.size()/2; ++i) {
            raw.data[2*i] = uint8_t(128+40*std::sin(2*M_PI*i/100));
            raw.data[2*i+1] = raw.data[2*i];
        }
        raw.tag=1; raw.armGeneration=oldArm;
        control.stateMachine();
        QVERIFY(control.isSamplingUI()); // A trigger in the old acquisition must not stop SINGLE.
        raw.tag=2; raw.armGeneration=control.singleCapture.current();
        QSignalSpy emitted(&control, &HantekDsoControl::samplesAvailable);
        control.stateMachine();
        QVERIFY(!control.isSamplingUI());
        QVERIFY(!emitted.empty());
        auto frame=qvariant_cast<std::shared_ptr<const DSOsamples>>(emitted.back().at(0));
        QCOMPARE(frame->tag, 2u);
        QVERIFY(frame->liveTrigger);
        control.result.data[0][0] = 9876;
        QVERIFY(frame->data[0][0] != 9876); // Queued processing owns its samples.
    }
    void calibrationLocalRoundTripAndDisconnect() {
        ScopeDevice device;
        HantekDsoControl control(&device, device.getModel(), 0);
        const auto *bytes=reinterpret_cast<const unsigned char*>(control.controlsettings.calibrationValues);
        for(size_t i=0; i<sizeof(Hantek::CalibrationValues); ++i) QCOMPARE(bytes[i], uint8_t(255));
        control.replaceCalibrationEEPROM = false;
        control.offsetCorrection[0][0] = 1.23456789;
        control.gainCorrection[0][0] = 1.01234567;
        control.calibrationHasChanged = true;
        control.scopeDevice = nullptr; // Simulate unplugged shutdown, never access USB.
        control.prepareForShutdown();
        QCOMPARE(control.calibrationSettings->value("offset/ch0/20mV").toDouble(), 1.23456789);
        QCOMPARE(control.calibrationSettings->value("gain/ch0/20mV").toDouble(), 1.01234567);
        QCOMPARE(control.calibrationSettings->value("eeprom/replace_eeprom").toBool(), false);
        control.scopeDevice = &device;
        QCOMPARE(control.getCalibrationFromIniFile(), Dso::ErrorCode::NONE);
        QCOMPARE(control.offsetCorrection[0][0], 1.23456789);
    }
    void exportSerialization() {
        ScopeDevice device; DsoSettings settings(&device);
        for (auto &ch : settings.scope.voltage) ch.used = false;
        for (auto &ch : settings.scope.spectrum) ch.used = false;
        settings.scope.voltage[0].used = true;
        settings.scope.voltage[0].name = "a\"b\\c";
        settings.scope.spectrum[0].used = true;
        auto frame = std::make_shared<PPresult>(settings.scope.voltage.size());
        frame->modifiableData(0)->voltage = {{12345.125, std::numeric_limits<double>::infinity(), 3}, .001};
        frame->modifiableData(0)->spectrum = {{-1, -2}, 1000};
        ExporterRegistry registry(device.getModel()->spec(), &settings);
        ExporterJSON json; json.create(&registry); json.samples(frame);
        QBuffer buffer; buffer.open(QIODevice::ReadWrite); QVERIFY(json.write(buffer));
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(buffer.data(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(doc.isArray()); QCOMPARE(doc.array().size(), 3);
        QVERIFY(buffer.data().contains("null"));
        for (const auto &locale : {QLocale(QLocale::English, QLocale::UnitedStates), QLocale(QLocale::German)}) {
            QLocale::setDefault(locale);
            ExporterCSV csv; csv.create(&registry); csv.samples(frame);
            QBuffer out; out.open(QIODevice::ReadWrite); QVERIFY(csv.write(out));
            QVERIFY(out.data().contains("a\"\"b\\c"));
            QVERIFY(!out.data().contains("12,345")); QVERIFY(!out.data().contains("12.345"));
            QVERIFY(!out.data().contains("inf"));
        }
        QLocale::setDefault(QLocale::c());
    }
    void fftPlanResizeAndFloor() {
        ScopeDevice device; DsoSettings settings(&device);
        settings.analysis.spectrumWindow = Dso::WindowFunction::HANN;
        settings.scope.analysis.calculateTHD = true;
        settings.analysis.reuseFftPlan = true;
        SpectrumGenerator generator(&settings.scope, &settings.analysis);
        for (auto n : {20000, 10243, 13311, 20000}) {
            PPresult frame(1);
            auto *data = frame.modifiableData(0); data->voltage.interval = 1e-5;
            for (int i=0; i<n; ++i) {
                const double phase = 2*M_PI*1234*i*1e-5;
                data->voltage.samples.push_back(std::sin(phase) + .01*std::sin(2*phase));
            }
            static_cast<Processor&>(generator).process(&frame);
            QCOMPARE(data->spectrum.samples.size(), size_t(n/2+1));
            QVERIFY(std::abs(data->frequency - 1234) < 2);
            QVERIFY2(std::abs(data->thd - .01) < .001, qPrintable(QString::number(data->thd)));
            const auto thd=data->thd; const auto spectrum=data->spectrum.samples;
            settings.analysis.reuseFftPlan = false;
            SpectrumGenerator fresh(&settings.scope, &settings.analysis);
            static_cast<Processor&>(fresh).process(&frame);
            QCOMPARE(data->spectrum.samples, spectrum);
            settings.analysis.spectrumLimit = -10;
            static_cast<Processor&>(generator).process(&frame);
            QCOMPARE(data->thd, thd);
            settings.analysis.spectrumLimit = -60;
            settings.analysis.reuseFftPlan = true;
        }
    }
};
QTEST_MAIN(RegressionTests)
#include "regression.moc"
