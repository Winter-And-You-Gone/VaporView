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

enum class SkyTuiKeyType
{
    Character,
    Enter,
    Backspace,
    Escape,
    Tab,
    Up,
    Down,
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
    void executeInput();
    void executeCommand(const QString& command);
    void requestQuit();
    void clearLogs();
    void setPaletteVisible(bool visible);
    QList<SkyTuiCommandItem> filteredPalette() const;
    void clampPaletteSelection();
    QString timestamp() const;
    QString makeLogLine(const QString& message) const;
    QString fitPlain(const QString& text, int width) const;
    QString padPlain(const QString& text, int width) const;
    void drawText(QString& output, int row, int column, const QString& text) const;
    void drawBox(QString& output, int top, int left, int bottom, int right, const QString& title) const;
    void drawLogo(QString& output, int& row, const SkyTuiTerminalSize& size) const;
    void drawMainPanels(QString& output, int top, int bottom, const SkyTuiTerminalSize& size) const;
    void drawPalette(QString& output, int top, int bottom, const SkyTuiTerminalSize& size);
    void drawInput(QString& output, int row, const SkyTuiTerminalSize& size) const;
    void drawStatusBar(QString& output, int row, const SkyTuiTerminalSize& size) const;
    QStringList statusPanelLines() const;
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
};

}  // namespace VaporView

Q_DECLARE_METATYPE(VaporView::SkyTuiKey)

#endif
