#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "SkyTuiApp.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QPointer>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <iostream>

#ifdef Q_OS_WIN
#include <conio.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace VaporView
{
namespace
{
#ifndef Q_OS_WIN
termios g_original_termios;
bool g_has_original_termios = false;
#endif

void writeRaw(const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    std::cout.write(bytes.constData(), bytes.size());
    std::cout.flush();
}

QString plainCommand(const QString& command)
{
    QString normalized = command.simplified();
    if (normalized.startsWith(QLatin1Char('/')))
    {
        normalized.remove(0, 1);
    }
    return normalized.simplified();
}

bool isPrintableCommandChar(QChar ch)
{
    return ch.unicode() >= 32 && ch.unicode() != 127;
}

bool isAnsiFinalByte(QChar ch)
{
    const ushort value = ch.unicode();
    return value >= 0x40 && value <= 0x7e;
}

int terminalCellWidth(QChar ch)
{
    const ushort value = ch.unicode();
    if (value < 32 || value == 127)
    {
        return 0;
    }

    const QChar::Category category = ch.category();
    if (category == QChar::Mark_NonSpacing ||
        category == QChar::Mark_SpacingCombining ||
        category == QChar::Mark_Enclosing)
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

}  // namespace

SkyTuiApp::SkyTuiApp(SkyRuntime *runtime, const SkyRuntimeOptions& options, QObject *parent)
    : QObject(parent)
    , runtime_(runtime)
    , options_(options)
    , controller_(runtime, options, this)
    , input_running_(std::make_shared<std::atomic_bool>(false))
{
    qRegisterMetaType<SkyTuiKey>("VaporView::SkyTuiKey");

    render_timer_.setInterval(16);
    render_timer_.setSingleShot(true);
    render_timer_.setTimerType(Qt::PreciseTimer);
    connect(&render_timer_, &QTimer::timeout, this, &SkyTuiApp::render);

    status_timer_.setInterval(500);
    status_timer_.setTimerType(Qt::CoarseTimer);
    connect(&status_timer_, &QTimer::timeout, this, &SkyTuiApp::refreshStatus);

    if (runtime_)
    {
        connect(runtime_, &SkyRuntime::runningChanged, this, [this](bool running) {
            appendLog(running ? QStringLiteral("天空端运行中") : QStringLiteral("天空端已停止"));
        });
    }
}

SkyTuiApp::~SkyTuiApp()
{
    input_running_->store(false);
    restoreTerminal();
}

void SkyTuiApp::start()
{
    if (started_)
    {
        return;
    }
    started_ = true;
    terminal_restored_ = false;
    SkyTuiTheme::enableVirtualTerminal();
    writeRaw(SkyTuiTheme::enterAlternateScreen() + SkyTuiTheme::clearScreen());

    model_.config = runtime_ ? runtime_->currentConfig() : SkyConfig::defaults();
    refreshStatus();
    appendLog(QStringLiteral("VaporViewSky TUI 已启动"));
    appendLog(QStringLiteral("输入 /help 查看命令，按 Ctrl+P 打开命令面板。"));
    startInputThread();
    status_timer_.start();
    scheduleRender();
}

void SkyTuiApp::appendLog(const QString& message)
{
    model_.logs << makeLogLine(message);
    while (model_.logs.size() > SkyTuiModel::MaxLogLines)
    {
        model_.logs.removeFirst();
    }
    if (model_.log_scroll > 0)
    {
        model_.log_scroll = std::min(model_.log_scroll, static_cast<int>(model_.logs.size()));
    }
    scheduleRender();
}

void SkyTuiApp::render()
{
    render_pending_ = false;
    if (!started_ || terminal_restored_)
    {
        return;
    }

    const SkyTuiTerminalSize size = SkyTuiTheme::terminalSize();
    QString output;
    output.reserve(size.columns * size.rows * 2);
    output += SkyTuiTheme::beginSynchronizedUpdate();
    output += SkyTuiTheme::hideCursor();
    output += SkyTuiTheme::clearScreen();

    int row = 2;
    drawLogo(output, row, size);

    const int paletteHeight = model_.palette_visible ? std::min(8, std::max(5, size.rows / 4)) : 0;
    const int paletteTop = model_.palette_visible ? size.rows - paletteHeight - 2 : size.rows;
    const int mainTop = std::min(row + 1, size.rows - 8);
    const int mainBottom = model_.palette_visible ? paletteTop - 1 : size.rows - 4;
    if (mainBottom > mainTop)
    {
        drawMainPanels(output, mainTop, mainBottom, size);
    }

    if (model_.palette_visible)
    {
        drawPalette(output, paletteTop, size.rows - 3, size);
    }
    else
    {
        drawText(output, size.rows - 3, 2,
                 SkyTuiTheme::foreground(SkyTuiTheme::muted()) +
                     fitPlain(model_.hint, size.columns - 2) +
                     SkyTuiTheme::reset());
    }

    drawInput(output, size.rows - 2, size);
    drawStatusBar(output, size.rows, size);

    const int cursorColumn = std::min(size.columns, 7 + displayWidth(model_.input_text));
    output += SkyTuiTheme::moveTo(size.rows - 2, cursorColumn);
    output += SkyTuiTheme::showCursor();
    output += SkyTuiTheme::endSynchronizedUpdate();
    writeRaw(output);
}

void SkyTuiApp::refreshStatus()
{
    if (!runtime_)
    {
        return;
    }
    model_.status = runtime_->currentStatus();
    model_.config = runtime_->currentConfig();
    scheduleRender();
}

void SkyTuiApp::handleKey(const SkyTuiKey& key)
{
    if (model_.quitting)
    {
        return;
    }

    switch (key.type)
    {
    case SkyTuiKeyType::CtrlC:
        requestQuit();
        return;
    case SkyTuiKeyType::CtrlL:
        clearLogs();
        return;
    case SkyTuiKeyType::CtrlP:
    case SkyTuiKeyType::Tab:
        setPaletteVisible(true);
        scheduleRender();
        return;
    case SkyTuiKeyType::Escape:
        setPaletteVisible(false);
        scheduleRender();
        return;
    case SkyTuiKeyType::Up:
        if (model_.palette_visible)
        {
            --model_.palette_selected;
            clampPaletteSelection();
        }
        else
        {
            model_.log_scroll = std::min(model_.log_scroll + 1, std::max(0, static_cast<int>(model_.logs.size()) - 1));
        }
        scheduleRender();
        return;
    case SkyTuiKeyType::Down:
        if (model_.palette_visible)
        {
            ++model_.palette_selected;
            clampPaletteSelection();
        }
        else
        {
            model_.log_scroll = std::max(0, model_.log_scroll - 1);
        }
        scheduleRender();
        return;
    case SkyTuiKeyType::PageUp:
        model_.log_scroll = std::min(model_.log_scroll + 8, std::max(0, static_cast<int>(model_.logs.size()) - 1));
        scheduleRender();
        return;
    case SkyTuiKeyType::PageDown:
        model_.log_scroll = std::max(0, model_.log_scroll - 8);
        scheduleRender();
        return;
    case SkyTuiKeyType::Backspace:
        if (!model_.input_text.isEmpty())
        {
            model_.input_text.chop(1);
        }
        if (!model_.input_text.startsWith(QLatin1Char('/')))
        {
            setPaletteVisible(false);
        }
        clampPaletteSelection();
        scheduleRender();
        return;
    case SkyTuiKeyType::Enter:
        executeInput();
        return;
    case SkyTuiKeyType::Character:
        if (key.character == QLatin1Char('q') && model_.input_text.isEmpty() && !model_.palette_visible)
        {
            requestQuit();
            return;
        }
        if (isPrintableCommandChar(key.character))
        {
            model_.input_text += key.character;
            if (model_.input_text.startsWith(QLatin1Char('/')))
            {
                setPaletteVisible(true);
            }
        }
        clampPaletteSelection();
        scheduleRender();
        return;
    default:
        scheduleRender();
        return;
    }
}

void SkyTuiApp::startInputThread()
{
    input_running_->store(true);
    QPointer<SkyTuiApp> self(this);
    const std::shared_ptr<std::atomic_bool> running = input_running_;

#ifndef Q_OS_WIN
    if (tcgetattr(STDIN_FILENO, &g_original_termios) == 0)
    {
        g_has_original_termios = true;
        termios raw = g_original_termios;
        raw.c_lflag &= ~(ICANON | ECHO | ISIG);
        raw.c_iflag &= ~(IXON | ICRNL);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
#endif

    input_thread_ = std::thread([self, running]() {
        auto postKey = [self](const SkyTuiKey& key) {
            if (!self)
            {
                return;
            }
            QMetaObject::invokeMethod(self.data(), [self, key]() {
                if (self)
                {
                    self->handleKey(key);
                }
            }, Qt::QueuedConnection);
        };

        while (running->load())
        {
#ifdef Q_OS_WIN
            const int value = _getwch();
            SkyTuiKey key;
            if (value == 0 || value == 224)
            {
                const int code = _getwch();
                if (code == 72) key.type = SkyTuiKeyType::Up;
                else if (code == 80) key.type = SkyTuiKeyType::Down;
                else if (code == 73) key.type = SkyTuiKeyType::PageUp;
                else if (code == 81) key.type = SkyTuiKeyType::PageDown;
                else key.type = SkyTuiKeyType::Unknown;
            }
            else if (value == 13) key.type = SkyTuiKeyType::Enter;
            else if (value == 8) key.type = SkyTuiKeyType::Backspace;
            else if (value == 27) key.type = SkyTuiKeyType::Escape;
            else if (value == 9) key.type = SkyTuiKeyType::Tab;
            else if (value == 3) key.type = SkyTuiKeyType::CtrlC;
            else if (value == 12) key.type = SkyTuiKeyType::CtrlL;
            else if (value == 16) key.type = SkyTuiKeyType::CtrlP;
            else
            {
                key.type = SkyTuiKeyType::Character;
                key.character = QChar(value);
            }
            postKey(key);
#else
            unsigned char ch = 0;
            if (read(STDIN_FILENO, &ch, 1) != 1)
            {
                continue;
            }
            SkyTuiKey key;
            if (ch == 13 || ch == 10) key.type = SkyTuiKeyType::Enter;
            else if (ch == 127 || ch == 8) key.type = SkyTuiKeyType::Backspace;
            else if (ch == 9) key.type = SkyTuiKeyType::Tab;
            else if (ch == 3) key.type = SkyTuiKeyType::CtrlC;
            else if (ch == 12) key.type = SkyTuiKeyType::CtrlL;
            else if (ch == 16) key.type = SkyTuiKeyType::CtrlP;
            else if (ch == 27)
            {
                unsigned char seq[2] = {0, 0};
                const ssize_t count = read(STDIN_FILENO, seq, 2);
                if (count == 2 && seq[0] == '[')
                {
                    if (seq[1] == 'A') key.type = SkyTuiKeyType::Up;
                    else if (seq[1] == 'B') key.type = SkyTuiKeyType::Down;
                    else if (seq[1] == '5') key.type = SkyTuiKeyType::PageUp;
                    else if (seq[1] == '6') key.type = SkyTuiKeyType::PageDown;
                    else key.type = SkyTuiKeyType::Escape;
                }
                else
                {
                    key.type = SkyTuiKeyType::Escape;
                }
            }
            else
            {
                key.type = SkyTuiKeyType::Character;
                key.character = QChar(ch);
            }
            postKey(key);
#endif
        }
    });
    input_thread_.detach();
}

void SkyTuiApp::restoreTerminal()
{
    if (terminal_restored_)
    {
        return;
    }
    terminal_restored_ = true;
    input_running_->store(false);
    render_timer_.stop();
    status_timer_.stop();
#ifndef Q_OS_WIN
    if (g_has_original_termios)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_original_termios);
    }
#endif
    writeRaw(SkyTuiTheme::leaveAlternateScreen());
}

void SkyTuiApp::scheduleRender()
{
    if (!started_ || terminal_restored_ || render_pending_)
    {
        return;
    }
    render_pending_ = true;
    render_timer_.start();
}

void SkyTuiApp::executeInput()
{
    QString command = model_.input_text.simplified();
    if (model_.palette_visible)
    {
        const QList<SkyTuiCommandItem> items = filteredPalette();
        if (!items.isEmpty())
        {
            command = items.at(std::clamp(model_.palette_selected, 0, static_cast<int>(items.size()) - 1)).command;
        }
    }

    model_.input_text.clear();
    setPaletteVisible(false);
    executeCommand(command);
}

void SkyTuiApp::executeCommand(const QString& command)
{
    const QString normalized = command.simplified();
    if (normalized.isEmpty())
    {
        scheduleRender();
        return;
    }

    model_.last_command = normalized;
    model_.show_logo = false;
    appendLog(QStringLiteral("sky> %1").arg(normalized));
    const SkyTuiCommandResult result = controller_.executeCommand(normalized);
    if (result.type == SkyTuiCommandResult::Type::ClearLogs)
    {
        clearLogs();
    }
    else
    {
        for (const QString& message : result.messages)
        {
            appendLog(message);
        }
    }

    if (result.type == SkyTuiCommandResult::Type::Quit)
    {
        requestQuit();
        return;
    }

    model_.hint = QStringLiteral("上一条命令：%1").arg(plainCommand(normalized));
    scheduleRender();
}

void SkyTuiApp::requestQuit()
{
    if (model_.quitting)
    {
        return;
    }
    model_.quitting = true;
    appendLog(QStringLiteral("正在停止天空端..."));
    if (runtime_)
    {
        runtime_->stop();
    }
    appendLog(QStringLiteral("已退出。"));
    restoreTerminal();
    QCoreApplication::quit();
}

void SkyTuiApp::clearLogs()
{
    model_.logs.clear();
    model_.log_scroll = 0;
    model_.hint = QStringLiteral("日志已清空");
    appendLog(QStringLiteral("已清空当前可视日志"));
}

void SkyTuiApp::setPaletteVisible(bool visible)
{
    model_.palette_visible = visible;
    if (visible && model_.input_text.isEmpty())
    {
        model_.input_text = QStringLiteral("/");
    }
    clampPaletteSelection();
}

QList<SkyTuiCommandItem> SkyTuiApp::filteredPalette() const
{
    QList<SkyTuiCommandItem> filtered;
    const QList<SkyTuiCommandItem> all = controller_.commandPalette();
    const QString prefix = model_.input_text.trimmed().toLower();
    for (const SkyTuiCommandItem& item : all)
    {
        if (prefix.isEmpty() ||
            prefix == QStringLiteral("/") ||
            item.command.toLower().startsWith(prefix))
        {
            filtered.push_back(item);
        }
    }
    return filtered.isEmpty() ? all : filtered;
}

void SkyTuiApp::clampPaletteSelection()
{
    const QList<SkyTuiCommandItem> items = filteredPalette();
    if (items.isEmpty())
    {
        model_.palette_selected = 0;
        return;
    }
    model_.palette_selected = std::clamp(model_.palette_selected, 0, static_cast<int>(items.size()) - 1);
}

QString SkyTuiApp::timestamp() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}

