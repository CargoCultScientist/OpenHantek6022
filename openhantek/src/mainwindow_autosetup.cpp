// SPDX-License-Identifier: GPL-2.0-or-later
#include "mainwindow.h"
#include "src/ui_mainwindow.h"
#include "dsosettings.h"
#include "dsowidget.h"
#include "docks/VoltageDock.h"
#include "docks/HorizontalDock.h"
#include "docks/TriggerDock.h"
#include "docks/SpectrumDock.h"
#include "hantekdso/hantekdsocontrol.h"
#include <QAction>
#include <QDateTime>
#include <QLabel>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

void MainWindow::initializeSetupAssistant(HantekDsoControl *control, VoltageDock *voltage,
                                         HorizontalDock *horizontal, TriggerDock *trigger) {
    setupControl = control;
    setupVoltageDock = voltage;
    setupHorizontalDock = horizontal;
    setupTriggerDock = trigger;
    signalHint = new QLabel(this);
    signalHint->setObjectName("labSignalHint");
    signalHint->setTextFormat(Qt::PlainText);
    signalHint->setWordWrap(true);
    signalHint->setStyleSheet("color: #ecd09d; padding: 2px 4px;");
    signalHint->setToolTip(tr("Live physical inputs only, not the selected historical capture. Hints never change settings. Sampling can conceal aliasing."));
    qobject_cast<QVBoxLayout *>(centralWidget()->layout())->insertWidget(1, signalHint);
    autoSetupAction = new QAction(tr("Auto setup"), this);
    autoSetupAction->setObjectName("labAutoSetup");
    autoSetupAction->setToolTip(tr("Fit enabled voltage inputs using fresh live samples. Preserves probes, coupling and calibration. Clipping widens one range per click; uncertain timing is left unchanged."));
    undoSetupAction = new QAction(tr("Undo Auto"), this);
    undoSetupAction->setObjectName("labUndoAutoSetup");
    undoSetupAction->setToolTip(tr("Restore the setup before the last Auto. A manual setup change discards this one-step undo."));
    ui->toolBar->insertAction(ui->actionRefresh, autoSetupAction);
    ui->toolBar->insertAction(ui->actionRefresh, undoSetupAction);
    setupTimeout = new QTimer(this);
    setupTimeout->setObjectName("labAutoSetupTimeout");
    setupTimeout->setSingleShot(true);
    setupTimeout->setInterval(2500);
    connect(setupTimeout, &QTimer::timeout, this, [this] {
        autoWaiting = false;
        // Advance the request so a late result cannot apply a timed-out plan.
        invalidateSetupAssistant(false);
        signalHint->setText(tr("Auto timed out without two fresh captures. Try AUTO trigger mode, a faster timebase, or check the connection. No setup was changed."));
    });
    const auto invalidate = [this] { invalidateSetupAssistant(); };
    // Connect after the ordinary dock -> controller connections so the capture
    // fence is queued after the corresponding settings commands.
    connect(voltage, &VoltageDock::gainChanged, this, invalidate);
    connect(voltage, &VoltageDock::probeAttnChanged, this, invalidate);
    connect(voltage, &VoltageDock::couplingChanged, this, invalidate);
    connect(voltage, &VoltageDock::invertedChanged, this, invalidate);
    connect(voltage, &VoltageDock::usedChannelChanged, this, invalidate);
    connect(voltage, &VoltageDock::modeChanged, this, invalidate);
    connect(horizontal, &HorizontalDock::timebaseChanged, this, invalidate);
    connect(horizontal, &HorizontalDock::samplerateChanged, this, invalidate);
    connect(horizontal, &HorizontalDock::recordLengthChanged, this, invalidate);
    connect(horizontal, &HorizontalDock::formatChanged, this, invalidate);
    connect(horizontal, &HorizontalDock::calfreqChanged, this, invalidate);
    connect(trigger, &TriggerDock::modeChanged, this, invalidate);
    connect(trigger, &TriggerDock::sourceChanged, this, invalidate);
    connect(trigger, &TriggerDock::slopeChanged, this, invalidate);
    connect(trigger, &TriggerDock::smoothChanged, this, invalidate);
    connect(dsoWidget, &DsoWidget::voltageOffsetChanged, this, invalidate);
    connect(dsoWidget, &DsoWidget::triggerLevelChanged, this, invalidate);
    connect(dsoWidget, &DsoWidget::triggerPositionChanged, this, invalidate);
    connect(findChild<SpectrumDock *>(), &SpectrumDock::usedChannelChanged, this, invalidate);
    connect(this, &MainWindow::settingsLoaded, this, invalidate);
    connect(control, &HantekDsoControl::showSamplingStatus, this, invalidate);
    connect(ui->actionSampling, &QAction::triggered, this, invalidate);
    connect(ui->actionCalibrateOffset, &QAction::toggled, this, invalidate);
    connect(ui->actionManualCommand, &QAction::toggled, this, invalidate);
    connect(autoSetupAction, &QAction::triggered, this, [this] {
        if (!autoSetupAction->isEnabled()) return;
        invalidateSetupAssistant(false);
        setupBefore = Lab::SetupValues::fromScope(dsoSettings->scope, setupControl->getModel()->spec()->channels);
        autoWaiting = true;
        setupTimeout->start();
        signalHint->setText(tr("Auto: waiting for two fresh live captures… Manual changes or Stop cancel this request."));
        updateSetupAvailability();
    });
    connect(undoSetupAction, &QAction::triggered, this, [this] {
        if (!setupUndoAvailable || !deviceAvailable || setupUndoExpected != Lab::setupFingerprint(dsoSettings->scope)) {
            invalidateSetupAssistant();
            return;
        }
        setupUndoAvailable = false;
        applySetupValues(setupUndo, true);
    });
    connect(control, &HantekDsoControl::setupValuesApplied, this, [this](quint64 request, bool success) {
        if (request != setupRequest) return;
        autoApplying = false;
        if (!success)
            signalHint->setText(tr("Some setup commands failed. Check the device connection; the displayed setup may not match the hardware."));
        statusBar()->showMessage(success ? tr("Setup commands applied. Inspect the next live captures.") : tr("Setup command failed."), 6000);
        updateSetupAvailability();
    });
    hintClock.start();
    hintFreshness.start();
    auto freshnessTimer = new QTimer(this);
    freshnessTimer->setInterval(1000);
    connect(freshnessTimer, &QTimer::timeout, this, [this] {
        if (autoSetupAction->isEnabled() && hintFreshness.elapsed() > 2000)
            signalHint->setText(tr("No recent live capture. Hints paused; check trigger mode, timebase and connection."));
    });
    freshnessTimer->start();
    invalidateSetupAssistant();
}

