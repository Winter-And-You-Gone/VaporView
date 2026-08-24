#ifndef VaporView_MAIN_WINDOW_H_
#define VaporView_MAIN_WINDOW_H_

#include "LogRecord.h"
#include "Ai8TemperatureControllerProtocol.h"
#include "SkyConfig.h"
#include "data_collector.h"
#include "data_types.h"
#include "TelemetryTypes.h"
#include "TcpWaveEncoding.h"
#include <QByteArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QTextEdit>
#include <QGroupBox>
#include <QFrame>
#include <QComboBox>
#include <QDateTime>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QAction>
#include <QActionGroup>
#include <QScrollArea>
#include <QLineEdit>
#include <QList>
#include <QVector>
#include <QHash>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class RtkConfigDialog;
class QFile;
class QEvent;
class QCloseEvent;
class QResizeEvent;
class QSplitter;
class QToolButton;
class QButtonGroup;
class QRadioButton;
class QStackedWidget;
class QCheckBox;
class QListView;
class QProgressBar;
class QPlainTextEdit;
class TcpWavePanel;
class SessionViewerWindow;
class GnssPanel;
class ImuPanel;
class PtbPanel;
class HmpPanel;
class LidarPanel;
class TemperatureControllerPanel;
class TemperatureTrendPlotWidget;
namespace VaporView { class SingleLevelPopupMenu; }
namespace VaporView::Ground::Widgets { class EpsilonPanel; }
namespace VaporView::Ground::Widgets { class Ai8TemperatureControllerPanel; }
namespace VaporView::Ground::Widgets { class DevicePanelCoordinator; }
namespace VaporView::Ground::Widgets { class SourceModeOverviewSwitchButton; }
namespace VaporView::Ground::Widgets { class TemperatureControllerOverviewPanel; }
namespace VaporView::Ground::Navigation { class CombinationNavigationPage; }
namespace VaporView::Ground::Navigation { class EpsilonConfigPanel; }
#ifdef VAPORVIEW_HAS_OSGEARTH
namespace VaporView::Ground { class Map3DController; }
#endif
namespace VaporView::Ground::Devices { struct CollectorSet; }
namespace VaporView::Ground::Devices { class LocalDeviceConnectionController; }
namespace VaporView::Ground::Devices { class LocalConnectionCoordinator; }
namespace VaporView::Ground::Devices { class RemoteSkyController; }
namespace VaporView::Ground::Devices { class Ai8DeviceSession; }
namespace VaporView::Ground::Devices { enum class Ai8Operation; struct Ai8SessionResult; }
namespace VaporView::Ground::Devices { class EpsilonDeviceSession; }
namespace VaporView::Ground::Devices { enum class EpsilonOperation; struct EpsilonSessionResult; }
namespace VaporView::Ground::Devices { class Rd105DeviceSession; }
namespace VaporView::Ground::Devices { struct Rd105SessionResult; }
namespace VaporView::Ground::Devices { class UiTestDataModel; enum class UiTestScenario; }
namespace VaporView::Ground::Session { class GroundRecordingService; }
namespace VaporView::Ground::Session { class RecordingScheduleController; }
namespace VaporView::Ground::Main
{
enum class AppSidebarMode;
enum class LogUiViewMode;
struct MainWindowState;
struct RemoteTelemetrySummarySections;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

#ifdef VAPORVIEW_MAIN_WINDOW_TESTING
    void testSetLocalTemperatureCommandObserver(std::function<void(VaporView::CommandId)> observer);
    void testInjectRemoteSkyConfig(const QJsonObject& object);
    void testInjectRemoteSkyApplyResult(const QJsonObject& result);
    QJsonObject testRemoteSkyConfigFromDeviceConfigUi(QString *errorMessage = nullptr) const;
    QString testRemoteSkyConfigStatusText() const;
#endif

#if defined(VAPORVIEW_HAS_OSGEARTH) && defined(VAPORVIEW_MAIN_WINDOW_TESTING)
    int testPendingMap3DSampleCount() const;
    qint64 testLatestPendingMap3DRecordTimestampUs() const;
    bool testMap3DFlushTimerActive() const;
    QString testLastMap3DDropReason() const;
    void testMaybeForwardMap3DSampleForMap3D(const VaporView::EpsilonData& epsilonData,
                                             quint64 recordTimestampUs);
    void testOnRemoteBasicTelemetryUpdatedForMap3D(const VaporView::TelemetryBasic& telemetry);
#endif

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void *message, qintptr *result) override;
#endif
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onEpsilonDataReady();
    void onGnssDataReady();
    void onImuDataReady();
    void onPtbDataReady();
    void onHmpDataReady();
    void onLidarDataReady();
    void onTemperatureControllerDataReady();
    void onAi8TemperatureControllerDataReady();
    void onAi8ReadPageRequested();
    void onAi8WritePageRequested();
    void onRefreshTimer();
    void onClearLogClicked();
    void onRefreshPortsClicked();
    void onAutoDetectPortsClicked();
    void onChooseRecordingDirectoryClicked();
    void onSwitchLanguage();
    void onReconfigureEpsilonClicked();
    void onConfigureEpsilonRtcmPortClicked();
    void onConfigureEpsilonPacketRatesClicked();
    void onGlobalRateChanged(const QString& text);
    void onGnssRateChanged(const QString& text);
    void onImuRateChanged(const QString& text);
    void onPtbRateChanged(const QString& text);
    void onHmpRateChanged(const QString& text);
    void onLidarRateChanged(const QString& text);
    void onTemperatureRateChanged(const QString& text);
    void onRtkConfigClicked();
    void onOpenSessionViewerClicked();
    void onOpenMap3DWindowClicked();
    void onOpenMap3DDiagnosticsClicked();
    void onCheckUpdatesClicked();
    void onFontScaleTriggered(QAction *action);
    void onCancelConnectClicked();
    void onToggleTheme();
    void onTcpRawWaveFrameReady(quint64 timestampUs, QByteArray rawSignalPayload, QByteArray harmonicPayload, VaporView::TcpFloatEncoding floatEncoding);
    void onScheduledRecordingClicked();
    void onScheduledRecordingTick();
    void onStartRecordingClicked();
    void onPauseRecordingClicked();
    void onStopRecordingClicked();
    void onDataSourceModeChanged(int index);
    void onRemoteBasicTelemetryUpdated(const VaporView::TelemetryBasic& telemetry);
    void onRemoteWaveformUpdated(const VaporView::DownsampledWaveform& waveform);
    void onRemoteWaveformFeatureUpdated(const VaporView::WaveformFeature& feature);
    void onRemoteTelemetryStatusUpdated(const VaporView::TelemetryStatus& status);
    void onRemoteTemperatureControllerStatusUpdated(const VaporView::TemperatureControllerData& controllerData);
    void onRemoteAi8TemperatureControllerStatusUpdated(
        const VaporView::Ai8TemperatureControllerProtocol::LiveData& data);
    void onAi8SessionAvailabilityChanged(bool available, const QString& reason);
    void onAi8SessionOperationStarted(
        quint64 requestId, VaporView::Ground::Devices::Ai8Operation operation);
    void onAi8SessionOperationFinished(
        const VaporView::Ground::Devices::Ai8SessionResult& result);
    void onAi8SessionPageDataAvailable(
        const VaporView::Ai8TemperatureControllerProtocol::PageData& data);
    void onEpsilonSessionAvailabilityChanged(bool available, const QString& reason);
    void onEpsilonSessionOperationStarted(
        quint64 requestId, VaporView::Ground::Devices::EpsilonOperation operation);
    void onEpsilonSessionOperationFinished(
        const VaporView::Ground::Devices::EpsilonSessionResult& result);
    void onRd105SessionAvailabilityChanged(bool available, const QString& reason);
    void onRd105SessionOperationStarted(
        quint64 requestId,
        VaporView::CommandId command,
        VaporView::TemperatureControllerCommand payload);
    void onRd105SessionOperationFinished(
        const VaporView::Ground::Devices::Rd105SessionResult& result);
    void onRemoteCommandAckReceived(const VaporView::CommandAck& ack);
    void onRemoteSerialPortDetectionResult(const QJsonObject& result);
    void onRemoteLinkOpenChanged(bool open);
    void onRemoteSkyConfigReadClicked();
    void onRemoteSkyConfigApplyClicked();
    void onRemoteSkyConfigSaveClicked();
    void onUiTestModeTriggered(bool enabled);
    void onUiTestScenarioTriggered(QAction *action);