QString SkyTuiApp::makeLogLine(const QString& message) const
{
    return QStringLiteral("[%1] %2").arg(timestamp(), message);
}

int SkyTuiApp::displayWidth(const QString& text) const
{
    int width = 0;
    for (int i = 0; i < text.size(); ++i)
    {
        const QChar ch = text.at(i);
        if (ch == QChar(0x1b) && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('['))
        {
            i += 2;
            while (i < text.size() && !isAnsiFinalByte(text.at(i)))
            {
                ++i;
            }
            continue;
        }
        width += terminalCellWidth(ch);
    }
    return width;
}

QString SkyTuiApp::fitPlain(const QString& text, int width) const
{
    if (width <= 0)
    {
        return QString();
    }
    if (displayWidth(text) <= width)
    {
        return text;
    }
    if (width <= 1)
    {
        return QStringLiteral("…");
    }

    const int targetWidth = width - 1;
    QString output;
    int used = 0;
    for (int i = 0; i < text.size(); ++i)
    {
        const QChar ch = text.at(i);
        if (ch == QChar(0x1b) && i + 1 < text.size() && text.at(i + 1) == QLatin1Char('['))
        {
            output += ch;
            output += text.at(++i);
            while (i + 1 < text.size())
            {
                const QChar seq = text.at(++i);
                output += seq;
                if (isAnsiFinalByte(seq))
                {
                    break;
                }
            }
            continue;
        }

        const int charWidth = terminalCellWidth(ch);
        if (used + charWidth > targetWidth)
        {
            break;
        }
        output += ch;
        used += charWidth;
    }
    output += QStringLiteral("…");
    return output;
}

