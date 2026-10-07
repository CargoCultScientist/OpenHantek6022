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
#include "post/postprocessing.h"
#include "post/graphgenerator.h"
#include "mainwindow.h"
#include "glscope.h"
#include <QListWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QCheckBox>

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
        QVERIFY(!std::isfinite(m.rise)); QVERIFY(m.undersampled);
        for(auto &v:square) v=2;
        m=Lab::measure(square,dt);
        QCOMPARE(m.rms,2.); QVERIFY(!std::isfinite(m.frequency));
        square[2]=std::numeric_limits<double>::quiet_NaN();
        QCOMPARE(Lab::measure(square,dt).count,size_t(0));
        Lab::RunningStatistic stats; stats.add(1);stats.add(2);stats.add(3);stats.add(Lab::unavailable);
        QCOMPARE(stats.count,size_t(3)); QCOMPARE(stats.mean,2.); QCOMPARE(stats.deviation(),1.);
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
        source.metadata={{"note","test"}};
        QString error; const auto path=config.filePath("record.ohl.json");
        QVERIFY2(source.save(path,error),qPrintable(error));
        auto copy=Lab::Capture::load(path,error); QVERIFY2(copy,qPrintable(error));
        QCOMPARE(copy->tag,42u); QCOMPARE(copy->capturedAtMs,source.capturedAtMs);
        QCOMPARE(copy->channels[0].signal.samples,source.channels[0].signal.samples);
        QCOMPARE(copy->metadata,source.metadata);
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
        QTest::qWait(1500);
        QVERIFY(window.grab().save(qEnvironmentVariable("OH_GUI_SCREENSHOT")));
        window.close();
    }
};
QTEST_MAIN(RegressionTests)
#include "regression.moc"