private:
    using AppSidebarMode = VaporView::Ground::Main::AppSidebarMode;
    using RemoteTelemetrySummarySections = VaporView::Ground::Main::RemoteTelemetrySummarySections;

    void setupMenuBar();
    void setupToolBar();
    void setupCustomTitleBar();
    void setupCentralWidget();
    void setupConfigPanel();
    void setupDeviceConfigPage();
    void setupDataPanels();
    void setupLogPanel();
    void configureComboPopup(QComboBox *combo) const;
    void configureComboPopupsIn(QWidget *scope) const;
    void installLocalSerialPortComboBehavior(QComboBox *combo);
    void refreshLocalSerialPortComboOptions(QComboBox *combo, const QStringList& ports, const QString& preferredText = QString());
    void setLocalSerialPortComboText(QComboBox *combo, const QString& text);
    QString localSerialPortComboValue(const QComboBox *combo) const;
    QString localSerialPortItemValue(const QComboBox *combo, int index) const;
    QString normalizedLocalSerialPortText(const QString& text) const;
    void installRemoteSkySerialPortComboBehavior(QComboBox *combo);
    void refreshRemoteSkySerialPortComboOptions(QComboBox *combo, const QString& preferredText = QString());
    void setRemoteSkySerialPortComboText(QComboBox *combo, const QString& text);
    QString remoteSkySerialPortComboValue(const QComboBox *combo) const;
    QString remoteSkySerialPortItemValue(const QComboBox *combo, int index) const;
    void beginManualLocalSerialPortEntry(QComboBox *combo);
    void finishManualLocalSerialPortEntry(QComboBox *combo, bool accept);
    bool handleLocalSerialPortManualEntryEvent(QObject *watched, QEvent *event);
    void beginManualRemoteSkySerialPortEntry(QComboBox *combo);
    void finishManualRemoteSkySerialPortEntry(QComboBox *combo, bool accept);
    bool handleRemoteSkySerialPortManualEntryEvent(QObject *watched, QEvent *event);
    bool isLocalSerialPortManualOption(const QComboBox *combo, int index) const;
    bool isLocalSerialPortManualOptionText(const QString& text) const;
    bool isRemoteSkySerialPortManualOption(const QComboBox *combo, int index) const;
    QString manualLocalSerialPortOptionText() const;
    QString remoteSkySerialPortManualOptionText() const;
    void refreshLocalSerialPortManualOptionTexts();
    void refreshRemoteSkySerialPortManualOptionTexts();
    void loadModernStyleSheet();
    void publishGroundLog(VaporView::LogLevel level,
                          const QString& category,
                          const QString& event,
                          const QString& message,
                          QVariantMap fields = {});
    void publishTemperatureCommandLog(VaporView::LogLevel level,
                                      const QString& event,
                                      const QString& message,
                                      QVariantMap fields = {});
    void updateRecordingStatusLabel();
    qint64 uiTestRecordingElapsedMs() const;
    void startOrResumeUiTestRecording();
    void pauseUiTestRecording();
    void resetUiTestRecording();
    bool isUiTestMode() const;
    bool canEnterUiTestMode(QString *reason = nullptr) const;
    void setUiTestModeEnabled(bool enabled);
    void setUiTestScenario(VaporView::Ground::Devices::UiTestScenario scenario);
    void applyUiTestSnapshot();
    void updateUiTestModeUi();
    void publishUiTestEvent(const QString& event,
                            const QString& message,
                            QVariantMap fields = {});
    void captureUiTestWidgetState();
    void restoreUiTestWidgetState();
    void enqueueUiLogRecord(const VaporView::LogRecord& record);
    void flushPendingUiLogRecords();
    void startEpsilonReconfigureProgress();
    void updateEpsilonReconfigureProgress();
    void stopEpsilonReconfigureProgress();
    void setLogViewMode(VaporView::Ground::Main::LogUiViewMode mode, bool persist = true);
    void scrollLogViewToBottom();
    bool isLogViewNearBottom() const;
    void updateLogFollowState();
    void updateLogUnreadUi();
    void clearLogUnreadState();
    void updateLogFilterAction();
    void renderLogView();
    bool shouldShowLogRecord(const VaporView::LogRecord& record) const;
    void rebuildRecordingRateMenu();
    void setRecordingExportRateHz(int rate, bool should_log = true);
    void applyEpsilonMainAntennaLeverArm(
        double x_m,
        double y_m,
        double z_m,
        std::function<void(bool, const QString&)> completion);
    void setImuRecordingRateHz(int rate, bool should_log = true);
    void setWaveformRecordingRateHz(int rate, bool should_log = true);
    QString defaultRecordingDirectory() const;
    QString configuredRecordingDirectory() const;
    QString scheduledRecordingStartBlockReason() const;
    bool scheduledRecordingSessionOpen() const;
    bool tryStartScheduledRecording(QString *failureReason = nullptr);
    bool tryStopScheduledRecording();
    void updateScheduledRecordingAction();
    bool startRecordingSession();
    void pauseRecordingSession(bool announce = true);
    void stopRecording(bool announce = true);
    void updateConnectionStatus(bool connected);
    bool homeDeviceConnected(VaporView::SkyDeviceId device) const;
    bool localDeviceEnabled(VaporView::SkyDeviceId device) const;
    void loadLocalDeviceEnabledState();
    void saveLocalDeviceEnabledState() const;
    bool homeDevicePortSelected(VaporView::SkyDeviceId device) const;
    VaporView::DeviceState homeDeviceActionState(VaporView::SkyDeviceId device) const;
    void triggerHomeDeviceAction(VaporView::SkyDeviceId device);
    void startHomeDeviceActionSpinner(VaporView::SkyDeviceId device);
    bool homeDeviceActionSpinnerActive(VaporView::SkyDeviceId device, qint64 nowMs) const;
    int homeDeviceActionSpinnerDegrees(VaporView::SkyDeviceId device, qint64 nowMs) const;
    void updateHomeDeviceStatusCapsules();
    void updateHomeDeviceActionSpinnerIcons();
    bool anyCollectorRunning() const;
    bool anyLocalDeviceConnected() const;
    void configureLocalConnectionCoordinator();
    QStringList getAvailablePorts();
    void setEnglish(bool english);
    void setFontScale(int percent);
    void showAboutDialog();
    void applyStyleConfiguration();
    QString themedStyleSheet() const;
    QString scaledStyleSheet(const QString& styleSheet) const;
    void applyScaledUiMetrics();
    void updateResponsiveHomeLayout();
    void queueResponsiveHomeLayoutRefresh();
    bool shouldUseCompactHomeLayout() const;
    void updateThemeAction();
    void updateThemedIcons();
    void updateRtkConfigIcon();
    void updateFontScaleMenuCheckIcons();
    QString currentMainPageTitleText() const;
    void updateCustomTitleBarTexts();
    void updateCustomTitleBarStyle();
    void updateWindowControlButtons();
    void updateSidebarNavIcons();
    void updateAppSidebarButtonTexts();
    void syncRtkConfigPageState();
    void updateAppSidebarForWidth(int width, bool snapToNearest);
    void finishAppSidebarResize();
    AppSidebarMode appSidebarModeForWidth(int width) const;
    int snappedAppSidebarWidth(int width) const;
    void setAppSidebarWidth(int width);
    int currentAppSidebarWidth() const;
    void saveAppSidebarWidth() const;
    int appSidebarIconOnlyWidth() const;
    int appSidebarDefaultWidth() const;
    void toggleWindowMaximized();
    bool isWindowMaximizedForUi() const;
    void rememberNormalWindowGeometry();
    QRect fallbackNormalWindowGeometry() const;
    QRect currentScreenAvailableGeometry() const;
    void setupWindowBorderFrames();
    void updateWindowBorderFrames();
    void setupWindowResizeHandles();
    void updateWindowResizeHandles();
    QToolButton *createTitleBarActionButton(QAction *action, QWidget *parent);
    QToolButton *createTitleBarIconButton(const QString& objectName, QWidget *parent);
    void addTitleBarSeparator(QHBoxLayout *layout);
    void discardTitleApplicationMenuPanel();
    void createTitleApplicationMenuPanel();
    void showTitleApplicationMenu();
    bool shouldStartWindowMove(QObject *watched) const;
    bool belongsToMainWindow(QWidget *widget) const;
    void syncMainHoverStateFromCursor();
    int scalePixels(int pixels) const;
    int minimumLogSidePanelWidth() const;
    void setLogSidePanelToMinimumWidth();
    void toggleLogSidePanel();
    void setLogSidePanelCollapsed(bool collapsed);
    void updateLogSidePanelToggleButton();
    bool isAppSidebarCollapsed() const;
    void toggleAppSidebarFromLogo();
    void setCustomLogoHovered(bool hovered);
    void updateCustomLogoPixmap();
    void updateCustomLogoTooltip();
    void applyAllSampleRates();
    void loadRememberedInputState();
    void saveRememberedInputState() const;
    void bindRememberedInputState();
    bool applyImuDeviceProfile(const QString& requestedFormat = QString(), int requestedBaud = -1, int requestedRate = -1);
    void setImuFormatSelection(const QString& format);
    void setImuBaudSelection(int baud);
    void setImuRateSelection(int rate);
    void invalidateTemperatureControllerDataUi();
    void stopAllCollectors();
    VaporView::Ground::Devices::CollectorSet snapshotCollectors() const;
    void finishConnectionAttempt(bool connected);
    void updateRecordingActionStates();
    bool isRemoteSkyMode() const;
    bool isRemoteSkyTcpMode() const;
    void updateSourceModeUi();
    void syncDeviceConfigPageForCurrentTarget();
    void enterLocalDeviceConfigMode();
    void enterRemoteSkyDeviceConfigMode();
    void setRemoteSkyConfigUi(const VaporView::SkyConfig& config);
    VaporView::SkyConfig remoteSkyConfigFromDeviceConfigUi(QString *errorMessage = nullptr) const;
    void refreshRemoteSkyConfigRawFromVisual();
    bool applyRemoteSkyConfigRawToVisual(QString *errorMessage = nullptr);
    void onRemoteSkyConfigRawModeToggled(bool checked);
    void markRemoteSkyConfigDirty();
    void setRemoteSkyConfigStatus(const QString& text, bool error = false);
    void updateRemoteSkyConfigControlsState();
    void requestRemoteSkyConfigIfAvailable(bool force = false);
    void startRemoteSerialPortDetection();
    void onRemoteSkyConfigReceived(const QJsonObject& object);
    void onRemoteSkyConfigApplyResultReceived(const QJsonObject& result);
    void handleRemoteSkyConfigReceived(const QJsonObject& object, bool bypassGenerationGuard);
    void handleRemoteSkyConfigApplyResultReceived(const QJsonObject& result, bool bypassGenerationGuard);
    int scaledConfiguredHeight(QWidget *widget, int baseHeight) const;
    int homeDeviceOverviewContentMinimumWidth() const;
    void updateHomeDeviceOverviewMinimumWidth();
    void updateConfigCardHeightForSourceMode();
    void syncDeviceConfigPageFromHome();
    void updateDeviceConfigTexts();
    void updateDeviceConfigState();
    void clearRemoteSkyDataUi();
    void markRemoteSkyLinkClosed();
    void refreshRemoteSkyDataUi();
    bool remoteDeviceDataValid(VaporView::SkyDeviceId device, qint64 timeout_ms) const;
    QString remoteDeviceInvalidText(VaporView::SkyDeviceId device, qint64 timeout_ms) const;
    double remotePacketRate(VaporView::MsgType type) const;
    double remoteWaveformPacketRate(quint16 channelId) const;

    RemoteTelemetrySummarySections remoteTelemetrySummarySections() const;
    void updateRemoteTelemetrySummaryLabel();
    void updateEnvironmentStatusIcons(bool lidarValid, bool ptbValid, bool hmpValid);
    void syncDeviceConfigEpsilonPanelFromSettings();
    void setDeviceConfigEpsilonPacketRates(const std::map<uint8_t, int>& packetRates);
    bool validateEpsilonPacketBandwidth(const std::map<uint8_t, int>& packetRates,
                                        const QString& baudText,
                                        bool showWarning);
    std::map<uint8_t, int> deviceConfigEpsilonPacketRates() const;
    void saveDeviceConfigEpsilonPacketRates(bool applyAfterSave);
    void sendRemoteDeviceCommand(VaporView::CommandId command, VaporView::SkyDeviceId device);
    void requestRemoteWaveTcpConnection(bool connectRequested, const QString& host = QString(), int port = 0);
    void clearPendingRemoteWaveTcpConnection();
    void sendRemotePeakSearchRange(quint32 startIndex, quint32 endIndex);
    QPushButton *createRemoteDeviceButton(const QString& text, VaporView::CommandId command, VaporView::SkyDeviceId device);
    void setRemoteDeviceButtonsEnabled(bool enabled);
    void updateRemoteDeviceButtonText(VaporView::SkyDeviceId device, VaporView::DeviceState state);
    void updateDeviceConfigRemoteActionButton(VaporView::SkyDeviceId device);
    void updateTemperatureControllerTitleText();
    void refreshAi8TemperatureTitlePortOptions(const QStringList& ports, const QString& preferredText = QString());
    void updateAi8TemperatureTitlePortAppearance();
    void updateAi8TemperatureTitleStatus();
    void updateTemperatureTitleButtonsState();
    void handleTemperatureTitleButton(VaporView::CommandId command);
    void connectLocalTemperatureController();
    void disconnectLocalTemperatureController();
    void reconnectLocalTemperatureController();
#ifdef VAPORVIEW_HAS_OSGEARTH
    void maybeForwardMap3DSample(const VaporView::EpsilonData& epsilonData, quint64 recordTimestampUs);
    void noteMap3DSampleDrop(const QString& source, const QString& reason, quint64 recordTimestampUs = 0);
#endif
    void sendTemperatureCommand(VaporView::CommandId command, const VaporView::TemperatureControllerCommand& payload);
    void restoreTemperatureCommandUi(VaporView::CommandId command, quint8 channel);
    bool isTemperatureCommand(VaporView::CommandId command) const;
    QString temperatureCommandStatusText(VaporView::CommandId command, quint8 channel, bool pending, const QString& detail = QString()) const;

    std::atomic<quint32> local_data_update_pending_mask_{0};
    std::unique_ptr<VaporView::Ground::Main::MainWindowState> state_;
};

#endif