QString SkyTuiApp::padPlain(const QString& text, int width) const
{
    QString value = fitPlain(text, width);
    const int currentWidth = displayWidth(value);
    if (currentWidth < width)
    {
        value += QString(width - currentWidth, QLatin1Char(' '));
    }
    return value;
}

void SkyTuiApp::drawText(QString& output, int row, int column, const QString& text) const
{
    output += SkyTuiTheme::moveTo(row, column);
    output += text;
    output += SkyTuiTheme::reset();
}

void SkyTuiApp::drawBox(QString& output, int top, int left, int bottom, int right, const QString& title) const
{
    if (bottom <= top || right <= left)
    {
        return;
    }
    const int width = right - left + 1;
    const QString horizontal = QString(width - 2, QLatin1Char('-'));
    drawText(output, top, left, SkyTuiTheme::foreground(SkyTuiTheme::muted()) + QStringLiteral("+") + horizontal + QStringLiteral("+"));
    for (int row = top + 1; row < bottom; ++row)
    {
        drawText(output, row, left, SkyTuiTheme::foreground(SkyTuiTheme::muted()) + QStringLiteral("|"));
        drawText(output, row, right, SkyTuiTheme::foreground(SkyTuiTheme::muted()) + QStringLiteral("|"));
    }
    drawText(output, bottom, left, SkyTuiTheme::foreground(SkyTuiTheme::muted()) + QStringLiteral("+") + horizontal + QStringLiteral("+"));
    if (!title.isEmpty() && width > 6)
    {
        drawText(output, top, left + 2,
                 SkyTuiTheme::bold() + SkyTuiTheme::foreground(SkyTuiTheme::accent()) +
                     QStringLiteral(" ") + fitPlain(title, width - 4) + QStringLiteral(" "));
    }
}

