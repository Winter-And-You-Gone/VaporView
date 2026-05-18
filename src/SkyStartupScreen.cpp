#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "SkyStartupScreen.h"

#include "SkyTuiTheme.h"

#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#ifdef Q_OS_WIN
#include <windows.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace VaporView
{
namespace
{
#ifdef Q_OS_WIN
DWORD g_startup_original_input_mode = 0;
bool g_startup_has_original_input_mode = false;
#endif
#ifndef Q_OS_WIN
termios g_startup_original_termios;
bool g_startup_has_original_termios = false;
#endif
constexpr double kPi = 3.14159265358979323846;
constexpr double kLogoTerminalAspect = 2.0;
constexpr int kLogoFrameCount = 24;

void writeRaw(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    std::cout.write(bytes.constData(), bytes.size());
    std::cout.flush();
}

bool isAnsiFinalByte(QChar ch)
{
    const ushort value = ch.unicode();
    return value >= 0x40 && value <= 0x7e;
}

enum class AnsiParseState
{
    None,
    Escape,
    Csi,
};

QString stripAnsi(const QString& text)
{
    QString out;
    AnsiParseState state = AnsiParseState::None;
    for (const QChar ch : text)
    {
        if (state == AnsiParseState::None && ch == QChar(0x1b))
        {
            state = AnsiParseState::Escape;
            continue;
        }
        if (state == AnsiParseState::Escape)
        {
            state = ch == QLatin1Char('[') ? AnsiParseState::Csi : AnsiParseState::None;
            continue;
        }
        if (state == AnsiParseState::Csi)
        {
            if (isAnsiFinalByte(ch))
            {
                state = AnsiParseState::None;
            }
            continue;
        }
        out += ch;
    }
    return out;
}

QString forceBlackBackground(QString text)
{
    text.replace(QStringLiteral("\x1b[0m"), QStringLiteral("\x1b[0m\x1b[48;2;0;0;0m"));
    return text;
}

int cellWidth(QChar ch)
{
    const ushort value = ch.unicode();
    if (value < 32 || value == 127)
    {
        return 0;
    }
    if ((value >= 0x1100 && value <= 0x115f) ||
        (value >= 0x2329 && value <= 0x232a) ||
        (value >= 0x2e80 && value <= 0xa4cf) ||
        (value >= 0xac00 && value <= 0xd7a3) ||
        (value >= 0xf900 && value <= 0xfaff) ||
        (value >= 0xfe10 && value <= 0xfe19) ||
        (value >= 0xfe30 && value <= 0xfe6f) ||
        (value >= 0xff00 && value <= 0xff60) ||
        (value >= 0xffe0 && value <= 0xffe6))
    {
        return 2;
    }
    return 1;
}

int displayWidth(const QString& text)
{
    int width = 0;
    AnsiParseState state = AnsiParseState::None;
    for (const QChar ch : text)
    {
        if (state == AnsiParseState::None && ch == QChar(0x1b))
        {
            state = AnsiParseState::Escape;
            continue;
        }
        if (state == AnsiParseState::Escape)
        {
            state = ch == QLatin1Char('[') ? AnsiParseState::Csi : AnsiParseState::None;
            continue;
        }
        if (state == AnsiParseState::Csi)
        {
            if (isAnsiFinalByte(ch))
            {
                state = AnsiParseState::None;
            }
            continue;
        }
        width += cellWidth(ch);
    }
    return width;
}

QString fitPlain(const QString& text, int width)
{
    if (width <= 0)
    {
        return QString();
    }
    QString out;
    int used = 0;
    for (const QChar ch : text)
    {
        const int charWidth = cellWidth(ch);
        if (used + charWidth > width)
        {
            break;
        }
        out += ch;
        used += charWidth;
    }
    return out;
}

QString loadLogo(const QString& requestedPath)
{
    QStringList candidates;
    if (!requestedPath.isEmpty())
    {
        candidates << requestedPath;
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << appDir + QStringLiteral("/resources/sky_startup_logo_ansi.txt")
               << appDir + QStringLiteral("/../resources/sky_startup_logo_ansi.txt")
               << QCoreApplication::applicationDirPath() + QStringLiteral("/../../resources/sky_startup_logo_ansi.txt")
               << QStringLiteral("resources/sky_startup_logo_ansi.txt");

    for (const QString& path : candidates)
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
        {
            return QString::fromUtf8(file.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        }
    }
    return QStringLiteral("VaporView Sky Mode");
}

QStringList visibleLogoLines(const QString& logo)
{
    QStringList lines = logo.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && stripAnsi(lines.last()).trimmed().isEmpty())
    {
        lines.removeLast();
    }
    while (!lines.isEmpty() && stripAnsi(lines.first()).trimmed().isEmpty())
    {
        lines.removeFirst();
    }

    int commonIndent = std::numeric_limits<int>::max();
    for (const QString& line : lines)
    {
        const QString plain = stripAnsi(line);
        if (plain.trimmed().isEmpty())
        {
            continue;
        }
        int indent = 0;
        while (indent < plain.size() && plain.at(indent) == QLatin1Char(' '))
        {
            ++indent;
        }
        commonIndent = std::min(commonIndent, indent);
    }
    if (commonIndent > 0 && commonIndent < std::numeric_limits<int>::max())
    {
        for (QString& line : lines)
        {
            int removed = 0;
            int pos = 0;
            AnsiParseState state = AnsiParseState::None;
            while (pos < line.size() && removed < commonIndent)
            {
                const QChar ch = line.at(pos);
                if (state == AnsiParseState::None && ch == QChar(0x1b))
                {
                    state = AnsiParseState::Escape;
                    ++pos;
                    continue;
                }
                if (state == AnsiParseState::Escape)
                {
                    state = ch == QLatin1Char('[') ? AnsiParseState::Csi : AnsiParseState::None;
                    ++pos;
                    continue;
                }
                if (state == AnsiParseState::Csi)
                {
                    if (isAnsiFinalByte(ch))
                    {
                        state = AnsiParseState::None;
                    }
                    ++pos;
                    continue;
                }
                if (ch != QLatin1Char(' '))
                {
                    break;
                }
                ++removed;
                ++pos;
            }
            if (pos > 0)
            {
                line.remove(0, pos);
            }
        }
    }
    return lines;
}

QString padPlainLine(const QString& line, int width)
{
    QString padded = stripAnsi(line);
    const int missing = width - padded.size();
    if (missing > 0)
    {
        padded += QString(missing, QLatin1Char(' '));
    }
    return padded;
}

bool isOuterLogoCell(int x, int y, int width, int height)
{
    if (width <= 1 || height <= 1)
    {
        return false;
    }

    const double cx = static_cast<double>(width - 1) / 2.0;
    const double cy = static_cast<double>(height - 1) / 2.0;
    const double nx = (static_cast<double>(x) - cx) / std::max(1.0, cx);
    const double ny = (static_cast<double>(y) - cy) / std::max(1.0, cy);
    return std::sqrt(nx * nx + ny * ny) >= 0.72;
}

SkyTuiRgb rotatingLogoColor(int x, int y, int width, int height, double phase)
{
    const double nx = width <= 1 ? 0.0 : static_cast<double>(x) / static_cast<double>(width - 1);
    const double ny = height <= 1 ? 0.0 : static_cast<double>(y) / static_cast<double>(height - 1);
    const double shimmer = (std::sin((nx * 2.2 + ny * 1.6) * kPi + phase) + 1.0) * 0.5;
    return {
        static_cast<int>(105 + 120 * shimmer),
        static_cast<int>(155 + 90 * shimmer),
        255,
    };
}

QString colorizeLogoCell(QChar ch, int x, int y, int width, int height, double phase)
{
    if (ch == QLatin1Char(' '))
    {
        return QString(ch);
    }
    return SkyTuiTheme::foreground(rotatingLogoColor(x, y, width, height, phase)) + QString(ch);
}

QStringList rotateLogoFrame(const QStringList& source, double angleRadians)
{
    int width = 0;
    for (const QString& line : source)
    {
        width = std::max(width, static_cast<int>(stripAnsi(line).size()));
    }
    if (width <= 0 || source.isEmpty())
    {
        return source;
    }

    const int height = source.size();
    QStringList padded;
    padded.reserve(height);
    for (const QString& line : source)
    {
        padded << padPlainLine(line, width);
    }

    const double cx = static_cast<double>(width - 1) / 2.0;
    const double cy = static_cast<double>(height - 1) / 2.0;
    const double cosA = std::cos(angleRadians);
    const double sinA = std::sin(angleRadians);

    QStringList frame;
    frame.reserve(height);
    for (int y = 0; y < height; ++y)
    {
        QString row;
        row.reserve(width * 12);
        for (int x = 0; x < width; ++x)
        {
            QChar ch = QLatin1Char(' ');
            if (isOuterLogoCell(x, y, width, height))
            {
                ch = padded.at(y).at(x);
            }
            else
            {
                const double dx = static_cast<double>(x) - cx;
                const double dy = (static_cast<double>(y) - cy) * kLogoTerminalAspect;
                const double sourceX = dx * cosA + dy * sinA + cx;
                const double sourceY = (-dx * sinA + dy * cosA) / kLogoTerminalAspect + cy;
                const int sx = static_cast<int>(std::llround(sourceX));
                const int sy = static_cast<int>(std::llround(sourceY));
                if (sx >= 0 && sx < width && sy >= 0 && sy < height && !isOuterLogoCell(sx, sy, width, height))
                {
                    ch = padded.at(sy).at(sx);
                }
            }
            row += colorizeLogoCell(ch, x, y, width, height, angleRadians);
        }
        row += SkyTuiTheme::reset();
        frame << row;
    }
    return frame;
}

QVector<QStringList> buildRotatingLogoFrames(const QStringList& logoLines)
{
    QVector<QStringList> frames;
    frames.reserve(kLogoFrameCount);
    for (int frame = 0; frame < kLogoFrameCount; ++frame)
    {
        const double angle = (static_cast<double>(frame) / static_cast<double>(kLogoFrameCount)) * 2.0 * kPi;
        frames.push_back(rotateLogoFrame(logoLines, angle));
    }
    return frames;
}

void drawCenteredText(int row, const QString& text, const SkyTuiTerminalSize& size, const QString& style = QString())
{
    const int column = std::max(1, (size.columns - displayWidth(text)) / 2 + 1);
    writeRaw(SkyTuiTheme::moveTo(row, column) + SkyTuiTheme::background(SkyTuiRgb{0, 0, 0}) +
             style + text + SkyTuiTheme::reset());
}

void drawLogo(const QStringList& lines, const SkyTuiTerminalSize& size, int top)
{
    int logoWidth = 0;
    for (const QString& line : lines)
    {
        logoWidth = std::max(logoWidth, displayWidth(line));
    }
    const int baseColumn = std::max(1, (size.columns - logoWidth) / 2 + 1);
    int row = top;
    for (const QString& line : lines)
    {
        if (row >= size.rows - 4)
        {
            break;
        }
        writeRaw(SkyTuiTheme::moveTo(row++, baseColumn) + SkyTuiTheme::background(SkyTuiRgb{0, 0, 0}) +
                 forceBlackBackground(line) + SkyTuiTheme::reset());
    }
}

void drawProgress(int percent, int row, const SkyTuiTerminalSize& size)
{
    const int barWidth = std::clamp(size.columns - 24, 24, 72);
    const int filled = std::clamp((barWidth * percent) / 100, 0, barWidth);
    const QString bar = QStringLiteral("[") +
                        QString(filled, QChar(0x2588)) +
                        QString(barWidth - filled, QChar(0x2591)) +
                        QStringLiteral("]");
    const QString percentText = QStringLiteral("%1%").arg(percent, 3, 10, QLatin1Char(' '));
    drawCenteredText(row - 1, percentText, size,
                     SkyTuiTheme::foreground(SkyTuiTheme::blue()) + SkyTuiTheme::bold());
    drawCenteredText(row, bar, size, SkyTuiTheme::foreground(SkyTuiTheme::blue()));
}

void setStartupInputMode()
{
#ifdef Q_OS_WIN
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input != INVALID_HANDLE_VALUE)
    {
        DWORD mode = 0;
        if (GetConsoleMode(input, &mode))
        {
            g_startup_original_input_mode = mode;
            g_startup_has_original_input_mode = true;
            mode &= ~ENABLE_PROCESSED_INPUT;
            mode &= ~ENABLE_ECHO_INPUT;
            mode &= ~ENABLE_LINE_INPUT;
            SetConsoleMode(input, mode);
        }
    }
#else
    if (tcgetattr(STDIN_FILENO, &g_startup_original_termios) == 0)
    {
        g_startup_has_original_termios = true;
        termios raw = g_startup_original_termios;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
#endif
}

void restoreStartupInputMode()
{
#ifdef Q_OS_WIN
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (g_startup_has_original_input_mode && input != INVALID_HANDLE_VALUE)
    {
        SetConsoleMode(input, g_startup_original_input_mode);
    }
#else
    if (g_startup_has_original_termios)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_startup_original_termios);
    }
#endif
}

