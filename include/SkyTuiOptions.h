#ifndef VaporView_SKY_TUI_OPTIONS_H_
#define VaporView_SKY_TUI_OPTIONS_H_

#include <QString>

namespace VaporView
{

struct SkyTuiOptions
{
    QString ipc_host = QStringLiteral("127.0.0.1");
    int ipc_port = 39001;
    bool auto_launch_core = false;
    bool quit_leaves_core = true;
    QString core_path;
};

}  // namespace VaporView

#endif