void SkyTuiApp::drawLogo(QString& output, int& row, const SkyTuiTerminalSize& size) const
{
    const QString title = QStringLiteral("VaporView Sky Mode");
    if (!model_.show_logo)
    {
        drawText(output, row++, 2,
                 SkyTuiTheme::bold() + SkyTuiTheme::foreground(SkyTuiTheme::yellow()) +
                     fitPlain(title, size.columns - 4));
    }
    else
    {
    const QStringList logo = SkyTuiTheme::logoLines();
    const int logoWidth = logo.isEmpty() ? 0 : logo.first().size();
    const bool compact = size.rows < 28 || size.columns < logoWidth + 4;
    if (compact)
    {
        const int column = std::max(2, (size.columns - displayWidth(title)) / 2);
        drawText(output, row++, column,
                 SkyTuiTheme::bold() + SkyTuiTheme::foreground(SkyTuiTheme::yellow()) + title);
    }
    else
    {
        for (const QString& line : logo)
        {
            const int column = std::max(1, (size.columns - logoWidth) / 2);
            drawText(output, row++, column, SkyTuiTheme::gradientLogoLine(line));
        }
        ++row;
        drawText(output, row++, std::max(2, (size.columns - displayWidth(title)) / 2),
                 SkyTuiTheme::bold() + SkyTuiTheme::foreground(SkyTuiTheme::yellow()) + title);
    }
    }

    const QString configPath = options_.config_path.isEmpty()
                                   ? QStringLiteral("(默认 sky_config.json)")
                                   : options_.config_path;
    QString summary = QStringLiteral("数传 %1 @ %2 | 配置 %3 | 模拟数据 %4 | 波形源 %5:%6")
                          .arg(options_.telemetry_port)
                          .arg(options_.telemetry_baud)
                          .arg(configPath)
                          .arg(options_.simulate_data ? QStringLiteral("开") : QStringLiteral("关"))
                          .arg(options_.wave_host)
                          .arg(options_.wave_port);
    summary = fitPlain(summary, size.columns - 4);
    drawText(output, row++, model_.show_logo ? std::max(2, (size.columns - displayWidth(summary)) / 2) : 2,
             SkyTuiTheme::foreground(SkyTuiTheme::muted()) + summary);
}