void MainWindow::updateSetupAvailability() {
    if (!autoSetupAction) return;
    const auto &scope = dsoSettings->scope;
    const bool supported = scope.horizontal.format == Dso::GraphFormat::TY &&
        (scope.trigger.mode == Dso::TriggerMode::AUTO || scope.trigger.mode == Dso::TriggerMode::NORMAL) &&
        !scope.liveCalibrationActive && !ui->actionManualCommand->isChecked();
    autoSetupAction->setEnabled(deviceAvailable && samplingActive && supported && !autoWaiting && !autoApplying);
    undoSetupAction->setEnabled(deviceAvailable && setupUndoAvailable && !autoWaiting && !autoApplying &&
                                setupUndoExpected == Lab::setupFingerprint(scope));
    if (!deviceAvailable || !samplingActive || !supported) {
        if (setupTimeout) setupTimeout->stop();
        autoWaiting = false;
        signalHint->setText(!deviceAvailable ? tr("Signal hints paused: device disconnected.")
            : !samplingActive ? tr("Signal hints paused: acquisition stopped. Run to inspect fresh samples.")
            : tr("Auto and live hints require time-domain AUTO/NORMAL acquisition, with calibration and manual commands off."));
    }
}

void MainWindow::invalidateSetupAssistant(bool forgetUndo) {
    if (!setupControl) return;
    const bool cancelled = autoWaiting;
    autoWaiting = false;
    autoApplying = false;
    setupTimeout->stop();
    if (forgetUndo) setupUndoAvailable = false;
    setupExpected = Lab::setupFingerprint(dsoSettings->scope);
    setupFrameCount = 0;
    hintRepeats = 0;
    hintFreshness.restart();
    hintCandidate.clear();
    const auto request = ++setupRequest;
    QMetaObject::invokeMethod(setupControl, [control = setupControl, request] {
        control->requestSetupSnapshot(request);
    }, Qt::QueuedConnection);
    signalHint->setText(cancelled ? tr("Auto cancelled by a setup/acquisition change. Waiting for fresh live samples.")
                                 : tr("Signal hints: waiting for fresh live samples…"));
    updateSetupAvailability();
}

