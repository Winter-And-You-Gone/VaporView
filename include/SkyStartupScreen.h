#ifndef VaporView_SKY_STARTUP_SCREEN_H_
#define VaporView_SKY_STARTUP_SCREEN_H_

#include <QString>

namespace VaporView
{

enum class SkyStartupDecision
{
    EnterTui,
    Exit,
};

SkyStartupDecision showSkyStartupScreen(const QString& logo_path = QString());

}  // namespace VaporView

#endif
