#ifndef VaporView_SKY_TUI_THEME_H_
#define VaporView_SKY_TUI_THEME_H_

#include <QString>
#include <QStringList>

namespace VaporView
{

struct SkyTuiRgb
{
    int r = 255;
    int g = 255;
    int b = 255;
};

struct SkyTuiTerminalSize
{
    int columns = 100;
    int rows = 32;
};

class SkyTuiTheme
{
public:
    static void enableVirtualTerminal();
    static SkyTuiTerminalSize terminalSize();

    static QString enterAlternateScreen();
    static QString leaveAlternateScreen();
    static QString beginSynchronizedUpdate();
    static QString endSynchronizedUpdate();
    static QString clearScreen();
    static QString moveTo(int row, int column);
    static QString hideCursor();
    static QString showCursor();

    static QString reset();
    static QString bold();
    static QString dim();
    static QString inverse();
    static QString foreground(const SkyTuiRgb& color);
    static QString background(const SkyTuiRgb& color);

    static SkyTuiRgb accent();
    static SkyTuiRgb muted();
    static SkyTuiRgb green();
    static SkyTuiRgb yellow();
    static SkyTuiRgb red();
    static SkyTuiRgb blue();

    static QStringList logoLines();
    static QString gradientLogoLine(const QString& line);
};

}  // namespace VaporView

#endif