bool pollStartupDecision(SkyStartupDecision& decision)
{
#ifdef Q_OS_WIN
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD eventCount = 0;
    if (input == INVALID_HANDLE_VALUE || !GetNumberOfConsoleInputEvents(input, &eventCount))
    {
        decision = SkyStartupDecision::EnterTui;
        return true;
    }
    while (eventCount-- > 0)
    {
        INPUT_RECORD record;
        DWORD recordsRead = 0;
        if (!ReadConsoleInputW(input, &record, 1, &recordsRead))
        {
            decision = SkyStartupDecision::EnterTui;
            return true;
        }
        if (recordsRead == 0 || record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
        {
            continue;
        }
        const WORD key = record.Event.KeyEvent.wVirtualKeyCode;
        if (key == VK_RETURN)
        {
            decision = SkyStartupDecision::EnterTui;
            return true;
        }
        if (key == VK_ESCAPE)
        {
            decision = SkyStartupDecision::Exit;
            return true;
        }
    }
    return false;
#else
    if (!g_startup_has_original_termios)
    {
        decision = SkyStartupDecision::EnterTui;
        return true;
    }
    unsigned char ch = 0;
    if (read(STDIN_FILENO, &ch, 1) == 1)
    {
        if (ch == 13 || ch == 10)
        {
            decision = SkyStartupDecision::EnterTui;
            return true;
        }
        if (ch == 27)
        {
            decision = SkyStartupDecision::Exit;
            return true;
        }
    }
    return false;
#endif
}

}  // namespace