void SkyTuiApp::drawMainPanels(QString& output, int top, int bottom, const SkyTuiTerminalSize& size) const
{
    const bool hasRightPanel = size.columns >= 92;
    const int gap = hasRightPanel ? 2 : 0;
    const int rightWidth = hasRightPanel ? std::min(44, std::max(32, size.columns / 3)) : 0;
    const int left = 2;
    const int right = size.columns - 1;
    const int leftRight = hasRightPanel ? right - rightWidth - gap : right;
    const int rightLeft = hasRightPanel ? leftRight + gap + 1 : right + 1;

    drawBox(output, top, left, bottom, leftRight, QStringLiteral("日志流"));
    const int logRows = std::max(0, bottom - top - 1);
    const int logWidth = std::max(4, leftRight - left - 3);
    const int latestEnd = std::max(0, static_cast<int>(model_.logs.size()) - model_.log_scroll);
    const int start = std::max(0, latestEnd - logRows);
    int row = top + 1;
    for (int i = start; i < latestEnd && row < bottom; ++i, ++row)
    {
        drawText(output, row, left + 2,
                 SkyTuiTheme::foreground(i == static_cast<int>(model_.logs.size()) - 1 ? SkyTuiTheme::green() : SkyTuiTheme::muted()) +
                     fitPlain(model_.logs.at(i), logWidth));
    }
    if (model_.log_scroll > 0 && row <= bottom - 1)
    {
        drawText(output, bottom - 1, left + 2,
                 SkyTuiTheme::foreground(SkyTuiTheme::yellow()) +
                     QStringLiteral("已向上滚动 %1 行。按 PageDown/Down 回到最新日志。").arg(model_.log_scroll));
    }

    if (!hasRightPanel)
    {
        return;
    }

    drawBox(output, top, rightLeft, bottom, right, QStringLiteral("天空端状态"));
    const QStringList lines = statusPanelLines();
    const int contentWidth = std::max(4, right - rightLeft - 3);
    row = top + 1;
    for (const QString& line : lines)
    {
        if (row >= bottom)
        {
            break;
        }
        drawText(output, row++, rightLeft + 2, fitPlain(line, contentWidth));
    }
}