void MainWindow::inspectSetupFrame(const PPresult &frame) {
    if (!setupControl || !deviceAvailable || !samplingActive || autoApplying) return;
    if (setupExpected != Lab::setupFingerprint(dsoSettings->scope)) {
        invalidateSetupAssistant();
        return;
    }
    if (!autoWaiting && !autoSetupAction->isEnabled()) return;
    const auto age = QDateTime::currentMSecsSinceEpoch() - frame.capturedAtMs;
    if (frame.setupRequest != setupRequest || age < 0 || age > 2000 ||
        (setupFrameCount && setupLastTag == frame.tag && setupLastTime == frame.capturedAtMs)) return;
    setupLastTag = frame.tag;
    setupLastTime = frame.capturedAtMs;
    hintFreshness.restart();
    // Discard the first post-fence block for range/USB settling; held repaints
    // do not count. No wall-clock timestamp is used as the acquisition fence.
    if (++setupFrameCount < 2) return;
    if (!autoWaiting && hintClock.elapsed() < 250) return;
    hintClock.restart();
    const auto plan = Lab::planAutoSetup(frame, dsoSettings->scope, setupControl->getModel()->spec()->channels);
    const auto text = plan.hints.join(QStringLiteral("  ·  "));
    if (autoWaiting) {
        autoWaiting = false;
        setupTimeout->stop();
        if (plan.changed) {
            setupUndo = setupBefore;
            setupUndoAvailable = true;
            applySetupValues(plan.values, false);
            signalHint->setText(tr("Auto requested; check the new live trace. ") + text);
        } else {
            signalHint->setText(tr("Auto made no changes. ") + text);
            updateSetupAvailability();
        }
    } else {
        if (text == hintCandidate) ++hintRepeats;
        else { hintCandidate = text; hintRepeats = 1; }
        if (hintRepeats >= 3) signalHint->setText(tr("Live signal: ") + text);
    }
}

void MainWindow::applySetupValues(const Lab::SetupValues &values, bool restoreRate) {
    const bool changeTimebase = values.timebase != dsoSettings->scope.horizontal.timebase;
    values.writeTo(dsoSettings->scope);
    {
        // Updating widgets must not dispatch a second set of hardware commands
        // or invalidate the undo entry. One value-captured batch goes below.
        QSignalBlocker voltageBlock(setupVoltageDock), horizontalBlock(setupHorizontalDock),
            triggerBlock(setupTriggerDock), scopeBlock(dsoWidget);
        for (unsigned ch = 0; ch < values.channels.size(); ++ch) {
            setupVoltageDock->setGain(ch, values.channels[ch].gain);
            dsoWidget->updateVoltageGain(ch, values.channels[ch].voltsPerDiv);
        }
        setupHorizontalDock->setTimebase(values.timebase);
        setupTriggerDock->timebaseChanged(values.timebase);
        setupTriggerDock->loadSettings(&dsoSettings->scope);
        dsoWidget->updateTimebase(values.timebase);
        dsoWidget->updateTriggerMode(values.mode);
        dsoWidget->updateTriggerSource(values.source);
        dsoWidget->updateTriggerSlope(values.slope);
        dsoWidget->updateSlidersSettings();
        // Sliders may quantize their visual position; retain the requested values.
        values.writeTo(dsoSettings->scope);
    }
    const auto request = setupRequest + 1;
    QMetaObject::invokeMethod(setupControl, [control = setupControl, values, changeTimebase, restoreRate, request] {
        bool ok = control->isDeviceAvailable();
        const auto check = [&ok](Dso::ErrorCode code) { ok = (code == Dso::ErrorCode::NONE) && ok; };
        if (ok) {
            for (unsigned ch = 0; ch < values.channels.size(); ++ch) {
                check(control->setGain(ch, values.channels[ch].voltsPerDiv));
                check(control->setTriggerLevel(ch, values.channels[ch].trigger));
            }
            if (changeTimebase) check(control->setRecordTime(values.timebase * DIVS_TIME));
            if (restoreRate) check(control->setSamplerate(values.samplerate));
            check(control->setTriggerMode(values.mode));
            check(control->setTriggerSource(values.source));
            check(control->setTriggerSlope(values.slope));
            check(control->setTriggerPosition(values.position));
            control->requestDisplayRefresh();
        }
        emit control->setupValuesApplied(request, ok);
    }, Qt::QueuedConnection);
    invalidateSetupAssistant(false); // Queued after the value-captured batch.
    setupUndoExpected = setupExpected;
    autoApplying = true;
    signalHint->setText(restoreRate ? tr("Restoring the setup from before Auto…") : tr("Applying Auto setup…"));
    updateAcquisitionStatus();
}