SkyStartupDecision showSkyStartupScreen(const QString& logo_path)
{
    SkyTuiTheme::enableVirtualTerminal();
    setStartupInputMode();
    writeRaw(SkyTuiTheme::enterAlternateScreen() +
             SkyTuiTheme::background(SkyTuiRgb{0, 0, 0}) +
             SkyTuiTheme::clearScreen() +
             SkyTuiTheme::hideCursor());

    const SkyTuiTerminalSize size = SkyTuiTheme::terminalSize();
    const QStringList logoLines = visibleLogoLines(loadLogo(logo_path));
    const QVector<QStringList> logoFrames = buildRotatingLogoFrames(logoLines);
    const int logoRows = static_cast<int>(logoLines.size());
    const int progressRow = std::min(size.rows - 4, std::max(logoRows + 3, size.rows / 5 + logoRows + 2));
    const int logoTop = std::max(2, progressRow - logoRows - 2);

    int logoFrameIndex = 0;
    auto drawNextLogoFrame = [&]() {
        if (!logoFrames.isEmpty())
        {
            drawLogo(logoFrames.at(logoFrameIndex % logoFrames.size()), size, logoTop);
            ++logoFrameIndex;
            return;
        }
        drawLogo(logoLines, size, logoTop);
    };

    for (int percent = 0; percent <= 100; percent += 4)
    {
        drawNextLogoFrame();
        drawProgress(percent, progressRow, size);
        QThread::msleep(percent < 100 ? 42 : 80);
    }

    const QString message = QStringLiteral("-- 加载完成，按 Enter 进入 TUI，按 Esc 退出 --");
    drawCenteredText(progressRow + 2, fitPlain(message, size.columns - 4), size,
                     SkyTuiTheme::foreground(SkyTuiTheme::yellow()) + SkyTuiTheme::bold());

    SkyStartupDecision decision = SkyStartupDecision::EnterTui;
    while (!pollStartupDecision(decision))
    {
        drawNextLogoFrame();
        QThread::msleep(80);
    }
    restoreStartupInputMode();
    writeRaw(SkyTuiTheme::hideCursor() + SkyTuiTheme::clearScreen() + SkyTuiTheme::leaveAlternateScreen());
    return decision;
}

}  // namespace VaporView
