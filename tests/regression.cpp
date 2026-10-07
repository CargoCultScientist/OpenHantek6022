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
#include "lab/capture.h"
#include "lab/capturedock.h"
#include "lab/measurements.h"
#include "lab/mask.h"
#include "lab/measurementlog.h"
#include "post/postprocessing.h"
#include "post/graphgenerator.h"
#include "mainwindow.h"
#include "glscope.h"
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QWheelEvent>
#include <QTabWidget>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QAction>
#include <QScrollArea>
#include <QScrollBar>

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
    void incompatibleDeviceSettingsStayProtected() {
        ScopeDevice device;
        QSettings original(QCoreApplication::organizationName(),device.getModel()->name+"_"+device.getSerialNumber());
        original.clear();original.setValue("configuration/version",99);original.setValue("valuable","keep");original.sync();
        QFile file(original.fileName());QVERIFY(file.open(QIODevice::ReadOnly));const auto before=file.readAll();file.close();
        DsoSettings settings(&device);QVERIFY(!settings.alwaysSave);
        settings.alwaysSave=true; // Loading another valid setup or a UI toggle must not silently destroy this file.
        settings.save();original.sync();
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),before);file.close();
        QVERIFY(settings.saveToFile(config.filePath("replacement.ini")));
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),before);file.close();
        original.clear();original.sync();
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
        const auto previousArm=raw.armGeneration;
        control.enableSamplingUI(true);control.stateMachine();
        raw.tag=3;raw.armGeneration=previousArm;control.stateMachine();
        QVERIFY(control.isSamplingUI());
        raw.tag=4;raw.armGeneration=control.singleCapture.current();control.stateMachine();
        QVERIFY(!control.isSamplingUI());
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
        QVERIFY(doc.array()[2].toObject()["freq"].isNull());
        settings.scope.voltage[0].name="time";settings.scope.spectrum[0].name="time";
        QBuffer collision;collision.open(QIODevice::ReadWrite);QVERIFY(json.write(collision));
        auto row=QJsonDocument::fromJson(collision.data()).array()[0].toObject();
        QCOMPARE(row["time"].toDouble(),0.);
        QCOMPARE(row["time (2)"].toDouble(),12345.125);
        QCOMPARE(row["time (3)"].toDouble(),-1.);
        settings.scope.voltage[0].name="a\"b\\c";
        for (const auto &locale : {QLocale(QLocale::English, QLocale::UnitedStates), QLocale(QLocale::German)}) {
            QLocale::setDefault(locale);
            ExporterCSV csv; csv.create(&registry); csv.samples(frame);
            QBuffer out; out.open(QIODevice::ReadWrite); QVERIFY(csv.write(out));
            QVERIFY(out.data().contains("a\"\"b\\c"));
            QVERIFY(!out.data().contains("12,345")); QVERIFY(!out.data().contains("12.345"));
            QVERIFY(!out.data().contains("inf"));
        }
        QLocale::setDefault(QLocale::c());
        frame->modifiableData(0)->voltageUnit=UNIT_NONE;
        ExporterCSV dimensionless;dimensionless.create(&registry);dimensionless.samples(frame);
        QBuffer logic;logic.open(QIODevice::ReadWrite);QVERIFY(dimensionless.write(logic));
        QVERIFY(logic.data().contains(" / 1\""));
        frame->modifiableData(0)->voltageUnit=UNIT_VOLTS;
        class FailingDevice : public QIODevice {
            qint64 readData(char*,qint64) override {return -1;}
            qint64 writeData(const char*,qint64) override {return -1;}
        } failure;
        failure.open(QIODevice::WriteOnly);QVERIFY(!json.write(failure));
        ExporterCSV csv;csv.create(&registry);csv.samples(frame);QVERIFY(!csv.write(failure));
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
    void measurementsKnownSignals() {
        std::vector<double> sine;
        const double dt=1e-6;
        for(int i=0;i<10000;++i) sine.push_back(1+2*std::sin(2*M_PI*1000*i*dt));
        auto m=Lab::measure(sine,dt);
        QCOMPARE(m.count,size_t(10000));
        QVERIFY(std::abs(m.mean-1)<1e-12);
        QVERIFY(std::abs(m.rms-std::sqrt(3))<1e-12);
        QVERIFY(std::abs(m.vpp-4)<1e-12);
        QVERIFY(std::abs(m.frequency-1000)<1e-6);
        QVERIFY(std::abs(m.duty-.5)<1e-8);
        QVERIFY(std::abs(m.rise-(2*std::asin(.8)/(2*M_PI*1000)))<2e-8);
        QVERIFY(std::abs(m.rise-m.fall)<1e-12);
        QVERIFY(!std::isfinite(Lab::measure(sine,dt,false).frequency));
        std::vector<double> square;
        for(int i=0;i<10000;++i) square.push_back(i%100<25?3.3:0);
        m=Lab::measure(square,dt);
        QVERIFY(std::abs(m.frequency-10000)<1e-8);
        QVERIFY(std::abs(m.duty-.25)<1e-12);
        QVERIFY(std::abs(m.positiveWidth-25e-6)<1e-12);
        QVERIFY(std::abs(m.negativeWidth-75e-6)<1e-12);
        QVERIFY(std::abs(m.negativeDuty-.75)<1e-12);
        QVERIFY(!std::isfinite(m.rise)); QVERIFY(m.undersampled);
        for(auto &v:square) v=2;
        m=Lab::measure(square,dt);
        QCOMPARE(m.rms,2.); QVERIFY(!std::isfinite(m.frequency));
        square[2]=std::numeric_limits<double>::quiet_NaN();
        QCOMPARE(Lab::measure(square,dt).count,size_t(0));
        Lab::RunningStatistic stats; stats.add(1);stats.add(2);stats.add(3);stats.add(Lab::unavailable);
        QCOMPARE(stats.count,size_t(3)); QCOMPARE(stats.mean,2.); QCOMPARE(stats.deviation(),1.);
    }
    void measurementSpansAndCycles() {
        auto range=Lab::sampleRange(1001,.001,-.3,{-.2,-.1});
        QCOMPARE(range.begin,size_t(100)); QCOMPARE(range.end,size_t(201));
        range=Lab::sampleRange(1001,.001,0,{.20001,.39999});
        QCOMPARE(range.begin,size_t(201)); QCOMPARE(range.end,size_t(400));
        range=Lab::sampleRange(1001,.001,0,{.2,.2});
        QCOMPARE(range.begin,size_t(200)); QCOMPARE(range.end,size_t(201));
        for(const auto span:{Lab::TimeSpan{2,3},Lab::TimeSpan{-3,-2},Lab::TimeSpan{.4,.2},Lab::TimeSpan{Lab::unavailable,1}}) {
            range=Lab::sampleRange(1001,.001,0,span); QCOMPARE(range.begin,range.end);
        }
        range=Lab::sampleRange(1001,.001,0); QCOMPARE(range.begin,size_t(0)); QCOMPARE(range.end,size_t(1001));
        std::vector<double> ramp;
        for(int i=0;i<1001;++i) ramp.push_back(i);
        ramp[0]=Lab::unavailable; // An invalid sample outside the gate cannot contaminate it.
        range=Lab::sampleRange(ramp.size(),.001,0,{.2,.4});
        auto m=Lab::measure(ramp,.001,true,range);
        QCOMPARE(m.count,size_t(201)); QCOMPARE(m.minimum,200.); QCOMPARE(m.maximum,400.);
        QCOMPARE(m.mean,300.); QCOMPARE(m.vpp,200.); QCOMPARE(m.span,.2);
        m=Lab::measure(ramp,.001,true,{200,201});
        QCOMPARE(m.count,size_t(1)); QCOMPARE(m.mean,200.); QCOMPARE(m.span,0.); QVERIFY(!std::isfinite(m.frequency));
        QCOMPARE(Lab::measure(ramp,.001,true,{5000,6000}).count,size_t(0));
        std::vector<double> sine;
        for(int i=0;i<3503;++i) sine.push_back(1+2*std::sin(2*M_PI*i/1000));
        m=Lab::measure(sine,1e-6);
        QCOMPARE(m.cycles,size_t(2)); QVERIFY(std::abs(m.cycleMean-1)<1e-12);
        QVERIFY(std::abs(m.cycleRms-std::sqrt(3))<1e-5);
        QVERIFY(std::abs(m.cycleSpan-.002)<1e-12);
        QVERIFY(std::abs(m.mean-m.cycleMean)>.1); // Partial cycles affect record mean, not cycle mean.
        QVERIFY(!std::isfinite(Lab::measure(sine,1e-6,false).cycleRms));
        QVERIFY(!std::isfinite(Lab::measure(sine,1e-6,true,{0,100}).cycleRms));
        // A piecewise-linear triangle has an exact analytic cycle RMS.
        std::vector<double> triangle;
        for(int i=0;i<350;++i) triangle.push_back(2-std::abs(i%100-50)/25.);
        m=Lab::measure(triangle,.001);
        QVERIFY(std::abs(m.cycleMean-1)<1e-12);
        QVERIFY(std::abs(m.cycleRms-std::sqrt(4./3))<1e-12);
        QCOMPARE(Lab::measure({2,2,2},.1).crestFactor,1.);
        QVERIFY(!std::isfinite(Lab::measure({0,0,0},.1).crestFactor));
    }
    void statisticsResetAndUnits() {
        Lab::Capture frame; frame.tag=1; frame.capturedAtMs=100;
        frame.channels.push_back({"A",UNIT_VOLTS,{{0,1,2,3,4},1},true});
        Lab::CaptureStatistics stats;
        stats.add(frame); stats.add(frame);
        QCOMPARE(stats.channels[0].vpp.count,size_t(1));
        ++frame.tag; frame.channels[0].signal.samples={0,2,4,6,8}; stats.add(frame);
        QCOMPARE(stats.channels[0].vpp.count,size_t(2)); QCOMPARE(stats.channels[0].vpp.mean,6.);
        QCOMPARE(stats.channels[0].vpp.minimum,4.); QCOMPARE(stats.channels[0].vpp.maximum,8.);
        auto old=frame;
        ++frame.tag; frame.channels[0].unit=UNIT_VOLTSQUARE; stats.add(frame);
        QCOMPARE(stats.channels[0].vpp.count,size_t(1)); QVERIFY(!stats.matches(old));
        // A different math operation can have the same unit: its metadata must also reset the run.
        ++frame.tag; frame.metadata={{"channelSettings",QJsonArray{QJsonObject{{"couplingOrMathIndex",2}}}}}; stats.add(frame);
        QCOMPARE(stats.channels[0].vpp.count,size_t(1));
        ++frame.tag; frame.channels[0].valid=false; stats.add(frame);
        QCOMPARE(stats.channels[0].vpp.count,size_t(1));
        ++frame.tag; frame.channels[0].valid=true; stats.add(frame,{1,3});
        QCOMPARE(stats.channels[0].vpp.count,size_t(1)); QCOMPARE(stats.channels[0].vpp.mean,4.);
        QVERIFY(stats.matches(frame,{1,3})); QVERIFY(!stats.matches(frame));
        ++frame.tag; frame.channels[0].signal.interval=.5; stats.add(frame,{1,3});
        QCOMPARE(stats.channels[0].vpp.count,size_t(1));
        stats.clear(); QCOMPARE(stats.channels[0].vpp.count,size_t(0)); QVERIFY(!stats.matches(frame,{1,3}));
    }
    void fftOddLastBinAndTinyWindow() {
        ScopeDevice device;DsoSettings settings(&device);
        settings.analysis.spectrumWindow=Dso::WindowFunction::RECTANGULAR;
        SpectrumGenerator generator(&settings.scope,&settings.analysis);
        PPresult frame(1);auto *data=frame.modifiableData(0);data->voltage.interval=1e-6;
        for(int i=0;i<101;++i) data->voltage.samples.push_back(std::sin(2*M_PI*50*i/101+.7));
        static_cast<Processor&>(generator).process(&frame);
        QVERIFY(std::abs(data->spectrum.samples.back()-20*std::log10(std::sqrt(.5)))<1e-9);
        settings.analysis.spectrumWindow=Dso::WindowFunction::HANN;
        data->voltage.samples={-1,1};static_cast<Processor&>(generator).process(&frame);
        QVERIFY(std::all_of(data->spectrum.samples.begin(),data->spectrum.samples.end(),[](double v){return std::isfinite(v);}));
    }
    void shortRecordInterpolatedFrequency() {
        std::vector<double> sine;
        for(int i=0;i<93;++i) sine.push_back(std::sin(2*M_PI*5.3*i/93));
        const auto m=Lab::measure(sine,1e-6);
        QVERIFY(std::abs(m.frequency-5.3e6/93)/(5.3e6/93)<.001);
        sine.clear();
        for(int i=0;i<100;++i) sine.push_back(std::sin(2*M_PI*i/5));
        QVERIFY(!std::isfinite(Lab::measure(sine,1e-6).frequency));
    }
    void captureRoundTripAndLimits() {
        Lab::Capture source;
        source.tag=42;source.capturedAtMs=123456789;source.triggerPosition=1;source.triggered=true;
        source.channels.push_back({"CH\"1",UNIT_VOLTS,{{.1,.2,.3},.001},true});
        source.channels.push_back({"logic",UNIT_NONE,{{0,1,0},.001},true});
        source.metadata={{"note","test"}};
        QString error; const auto path=config.filePath("record.ohl.json");
        QVERIFY2(source.save(path,error),qPrintable(error));
        auto copy=Lab::Capture::load(path,error); QVERIFY2(copy,qPrintable(error));
        QCOMPARE(copy->tag,42u); QCOMPARE(copy->capturedAtMs,source.capturedAtMs);
        QCOMPARE(copy->channels[0].signal.samples,source.channels[0].signal.samples);
        QCOMPARE(copy->metadata,source.metadata);
        QCOMPARE(copy->channels[1].unit,UNIT_NONE);
        QCOMPARE(copy->channels[1].signal.samples,source.channels[1].signal.samples);
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto original=file.readAll();file.close();
        source.channels[0].signal.samples[0]=Lab::unavailable;
        QVERIFY(!source.save(path,error)); // Failed serialization preserves an existing capture.
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),original);file.close();
        auto root=QJsonDocument::fromJson(original).object();
        root["version"]=99;
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(root).toJson());file.close();
        QVERIFY(!Lab::Capture::load(path,error));
        root["version"]=1; root["triggerPosition"]=999;
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(root).toJson());file.close();
        QVERIFY(!Lab::Capture::load(path,error));
        Lab::CaptureHistory history(4096,2);
        for(unsigned tag:{1,2,5}) {auto frame=std::make_shared<Lab::Capture>(*copy);frame->tag=tag;QVERIFY(history.append(frame));}
        QCOMPARE(history.frames().size(),size_t(2)); QCOMPARE(history.skippedTags(),quint64(2));
        QVERIFY(!history.append(history.frames().back())); QVERIFY(history.bytes()<=4096);
        history.clear();QCOMPARE(history.bytes(),size_t(0));QCOMPARE(history.skippedTags(),quint64(0));
        Lab::CaptureHistory tiny(1);QVERIFY(!tiny.append(copy));
    }
    void referenceComparison() {
        Lab::Capture a,b;
        a.channels.push_back({"A",UNIT_VOLTS,{{1,2,3,4,5},1},true});
        b.channels.push_back({"B",UNIT_VOLTS,{{0,2,4},2},true});
        auto d=Lab::compare(a,b,0,false);
        QCOMPARE(d.count,size_t(5));QCOMPARE(d.rms,1.);QCOMPARE(d.maximum,1.);
        d=Lab::compare(a,b,0,false,0,{1,3});
        QCOMPARE(d.count,size_t(3));QCOMPARE(d.rms,1.);
        d=Lab::compare(a,b,0,false,0,{100,200});
        QCOMPARE(d.count,size_t(0));QVERIFY(d.error.contains("span"));
        d=Lab::compare(a,b,0,true);QVERIFY(!d.error.isEmpty());
        d=Lab::compare(a,b,0,false,10);QCOMPARE(d.count,size_t(0));
        b.channels[0].unit=UNIT_VOLTSQUARE;
        QVERIFY(!Lab::compare(a,b,0,false).error.isEmpty());
        b.channels[0].unit=UNIT_VOLTS;
        a.channels[0].signal.samples={0,1,2,3,4};b.channels[0].signal.samples={1,3,5};
        a.triggered=b.triggered=true;a.triggerPosition=1;b.triggerPosition=0;
        d=Lab::compare(a,b,0,true);QCOMPARE(d.count,size_t(4));QCOMPARE(d.rms,0.);
    }
    void analysisMailboxKeepsLatest() {
        PostProcessing processing(3);
        QSignalSpy output(&processing,&PostProcessing::processingFinished);
        for(unsigned tag=1;tag<=20;++tag) {
            auto frame=std::make_shared<DSOsamples>();frame->data={{1,2,3},{1,2,3},{}};frame->samplerate=1000;frame->tag=tag;
            processing.enqueue(frame);
        }
        QTRY_COMPARE(output.count(),1);
        const auto latest=qvariant_cast<std::shared_ptr<PPresult>>(output[0][0]);
        QCOMPARE(latest->tag,20u);
    }
    void referenceMaskSafety() {
        Lab::Capture reference;
        reference.channels.push_back({"CH1",UNIT_VOLTS,{{0,1,0,-1,0},1},true});
        Lab::Capture current=reference;
        Lab::MaskSpec spec{0,0,5}; // 5% of 2 Vpp = ±0.1 V, including at zero crossings.
        auto result=Lab::testMask(current,&reference,spec);
        QCOMPARE(result.state,Lab::MaskState::Pass); QCOMPARE(result.tested,size_t(5)); QCOMPARE(result.tolerance,.1);
        current.channels[0].signal.samples[1]+= .1;
        QCOMPARE(Lab::testMask(current,&reference,spec).state,Lab::MaskState::Pass);
        current.channels[0].signal.samples[2]=.11;
        result=Lab::testMask(current,&reference,spec);
        QCOMPARE(result.state,Lab::MaskState::Fail); QCOMPARE(result.outside,size_t(1));
        QCOMPARE(Lab::testMask(current,&reference,spec,false,1).state,Lab::MaskState::Untestable); // Partial overlap never passes.
        current=reference; current.channels[0].valid=false;
        QCOMPARE(Lab::testMask(current,&reference,spec).state,Lab::MaskState::Untestable);
        current=reference; current.channels[0].unit=UNIT_WATTS;
        QCOMPARE(Lab::testMask(current,&reference,spec).state,Lab::MaskState::Untestable);
        current=reference; current.channels[0].signal.samples[3]=Lab::unavailable;
        QCOMPARE(Lab::testMask(current,&reference,spec).state,Lab::MaskState::Untestable);
        current=reference;
        QCOMPARE(Lab::testMask(current,nullptr,spec).state,Lab::MaskState::Untestable);
        QCOMPARE(Lab::testMask(current,&reference,spec,true).state,Lab::MaskState::Untestable);
        current.triggered=reference.triggered=true; current.triggerPosition=reference.triggerPosition=2;
        QCOMPARE(Lab::testMask(current,&reference,spec,true).state,Lab::MaskState::Pass);
        current.channels[0].signal={{0,.5,1,.5,0,-.5,-1,-.5,0},.5};
        QCOMPARE(Lab::testMask(current,&reference,spec).state,Lab::MaskState::Pass); // Different sample grid.
        spec.absoluteTolerance=-1;
        QCOMPARE(Lab::testMask(current,&reference,spec).state,Lab::MaskState::Untestable);
        spec={0,0,0}; reference.channels[0].signal.samples={0,0,0,0,0}; current=reference;
        current.channels[0].signal.samples[2]=1e-15;
        QCOMPARE(Lab::testMask(current,&reference,spec).state,Lab::MaskState::Fail); // No hidden voltage floor.
    }
    void maskBrowserInteraction() {
        ScopeDevice device; DsoSettings settings(&device);
        Lab::CaptureDock dock(&settings); dock.resize(1500,800); dock.show();
        auto frame=std::make_shared<PPresult>(1); frame->tag=1; frame->capturedAtMs=100;
        frame->modifiableData(0)->voltage={{0,1,0,-1,0},.001}; dock.ingest(frame);
        for(auto button:dock.findChildren<QPushButton*>()) if(button->text()=="Set reference") button->click();
        auto enable=dock.findChild<QCheckBox*>("labMaskEnabled");
        auto freeze=dock.findChild<QCheckBox*>("labFreezeOnFailure");
        auto filter=dock.findChild<QComboBox*>("labHistoryFilter");
        auto summary=dock.findChild<QLabel*>("labMaskSummary");
        auto badge=dock.findChild<QLabel*>("labSelectionBadge");
        auto list=dock.findChild<QListWidget*>();
        QVERIFY(enable && freeze && filter && summary && badge && list);
        enable->setChecked(true); freeze->setChecked(true);
        frame=std::make_shared<PPresult>(*frame); frame->tag=2; dock.ingest(frame);
        QVERIFY(summary->text().contains("1 pass / 0 fail"));
        frame=std::make_shared<PPresult>(*frame); frame->tag=3; frame->modifiableData(0)->voltage.samples[2]=.5; dock.ingest(frame);
        QVERIFY(summary->text().contains("1 pass / 1 fail")); QVERIFY(badge->text().contains("FROZEN"));
        dock.ingest(frame); QVERIFY(summary->text().contains("1 pass / 1 fail"));
        filter->setCurrentIndex(1);
        QVERIFY(list->item(0)->isHidden()); QVERIFY(list->item(1)->isHidden()); QVERIFY(!list->item(2)->isHidden());
        list->setCurrentRow(0); list->setCurrentRow(2);
        QVERIFY(summary->text().contains("1 pass / 1 fail")); // Browsing/retesting is not a new acquisition.
        dock.findChild<QDoubleSpinBox*>("labMaskTolerance")->setValue(30);
        QVERIFY(list->item(2)->isHidden()); QVERIFY(summary->text().contains("0 pass / 0 fail"));
    }
    void captureBrowserInteraction() {
        ScopeDevice device;DsoSettings settings(&device);
        Lab::CaptureDock dock(&settings); dock.resize(1500,650);dock.show();
        auto frame=std::make_shared<PPresult>(3);frame->tag=1;frame->capturedAtMs=1000;
        for(unsigned ch=0;ch<2;++ch) {
            auto *data=frame->modifiableData(ch);data->voltage.interval=1e-6;
            for(int i=0;i<1000;++i) data->voltage.samples.push_back(std::sin(2*M_PI*i/100));
        }
        dock.ingest(frame);frame=std::make_shared<PPresult>(*frame);frame->tag=2;dock.ingest(frame);
        auto list=dock.findChild<QListWidget*>();QVERIFY(list);QCOMPARE(list->count(),2);
        list->setCurrentRow(0);
        auto table=dock.findChild<QTableWidget*>();QVERIFY(table);
        for(auto button:dock.findChildren<QPushButton*>()) if(button->text()=="Set reference") QTest::mouseClick(button,Qt::LeftButton);
        list->setCurrentRow(1);
        QVERIFY(std::abs(stringToValue(table->item(0,9)->text(),UNIT_VOLTS))<1e-10);
        dock.ingest(frame);QCOMPARE(list->count(),2); // stopped redraw does not grow history
        if(qEnvironmentVariableIsSet("OH_CAPTURE_SCREENSHOT")) {
            QTest::qWait(100);
            QVERIFY(dock.grab().save(qEnvironmentVariable("OH_CAPTURE_SCREENSHOT")));
        }
    }
    void measurementLogSegmentsAndCsv() {
        Lab::Capture frame; frame.tag=1; frame.capturedAtMs=1000;
        frame.channels.push_back({"=SUM(1,2)\"\nCH1",UNIT_VOLTS,{{0,1,0,-1,0},.001},true});
        Lab::MeasurementLog log(3); Lab::LogOptions options;
        QVERIFY(log.append(frame,options)); QVERIFY(!log.append(frame,options));
        auto next=[&]{++frame.tag; frame.capturedAtMs+=100;};
        next(); QVERIFY(log.append(frame,options)); QCOMPARE(log.entries().back().segment,quint64(1));
        options.span={.001,.003}; next(); QVERIFY(log.append(frame,options));
        QCOMPARE(log.entries().back().segment,quint64(2)); QCOMPARE(log.entries().back().values.count,size_t(3));
        frame.channels[0].unit=UNIT_NONE; next(); QVERIFY(log.append(frame,options));
        QCOMPARE(log.entries().back().segment,quint64(3)); QCOMPARE(log.entries().size(),size_t(3)); QCOMPARE(log.evicted(),quint64(1));
        options.intervalMs=500; next(); QVERIFY(log.append(frame,options));
        const auto segment=log.entries().back().segment;
        next(); QVERIFY(!log.append(frame,options)); frame.capturedAtMs+=400; ++frame.tag; QVERIFY(log.append(frame,options));
        QCOMPARE(log.entries().back().segment,segment);
        frame.capturedAtMs=500; ++frame.tag; QVERIFY(log.append(frame,options));
        QCOMPARE(log.entries().back().segment,segment+1); // A backward wall clock must not join the previous trend.
        log.breakSegment(); next(); QVERIFY(log.append(frame,options)); QCOMPARE(log.entries().back().segment,segment+2);
        QLocale::setDefault(QLocale(QLocale::German));
        QBuffer buffer; buffer.open(QIODevice::ReadWrite); QVERIFY(log.writeCsv(buffer));
        QLocale::setDefault(QLocale::c());
        QVERIFY(buffer.data().startsWith("timestamp_utc,tag,segment"));
        QVERIFY(buffer.data().contains("\"'=SUM(1,2)\"\"\nCH1\"")); // Text escaped and spreadsheet formula neutralized.
        QVERIFY(buffer.data().contains("1970-01-01T00:00:00.600Z"));
        // Parse RFC-style quoted fields, including the embedded newline/quote in the channel name.
        QList<QStringList> csvRows; QStringList fields; QString field; bool quoted=false;
        const auto csv=QString::fromUtf8(buffer.data());
        for(int i=0;i<csv.size();++i) {
            const auto ch=csv[i];
            if(ch=='"') {
                if(quoted && i+1<csv.size() && csv[i+1]=='"') {field+='"'; ++i;} else quoted=!quoted;
            } else if(!quoted && ch==',') {fields<<field; field.clear();}
            else if(!quoted && ch=='\n') {fields<<field; csvRows<<fields; fields.clear();field.clear();}
            else if(quoted || ch!='\r') field+=ch;
        }
        QVERIFY(!quoted); QCOMPARE(csvRows.size(),4);
        for(const auto &row:csvRows) QCOMPARE(row.size(),29);
        QCOMPARE(csvRows.back()[3],QString("'=SUM(1,2)\"\nCH1"));
        QCOMPARE(csvRows.back()[4],QString("1")); QCOMPARE(csvRows.back()[5],QString("3"));
        QCOMPARE(csvRows.back()[6].toDouble(),.001); QCOMPARE(csvRows.back()[7].toDouble(),.003);
        QVERIFY(buffer.data().contains("displayed acquisitions only; gaps possible"));
        QVERIFY(!buffer.data().contains("nan"));
        QString error; const auto path=config.filePath("measurements.csv"); QVERIFY2(log.saveCsv(path,error),qPrintable(error));
        QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(),buffer.data());
        QVERIFY(!log.saveCsv(config.path(),error));
        class FailingDevice : public QIODevice {
            qint64 readData(char*,qint64) override {return -1;}
            qint64 writeData(const char*,qint64) override {return -1;}
        } failing;
        failing.open(QIODevice::WriteOnly); QVERIFY(!log.writeCsv(failing));
        log.clear(); QVERIFY(log.entries().empty()); QCOMPARE(log.evicted(),quint64(0));
    }
    void measurementLoggingIndependentOfHistory() {
        ScopeDevice device; DsoSettings settings(&device);
        Lab::CaptureDock dock(&settings); dock.resize(1500,800); dock.show();
        auto collect=dock.findChild<QCheckBox*>("labCollectLog");
        auto summary=dock.findChild<QLabel*>("labLogSummary"); auto list=dock.findChild<QListWidget*>();
        QVERIFY(collect && summary && list);
        for(auto checkbox:dock.findChildren<QCheckBox*>()) if(checkbox->text()=="Record history") checkbox->setChecked(false);
        collect->setChecked(true);
        auto frame=std::make_shared<PPresult>(1); frame->tag=1; frame->capturedAtMs=1000;
        frame->modifiableData(0)->voltage={{0,1,0,-1,0},.001}; dock.ingest(frame);
        QCOMPARE(list->count(),0); QVERIFY(summary->text().contains("1 / 10,000"));
        dock.ingest(frame); QVERIFY(summary->text().contains("1 / 10,000"));
        collect->setChecked(false); frame=std::make_shared<PPresult>(*frame);frame->tag=2;dock.ingest(frame);
        QVERIFY(summary->text().contains("1 / 10,000"));
        collect->setChecked(true); dock.ingest(frame); QVERIFY(summary->text().contains("1 / 10,000")); // Paused redraw is not new.
        frame=std::make_shared<PPresult>(*frame);frame->tag=3;dock.ingest(frame); QVERIFY(summary->text().contains("2 / 10,000"));
        QTimer::singleShot(0,[] {
            auto dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(dialog); dialog->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!dock.confirmDiscardLog()); QVERIFY(collect->isChecked());
        QVERIFY(summary->text().contains("2 / 10,000"));
        QTimer::singleShot(0,[] {
            auto dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(dialog); dialog->button(QMessageBox::Discard)->click();
        });
        dock.findChild<QPushButton*>("labClearLog")->click(); QVERIFY(summary->text().contains("0 / 10,000"));
        QVERIFY(collect->isChecked()); QVERIFY(dock.confirmDiscardLog());
    }
    void workbenchVisualSmoke() {
        ScopeDevice device; DsoSettings settings(&device);
        Lab::CaptureDock dock(&settings); dock.resize(1450,850); dock.show();
        auto frame=std::make_shared<PPresult>(2); frame->tag=1; frame->capturedAtMs=QDateTime::currentMSecsSinceEpoch();
        for(unsigned channel=0;channel<2;++channel) {
            auto *data=frame->modifiableData(channel); data->voltage.interval=1e-6;
            for(int i=0;i<2000;++i) data->voltage.samples.push_back((channel?.7:1)*std::sin(2*M_PI*i/200+channel*.4));
        }
        dock.ingest(frame);
        for(auto button:dock.findChildren<QPushButton*>()) if(button->text()=="Set reference") button->click();
        dock.findChild<QCheckBox*>("labMaskEnabled")->setChecked(true);
        dock.findChild<QCheckBox*>("labCollectLog")->setChecked(true);
        for(unsigned tag=2;tag<=42;++tag) {
            auto next=std::make_shared<PPresult>(*frame); next->tag=tag;next->capturedAtMs+=tag*100;
            for(auto &sample:next->modifiableData(0)->voltage.samples) sample*=1+.006*tag;
            dock.ingest(next);
        }
        dock.findChild<QTabWidget*>("labControls")->setCurrentIndex(2);
        QCoreApplication::processEvents();
        if(qEnvironmentVariableIsSet("OH_WORKBENCH_SCREENSHOT")) {
            QVERIFY(dock.grab().save(qEnvironmentVariable("OH_WORKBENCH_SCREENSHOT")));
            dock.findChild<QPushButton*>("labConfigure")->click();
            QCoreApplication::processEvents();
            QVERIFY(dock.grab().save(qEnvironmentVariable("OH_WORKBENCH_SCREENSHOT")+".settings.png"));
            dock.findChild<QPushButton*>("labFocusView")->click();
            QCoreApplication::processEvents();
            QVERIFY(dock.grab().save(qEnvironmentVariable("OH_WORKBENCH_SCREENSHOT")+".focus.png"));
            dock.findChild<QPushButton*>("labFocusView")->click();
            dock.findChild<QTabWidget*>("labControls")->setCurrentIndex(3);
            dock.findChild<QTabWidget*>("labViews")->setCurrentIndex(1);
            QCoreApplication::processEvents();
            QVERIFY(dock.grab().save(qEnvironmentVariable("OH_WORKBENCH_SCREENSHOT")+".trend.png"));
        }
        dock.resize(1100,700); QCoreApplication::processEvents();
        QVERIFY(dock.width()<=1100); QVERIFY(dock.height()<=700);
    }
    void instrumentUiInteraction() {
        ScopeDevice device; DsoSettings settings(&device);
        settings.scope.voltage[0].name="<b>Supply</b> & " + QString(160,'x');
        Lab::CaptureDock dock(&settings); dock.resize(1100,700); dock.show();
        auto configure=dock.findChild<QPushButton*>("labConfigure");
        auto focus=dock.findChild<QPushButton*>("labFocusView");
        auto tabs=dock.findChild<QTabWidget*>("labControls");
        auto sidebar=dock.findChild<QWidget*>("labHistorySidebar");
        auto save=dock.findChild<QPushButton*>("labSaveCapture");
        auto pin=dock.findChild<QPushButton*>("labSetReference");
        auto plot=dock.findChild<QWidget*>("labCapturePlot");
        auto scroll=dock.findChild<QScrollArea*>("labWorkbenchScroll");
        auto badge=dock.findChild<QLabel*>("labSelectionBadge");
        auto follow=dock.findChild<QCheckBox*>("labFollowLatest");
        auto record=dock.findChild<QCheckBox*>("labRecordHistory");
        auto collect=dock.findChild<QCheckBox*>("labCollectLog");
        auto log=dock.findChild<QLabel*>("labLogBadge");
        QVERIFY(configure && focus && tabs && sidebar && save && pin && plot && scroll && badge && follow && record && collect && log);
        QVERIFY(tabs->isHidden()); QVERIFY(!save->isEnabled()); QVERIFY(!pin->isEnabled());
        auto frame=std::make_shared<PPresult>(3); frame->tag=1; frame->capturedAtMs=1000;
        for(unsigned channel=0;channel<3;++channel) {
            auto data=frame->modifiableData(channel); data->voltage.interval=1e-6;
            for(int i=0;i<1000;++i) data->voltage.samples.push_back(std::sin(2*M_PI*i/100));
        }
        collect->setChecked(true); dock.ingest(frame); QCoreApplication::processEvents();
        QVERIFY(save->isEnabled()); QVERIFY(pin->isEnabled());
        QVERIFY(badge->text().contains("FOLLOWING LATEST")); QVERIFY(!badge->text().contains("LIVE"));
        QVERIFY(log->isVisible()); QVERIFY(log->text().contains("collecting")); QVERIFY(log->text().contains("unexported"));
        auto name=dock.findChild<QLabel*>("labMetricName0"); QVERIFY(name);
        QCOMPARE(name->text(),settings.scope.voltage[0].name); QCOMPARE(name->textFormat(),Qt::PlainText);
        // With all three channels and a long imported name, the default view fits
        // without scrolling and spends at least half its height on the waveform.
        QCOMPARE(scroll->horizontalScrollBar()->maximum(),0); QCOMPARE(scroll->verticalScrollBar()->maximum(),0);
        QVERIFY2(plot->height()>=dock.height()/2,qPrintable(QString::number(plot->height())));
        const int normalWidth=plot->width(), normalHeight=plot->height();
        configure->setFocus(); QTest::keyClick(configure,Qt::Key_Space); QCoreApplication::processEvents();
        QVERIFY(tabs->isVisible()); QVERIFY(plot->height()<normalHeight);
        tabs->setCurrentIndex(2);
        focus->click(); QCoreApplication::processEvents();
        QVERIFY(sidebar->isHidden()); QVERIFY(tabs->isHidden()); QVERIFY(!configure->isEnabled());
        QVERIFY(plot->width()>normalWidth); QVERIFY(log->isVisible()); QVERIFY(collect->isChecked()); QVERIFY(record->isChecked());
        focus->click(); QCoreApplication::processEvents();
        QVERIFY(sidebar->isVisible()); QVERIFY(tabs->isVisible()); QCOMPARE(tabs->currentIndex(),2); QVERIFY(configure->isChecked());
        configure->click();
        follow->setChecked(false); QVERIFY(badge->text().contains("VIEW FROZEN"));
        follow->setChecked(true); QVERIFY(badge->text().contains("FOLLOWING LATEST"));
        pin->click();
        const QPointF centre=plot->rect().center();
        QWheelEvent wheel(centre,plot->mapToGlobal(centre.toPoint()),QPoint(),QPoint(0,1200),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(plot,&wheel);
        dock.findChild<QComboBox*>("labMeasurementSpan")->setCurrentIndex(1);
        auto table=dock.findChild<QTableWidget*>("labMeasurements");
        QVERIFY(!table->item(0,0)->text().contains("1000 samples"));
        dock.findChild<QPushButton*>("labFitRecord")->click(); QVERIFY(table->item(0,0)->text().contains("1000 samples"));
        dock.findChild<QPushButton*>("labMetricDetails1")->click();
        QCOMPARE(dock.findChild<QTabWidget*>("labViews")->currentWidget(),table); QCOMPARE(table->currentRow(),1);
        auto clear=dock.findChild<QAction*>("labClearHistory"); QVERIFY(clear);
        auto list=dock.findChild<QListWidget*>("labCaptureList");
        QTimer::singleShot(0,[&]{
            auto dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(dialog);
            QVERIFY(!record->isChecked()); QVERIFY(collect->isChecked()); dialog->button(QMessageBox::Cancel)->click();
        });
        clear->trigger(); QCOMPARE(list->count(),1); QVERIFY(record->isChecked());
        QTimer::singleShot(0,[]{
            auto dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); QVERIFY(dialog);
            dialog->button(QMessageBox::Yes)->click();
        });
        clear->trigger(); QCOMPARE(list->count(),0); QVERIFY(record->isChecked()); QVERIFY(!save->isEnabled());
        QVERIFY(log->text().contains("1 in RAM")); // Clearing history never clears the log or reference.
        frame=std::make_shared<PPresult>(*frame); frame->tag=2; frame->capturedAtMs+=10;
        frame->modifiableData(0)->valid=false; dock.ingest(frame);
        QVERIFY(dock.findChild<QLabel*>("labMetricStatus0")->text().contains("CLIPPED"));
        QVERIFY(table->item(1,9)->text()!="—");
    }
    void captureBrowserMeasurementControls() {
        ScopeDevice device; DsoSettings settings(&device);
        Lab::CaptureDock dock(&settings); dock.resize(1500,800); dock.show();
        auto frame=std::make_shared<PPresult>(1); frame->tag=1; frame->capturedAtMs=100;
        auto data=frame->modifiableData(0); data->voltage.interval=1e-6;
        for(int i=0;i<1000;++i) data->voltage.samples.push_back(i);
        dock.ingest(frame);
        auto mode=dock.findChild<QComboBox*>("labMeasurementSpan");
        auto a=dock.findChild<QDoubleSpinBox*>("labCursorA"); auto b=dock.findChild<QDoubleSpinBox*>("labCursorB");
        auto table=dock.findChild<QTableWidget*>("labMeasurements");
        auto plot=dock.findChild<QWidget*>("labCapturePlot");
        auto reset=dock.findChild<QPushButton*>("labResetStatistics");
        QVERIFY(mode && a && b && table && plot && reset);
        QVERIFY(table->item(0,12)->text().contains("n=1"));
        for(auto button:dock.findChildren<QPushButton*>()) if(button->text()=="Set reference") button->click();
        frame=std::make_shared<PPresult>(*frame); frame->tag=2;
        frame->modifiableData(0)->voltage.samples[0]=9999; // Difference entirely outside the cursor gate.
        dock.ingest(frame); QVERIFY(table->item(0,12)->text().contains("n=2"));
        a->setValue(.0002); b->setValue(.0001); mode->setCurrentIndex(2); // Reversed cursors are accepted.
        QVERIFY(a->isEnabled()); QVERIFY(table->item(0,0)->text().contains("101 samples"));
        QCOMPARE(stringToValue(table->item(0,1)->text(),UNIT_VOLTS),100.);
        QCOMPARE(stringToValue(table->item(0,2)->text(),UNIT_VOLTS),150.);
        QCOMPARE(stringToValue(table->item(0,9)->text(),UNIT_VOLTS),0.);
        QVERIFY(!table->item(0,12)->text().contains("n=2"));
        frame=std::make_shared<PPresult>(*frame); frame->tag=3; dock.ingest(frame);
        QVERIFY(table->item(0,12)->text().contains("n=1"));
        auto list=dock.findChild<QListWidget*>(); list->setCurrentRow(0); list->setCurrentRow(2);
        QVERIFY(table->item(0,12)->text().contains("n=1"));
        reset->click(); QVERIFY(!table->item(0,12)->text().contains("n=1"));
        QCOMPARE(list->count(),3); // Reset does not clear history or the reference.
        QCOMPARE(stringToValue(table->item(0,9)->text(),UNIT_VOLTS),0.);
        mode->setCurrentIndex(1); QCoreApplication::processEvents();
        QVERIFY(!a->isEnabled()); QVERIFY(table->item(0,0)->text().contains("1000 samples"));
        const QPointF centre=plot->rect().center();
        QWheelEvent wheel(centre,plot->mapToGlobal(centre.toPoint()),QPoint(),QPoint(0,1200),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(plot,&wheel);
        QVERIFY(!table->item(0,0)->text().contains("1000 samples"));
        mode->setCurrentIndex(0); QVERIFY(table->item(0,0)->text().contains("1000 samples"));
        // Place a cursor from the plot and check that cursor mode is selected.
        QTest::mouseClick(plot,Qt::LeftButton,Qt::ShiftModifier,centre.toPoint());
        QCOMPARE(mode->currentIndex(),2); QVERIFY(a->value()>0);
        a->setValue(2);b->setValue(3); QVERIFY(table->item(0,11)->text().contains("No finite samples"));
    }
    void integratedGuiSmoke() {
        if(!qEnvironmentVariableIsSet("OH_GUI_SCREENSHOT")) QSKIP("Requires a real OpenGL display; run explicitly with OH_GUI_SCREENSHOT");
        QVERIFY(!GlScope::getOpenGLversion().isEmpty());
        GlScope::useOpenGLSLversion();
        ScopeDevice device;DsoSettings settings(&device);settings.alwaysSave=false;
        settings.scope.horizontal.timebase=.0001;
        HantekDsoControl control(&device,device.getModel(),0);control.applySettings(&settings.scope);
        ExporterRegistry registry(device.getModel()->spec(),&settings);
        MainWindow window(&control,&settings,&registry);window.resize(1650,1000);window.show();
        SpectrumGenerator spectrum(&settings.scope,&settings.analysis);
        GraphGenerator graphs(&settings.scope,&settings.view);
        auto frame=std::make_shared<PPresult>(3);frame->tag=71;frame->capturedAtMs=QDateTime::currentMSecsSinceEpoch();
        frame->softwareTriggerTriggered=true;frame->triggeredPosition=1000;
        for(unsigned ch=0;ch<2;++ch) {
            auto *data=frame->modifiableData(ch);data->voltage.interval=1e-6;
            for(int i=0;i<20000;++i) data->voltage.samples.push_back((ch?.7:1.)*std::sin(2*M_PI*i/1000+ch*.4));
        }
        static_cast<Processor&>(spectrum).process(frame.get());static_cast<Processor&>(graphs).process(frame.get());
        window.showNewData(frame);
        auto dock=window.findChild<Lab::CaptureDock*>();QVERIFY(dock);dock->show();
        for(auto button:dock->findChildren<QPushButton*>()) if(button->text()=="Set reference") button->click();
        frame=std::make_shared<PPresult>(*frame);frame->tag=72;frame->capturedAtMs+=100;
        for(auto &sample:frame->modifiableData(0)->voltage.samples) sample+=.08;
        static_cast<Processor&>(spectrum).process(frame.get());static_cast<Processor&>(graphs).process(frame.get());
        window.showNewData(frame);
        dock->findChild<QDoubleSpinBox*>("labCursorA")->setValue(.004);
        dock->findChild<QDoubleSpinBox*>("labCursorB")->setValue(.012);
        dock->findChild<QComboBox*>("labMeasurementSpan")->setCurrentIndex(2);
        frame=std::make_shared<PPresult>(*frame); frame->tag=73; frame->capturedAtMs+=100;
        window.showNewData(frame);
        QVERIFY(dock->findChild<QTableWidget*>("labMeasurements")->item(0,0)->text().contains("8001 samples"));
        window.resize(1650,1000);
        QTest::qWait(1500);
        // A tiling window manager may override resize(); the application's minimum
        // layout must still fit, independently of the window manager's allocation.
        QVERIFY(window.minimumSizeHint().width()<=1650); QVERIFY(window.minimumSizeHint().height()<=1000);
        QVERIFY(window.grab().save(qEnvironmentVariable("OH_GUI_SCREENSHOT")));
        dock->findChild<QCheckBox*>("labCollectLog")->setChecked(true);
        frame=std::make_shared<PPresult>(*frame); frame->tag=74; frame->capturedAtMs+=100; window.showNewData(frame);
        QTimer::singleShot(0,[] {
            auto dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(dialog); dialog->button(QMessageBox::Cancel)->click();
        });
        QVERIFY(!window.close()); QVERIFY(window.isVisible());
        QTimer::singleShot(0,[] {
            auto dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(dialog); dialog->button(QMessageBox::Discard)->click();
        });
        window.close();
    }
};
QTEST_MAIN(RegressionTests)
#include "regression.moc"
