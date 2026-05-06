#ifndef VaporView_SKY_TUI_MODEL_H_
#define VaporView_SKY_TUI_MODEL_H_

#include "SkyConfig.h"
#include "TelemetryTypes.h"

#include <QString>
#include <QStringList>

namespace VaporView
{

enum class SkyTuiFocus
{
    CommandInput,
    Logs,
    Status,
};

struct SkyTuiModel
{
    static constexpr int MaxLogLines = 600;

    QString input_text;
    QString draft_input;
    QStringList command_history;
    int history_index = -1;
    bool palette_visible = false;
    int palette_selected = 0;
    QStringList logs;
    int log_scroll = 0;
    int selected_log_index = -1;
    int selected_status_index = 0;
    SkyTuiFocus focus = SkyTuiFocus::CommandInput;
    bool show_logo = true;
    TelemetryStatus status;
    SkyConfig config = SkyConfig::defaults();
    QString last_command;
    QString hint = QStringLiteral("输入 /help 查看可用命令");
    bool quitting = false;
};

}  // namespace VaporView

#endif
