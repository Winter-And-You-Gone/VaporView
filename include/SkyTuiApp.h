#ifndef VaporView_SKY_TUI_APP_H_
#define VaporView_SKY_TUI_APP_H_

#include "SkyRuntime.h"
#include "SkyTuiController.h"
#include "SkyTuiModel.h"
#include "SkyTuiTheme.h"

#include <QObject>
#include <QTimer>
#include <atomic>
#include <memory>
#include <thread>

namespace VaporView
{

class SkyTuiScreenBuffer;

enum class SkyTuiKeyType
{
    Character,
    Enter,
    Backspace,
    Escape,
    Tab,
    Up,
    Down,
    Left,
    Right,
    PageUp,
    PageDown,
    CtrlC,
    CtrlL,
    CtrlP,
    Unknown,
};

struct SkyTuiKey
{
    SkyTuiKeyType type = SkyTuiKeyType::Unknown;
    QChar character;
};

class SkyTuiApp : public QObject
{
    Q_OBJECT

public:
    SkyTuiApp(SkyRuntime *runtime, const SkyRuntimeOptions& options, QObject *parent = nullptr);
    ~SkyTuiApp() override;

    void start();

public slots:
    void appendLog(const QString& message);

private slots:
    void render();
    void refreshStatus();
    void handleKey(const SkyTuiKey& key);

private:
    void startInputThread();
    void restoreTerminal();
    void scheduleRender();
    void forceFullRedraw();
    void executeInput();
    void executeCommand(const QString& command);
    bool handlePageCommand(const QString& normalized);
    void requestQuit();
    void clearLogs();
    void selectPreviousHistory();
    void selectNextHistory();
    void setPaletteVisible(bool visible);
    QList<SkyTuiCommandItem> filteredPalette() const;
    void clampPaletteSelection();
    QString statusSignature(const TelemetryStatus& status, const SkyConfig& config) const;
    QString timestamp() const;
    QString makeLogLine(const QString& message) const;
    int displayWidth(const QString& text) const;
    QString fitPlain(const QString& text, int width) const;
    QString padPlain(const QString& text, int width) const;
    QStringList wrapPlain(const QString& text, int width) const;
    void drawText(SkyTuiScreenBuffer& output, int row, int column, const QString& text) const;
    void drawBox(SkyTuiScreenBuffer& output, int top, int left, int bottom, int right, const QString& title, bool focused = false) const;
    void drawLogo(SkyTuiScreenBuffer& output, int& row, const SkyTuiTerminalSize& size) const;
    void drawMainPanels(SkyTuiScreenBuffer& output, int top, int bottom, const SkyTuiTerminalSize& size) const;
    void drawDeviceOverview(SkyTuiScreenBuffer& output, int top, int bottom, const SkyTuiTerminalSize& size) const;
    void drawPalette(SkyTuiScreenBuffer& output, int top, int bottom, const SkyTuiTerminalSize& size);
    void drawInput(SkyTuiScreenBuffer& output, int row, const SkyTuiTerminalSize& size) const;
    void drawStatusBar(SkyTuiScreenBuffer& output, int row, const SkyTuiTerminalSize& size) const;
    QStringList statusPanelLines() const;
    QStringList dashboardSummaryLines(int width) const;
    QStringList renderTerminalWaveform(const QVector<float>& samples, int width, int height) const;
    void drawLinesInBox(SkyTuiScreenBuffer& output, int top, int left, int bottom, int right, const QString& title, const QStringList& lines) const;
    QString deviceEndpointText(SkyDeviceId id) const;
    QString freshnessText(bool stale, bool valid) const;
    QString deviceStateColored(DeviceState state) const;
    QString recordingStateText(quint8 state) const;
    QString humanBytes(quint64 bytes) const;

    SkyRuntime *runtime_ = nullptr;
    SkyRuntimeOptions options_;
    SkyTuiController controller_;
    SkyTuiModel model_;
    QTimer render_timer_;
    QTimer status_timer_;
    std::shared_ptr<std::atomic_bool> input_running_;
    std::thread input_thread_;
    bool started_ = false;
    bool terminal_restored_ = false;
    bool render_pending_ = false;
    bool force_full_redraw_ = true;
    SkyTuiTerminalSize previous_size_;
    std::unique_ptr<SkyTuiScreenBuffer> previous_buffer_;
    bool status_signature_initialized_ = false;
    QString last_status_signature_;
};

}  // namespace VaporView

Q_DECLARE_METATYPE(VaporView::SkyTuiKey)

#endif