void SkyTuiApp::drawPalette(QString& output, int top, int bottom, const SkyTuiTerminalSize& size)
{
    drawBox(output, top, 2, bottom, size.columns - 1, QStringLiteral("命令面板"));
    const QList<SkyTuiCommandItem> items = filteredPalette();
    clampPaletteSelection();
    const int maxRows = std::max(0, bottom - top - 1);
    const int width = size.columns - 6;
    const int start = std::max(0, std::min(model_.palette_selected - maxRows / 2, std::max(0, static_cast<int>(items.size()) - maxRows)));

    int row = top + 1;
    for (int i = start; i < static_cast<int>(items.size()) && row < bottom; ++i, ++row)
    {
        QString line = QStringLiteral("%1  %2")
                           .arg(padPlain(items.at(i).command, 26), items.at(i).description);
        line = padPlain(line, width);
        if (i == model_.palette_selected)
        {
            drawText(output, row, 4,
                     SkyTuiTheme::inverse() + SkyTuiTheme::foreground(SkyTuiTheme::yellow()) + line);
        }
        else
        {
            drawText(output, row, 4,
                     SkyTuiTheme::foreground(SkyTuiTheme::muted()) + line);
        }
    }
}

void SkyTuiApp::drawInput(QString& output, int row, const SkyTuiTerminalSize& size) const
{
    const QString prompt = QStringLiteral("sky> ");
    const int width = size.columns - 3;
    const QString input = fitPlain(prompt + model_.input_text, width);
    drawText(output, row, 2,
             SkyTuiTheme::bold() + SkyTuiTheme::foreground(SkyTuiTheme::blue()) +
                 padPlain(input, width));
}

