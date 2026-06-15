#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "SkyTuiTheme.h"
#include "AppTheme.h"

#include <QtGlobal>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace VaporView
{
namespace
{
SkyTuiRgb tuiRgb(AppThemeColor color)
{
    const QColor value = appThemeColor(color, true);
    return {value.red(), value.green(), value.blue()};
}

const AppThemeColor kGradient[] = {
    AppThemeColor::TuiGradient0,
    AppThemeColor::TuiGradient1,
    AppThemeColor::TuiGradient2,
    AppThemeColor::TuiGradient3,
    AppThemeColor::TuiGradient4,
    AppThemeColor::TuiGradient5,
};

SkyTuiRgb interpolate(const SkyTuiRgb& a, const SkyTuiRgb& b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return {
        static_cast<int>(a.r + (b.r - a.r) * t),
        static_cast<int>(a.g + (b.g - a.g) * t),
        static_cast<int>(a.b + (b.b - a.b) * t),
    };
}

SkyTuiRgb gradientAt(double x)
{
    const int count = static_cast<int>(sizeof(kGradient) / sizeof(kGradient[0]));
    if (x <= 0.0)
    {
        return tuiRgb(kGradient[0]);
    }
    if (x >= 1.0)
    {
        return tuiRgb(kGradient[count - 1]);
    }
    const double scaled = x * (count - 1);
    const int index = static_cast<int>(scaled);
    const double local = scaled - index;
    return interpolate(tuiRgb(kGradient[index]), tuiRgb(kGradient[std::min(index + 1, count - 1)]), local);
}

}  // namespace

void SkyTuiTheme::enableVirtualTerminal()
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(output, &mode))
        {
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(output, mode);
        }
    }
#endif
}

SkyTuiTerminalSize SkyTuiTheme::terminalSize()
{
    SkyTuiTerminalSize size;
#ifdef Q_OS_WIN
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output, &info))
    {
        size.columns = info.srWindow.Right - info.srWindow.Left + 1;
        size.rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
    {
        size.columns = ws.ws_col > 0 ? ws.ws_col : size.columns;
        size.rows = ws.ws_row > 0 ? ws.ws_row : size.rows;
    }
#endif
    size.columns = std::max(60, size.columns);
    size.rows = std::max(20, size.rows);
    return size;
}

QString SkyTuiTheme::enterAlternateScreen()
{
    return QStringLiteral("\x1b[?1049h\x1b[?25l");
}

QString SkyTuiTheme::leaveAlternateScreen()
{
    return QStringLiteral("\x1b[?25h\x1b[0m\x1b[?1049l");
}

QString SkyTuiTheme::beginSynchronizedUpdate()
{
    return QString();
}

QString SkyTuiTheme::endSynchronizedUpdate()
{
    return QString();
}

QString SkyTuiTheme::clearScreen()
{
    return background(tuiRgb(AppThemeColor::TuiBackground)) + QStringLiteral("\x1b[2J\x1b[3J\x1b[H");
}

QString SkyTuiTheme::moveTo(int row, int column)
{
    return QStringLiteral("\x1b[%1;%2H").arg(row).arg(column);
}

QString SkyTuiTheme::hideCursor()
{
    return QStringLiteral("\x1b[?25l");
}

QString SkyTuiTheme::showCursor()
{
    return QStringLiteral("\x1b[?25h");
}

QString SkyTuiTheme::reset()
{
    return QStringLiteral("\x1b[0m") + background(tuiRgb(AppThemeColor::TuiBackground));
}

QString SkyTuiTheme::bold()
{
    return QStringLiteral("\x1b[1m");
}

QString SkyTuiTheme::dim()
{
    return QStringLiteral("\x1b[2m");
}

QString SkyTuiTheme::inverse()
{
    return QStringLiteral("\x1b[7m");
}

QString SkyTuiTheme::foreground(const SkyTuiRgb& color)
{
    return QStringLiteral("\x1b[38;2;%1;%2;%3m").arg(color.r).arg(color.g).arg(color.b);
}

QString SkyTuiTheme::background(const SkyTuiRgb& color)
{
    return QStringLiteral("\x1b[48;2;%1;%2;%3m").arg(color.r).arg(color.g).arg(color.b);
}

SkyTuiRgb SkyTuiTheme::accent()
{
    return tuiRgb(AppThemeColor::TuiAccent);
}

SkyTuiRgb SkyTuiTheme::muted()
{
    return tuiRgb(AppThemeColor::TuiMuted);
}

SkyTuiRgb SkyTuiTheme::green()
{
    return tuiRgb(AppThemeColor::TuiGreen);
}

SkyTuiRgb SkyTuiTheme::yellow()
{
    return tuiRgb(AppThemeColor::TuiYellow);
}

SkyTuiRgb SkyTuiTheme::red()
{
    return tuiRgb(AppThemeColor::TuiRed);
}

SkyTuiRgb SkyTuiTheme::blue()
{
    return tuiRgb(AppThemeColor::TuiBlue);
}

QStringList SkyTuiTheme::logoLines()
{
    return {
        QStringLiteral("██╗   ██╗ █████╗ ██████╗  ██████╗ ██████╗ ██╗   ██╗██╗███████╗██╗    ██╗"),
        QStringLiteral("██║   ██║██╔══██╗██╔══██╗██╔═══██╗██╔══██╗██║   ██║██║██╔════╝██║    ██║"),
        QStringLiteral("██║   ██║███████║██████╔╝██║   ██║██████╔╝██║   ██║██║█████╗  ██║ █╗ ██║"),
        QStringLiteral("╚██╗ ██╔╝██╔══██║██╔═══╝ ██║   ██║██╔══██╗╚██╗ ██╔╝██║██╔══╝  ██║███╗██║"),
        QStringLiteral(" ╚████╔╝ ██║  ██║██║     ╚██████╔╝██║  ██║ ╚████╔╝ ██║███████╗╚███╔███╔╝"),
        QStringLiteral("  ╚═══╝  ╚═╝  ╚═╝╚═╝      ╚═════╝ ╚═╝  ╚═╝  ╚═══╝  ╚═╝╚══════╝ ╚══╝╚══╝"),
    };
}

QString SkyTuiTheme::gradientLogoLine(const QString& line)
{
    QString output;
    const int width = std::max(1, static_cast<int>(line.size()) - 1);
    for (int i = 0; i < line.size(); ++i)
    {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char(' '))
        {
            output += ch;
            continue;
        }
        output += foreground(gradientAt(static_cast<double>(i) / width));
        output += ch;
    }
    output += reset();
    return output;
}

}  // namespace VaporView
