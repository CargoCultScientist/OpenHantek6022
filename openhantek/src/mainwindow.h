// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include "post/ppresult.h"
#include <QElapsedTimer>
#include <QLineEdit>
#include <QList>
#include <QMainWindow>
#include <memory>

#include "scopesettings.h"
#include "lab/autosetup.h"
#include "usb/devicereconnectionsupervisor.h"

class SpectrumGenerator;
class HantekDsoControl;
class DsoSettings;
class ExporterRegistry;
class DsoWidget;
class HorizontalDock;
class TriggerDock;
class SpectrumDock;
class VoltageDock;
class QAction;
class QLabel;
class QTabWidget;
class QTimer;
namespace Lab { class CaptureDock; }

namespace Ui {
class MainWindow;
}

/// \brief The main window of the application.
/// The main window contains the classic oszilloscope-screen and the gui
/// elements used to control the oszilloscope.
class MainWindow : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow( HantekDsoControl *dsoControl, DsoSettings *dsoSettings, ExporterRegistry *exporterRegistry,
                         QWidget *parent = nullptr );
    ~MainWindow() override;
    QElapsedTimer elapsedTime;

  public slots:
    void showNewData( std::shared_ptr< PPresult > newData );
    void exporterStatusChanged( const QString &exporterName, const QString &status );
    void exporterProgressChanged();
    void deviceConnectionStateChanged( DeviceConnectionState state, const QString &message );

  protected:
    void closeEvent( QCloseEvent *event ) override;

  private:
    Ui::MainWindow *ui;
    QIcon iconPause;
    QIcon iconPlay;
    QLineEdit *commandEdit = nullptr;
    QString lastSaveAsDir = "";

    // Central widgets
    DsoWidget *dsoWidget;
    Lab::CaptureDock *captureDock;
    QTabWidget *workspace;
    QLabel *acquisitionBadge;
    QLabel *workspaceHint;
    bool demoDevice = false;
    bool samplingActive = false;
    bool deviceAvailable = true;
    QByteArray defaultWorkspaceState;
    // Version only the window layout, not the user's scope/calibration settings.
    static constexpr int workspaceLayoutVersion = 1;
    void createWorkspace(HantekDsoControl *control, TriggerDock *triggerDock);
    void updateAcquisitionStatus();
    void restoreWorkspaceLayout();
    void initializeSetupAssistant(HantekDsoControl *, VoltageDock *, HorizontalDock *, TriggerDock *);
    void invalidateSetupAssistant(bool forgetUndo = true);
    void updateSetupAvailability();
    void inspectSetupFrame(const PPresult &frame);
    void applySetupValues(const Lab::SetupValues &values, bool restoreRate);
    HantekDsoControl *setupControl = nullptr;
    VoltageDock *setupVoltageDock = nullptr;
    HorizontalDock *setupHorizontalDock = nullptr;
    TriggerDock *setupTriggerDock = nullptr;
    QAction *autoSetupAction = nullptr, *undoSetupAction = nullptr;
    QLabel *signalHint = nullptr;
    QTimer *setupTimeout = nullptr;
    quint64 setupRequest = 0;
    unsigned setupFrameCount = 0, setupLastTag = 0, hintRepeats = 0;
    qint64 setupLastTime = 0;
    bool autoWaiting = false, autoApplying = false, setupUndoAvailable = false;
    QByteArray setupExpected, setupUndoExpected;
    QString hintCandidate;
    Lab::SetupValues setupBefore, setupUndo;
    QElapsedTimer hintClock, hintFreshness;

    // Settings used for the whole program
    DsoSettings *dsoSettings;
    ExporterRegistry *exporterRegistry;

    // Taking screenshots
    enum screenshotType_t { SCREENSHOT, HARDCOPY, PRINTER };
    screenshotType_t screenshotType;
    void screenShot( screenshotType_t screenshotType = SCREENSHOT, bool autoSave = false );

    bool openDocument( QString docName );
    void setDeviceCommandUiEnabled( bool enabled );

    QList< QWidget * > deviceCommandWidgets;
    QList< QAction * > deviceCommandActions;

  signals:
    void settingsLoaded( DsoSettingsScope *scope, const Dso::ControlSpecification *spec );
    void singleCaptureRequested();
};