void SkyTuiApp::drawStatusBar(QString& output, int row, const SkyTuiTerminalSize& size) const
{
    const QString path = QCoreApplication::applicationDirPath();
    QString left = QStringLiteral(" Tab 命令  Ctrl+P 面板  Ctrl+L 清屏  Ctrl+C 退出 ");
    QString right = QStringLiteral(" %1 ").arg(path);
    if (displayWidth(left) + displayWidth(right) > size.columns)
    {
        right.clear();
    }
    QString bar = left;
    if (right.isEmpty())
    {
        bar = padPlain(bar, size.columns);
    }
    else
    {
        bar += QString(std::max(0, size.columns - displayWidth(left) - displayWidth(right)), QLatin1Char(' '));
        bar += right;
    }
    drawText(output, row, 1,
             SkyTuiTheme::inverse() + SkyTuiTheme::foreground(SkyTuiTheme::muted()) + fitPlain(bar, size.columns));
}

QStringList SkyTuiApp::statusPanelLines() const
{
    QStringList lines;
    lines << QStringLiteral("运行：%1").arg(runtime_ && runtime_->isRunning() ? QStringLiteral("是") : QStringLiteral("否"))
          << QStringLiteral("记录：%1").arg(recordingStateText(model_.status.recording_state))
          << QStringLiteral("Session: %1").arg(model_.status.session_name.isEmpty() ? QStringLiteral("-") : model_.status.session_name)
          << QStringLiteral("磁盘：%1").arg(humanBytes(model_.status.disk_free_bytes))
          << QStringLiteral("波形下传：%1").arg(runtime_ && runtime_->waveformStreamingEnabled() ? QStringLiteral("开启") : QStringLiteral("关闭"))
          << QStringLiteral("频率：基础 %1Hz").arg(model_.status.telemetry_basic_rate_hz, 0, 'f', 1)
          << QStringLiteral("      特征 %1Hz").arg(model_.status.feature_rate_hz, 0, 'f', 1)
          << QStringLiteral("      波形 %1Hz").arg(model_.status.waveform_rate_hz, 0, 'f', 1)
          << QStringLiteral("      心跳 %1Hz").arg(model_.status.heartbeat_rate_hz, 0, 'f', 1)
          << QStringLiteral("      状态 %1Hz").arg(model_.status.status_rate_hz, 0, 'f', 1)
          << QStringLiteral("接收帧：%1").arg(model_.status.rx_total_frames)
          << QStringLiteral("CRC 错误：%1").arg(model_.status.crc_error_count)
          << QStringLiteral("");

    lines << SkyTuiTheme::bold() + SkyTuiTheme::foreground(SkyTuiTheme::accent()) + QStringLiteral("设备") + SkyTuiTheme::reset();
    for (const DeviceStatusItem& item : model_.status.devices)
    {
        lines << QStringLiteral("%1: %2")
                     .arg(skyDeviceIdName(item.device_id), deviceStateColored(item.state));
        lines << QStringLiteral("  接收 %1  错误 %2").arg(item.rx_count).arg(item.error_count);
        lines << QStringLiteral("  最近 %1  错误码 %2").arg(item.last_data_time_us).arg(item.error_code);
    }
    if (model_.status.devices.isEmpty())
    {
        lines << QStringLiteral("暂无设备状态");
    }
    return lines;
}

QString SkyTuiApp::deviceStateColored(DeviceState state) const
{
    SkyTuiRgb color = SkyTuiTheme::muted();
    if (state == DeviceState::Connected)
    {
        color = SkyTuiTheme::green();
    }
    else if (state == DeviceState::Connecting || state == DeviceState::Reconnecting)
    {
        color = SkyTuiTheme::yellow();
    }
    else if (state == DeviceState::Error)
    {
        color = SkyTuiTheme::red();
    }
    return SkyTuiTheme::foreground(color) + deviceStateName(state) + SkyTuiTheme::reset();
}

QString SkyTuiApp::recordingStateText(quint8 state) const
{
    switch (state)
    {
    case 1:
        return QStringLiteral("记录中");
    case 2:
        return QStringLiteral("已暂停");
    default:
        return QStringLiteral("未记录");
    }
}

QString SkyTuiApp::humanBytes(quint64 bytes) const
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', unit == 0 ? 0 : 1).arg(units[unit]);
}

}  // namespace VaporView
