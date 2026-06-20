#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

#include <cstdlib>
#include <iostream>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace
{

void fail(const QString& message,
          QProcess *process = nullptr,
          const QString& stdoutText = QString(),
          const QString& stderrText = QString())
{
    if (process && process->state() != QProcess::NotRunning)
    {
        process->kill();
        process->waitForFinished(5000);
    }

    std::cerr << message.toLocal8Bit().constData() << '\n';
    if (!stdoutText.isEmpty())
    {
        std::cerr << "stdout:\n" << stdoutText.toLocal8Bit().constData();
        if (!stdoutText.endsWith(QLatin1Char('\n')))
        {
            std::cerr << '\n';
        }
    }
    if (!stderrText.isEmpty())
    {
        std::cerr << "stderr:\n" << stderrText.toLocal8Bit().constData();
        if (!stderrText.endsWith(QLatin1Char('\n')))
        {
            std::cerr << '\n';
        }
    }
    std::exit(1);
}

void require(bool condition, const QString& message)
{
    if (!condition)
    {
        fail(message);
    }
}

QString readProcessOutput(QProcess& process)
{
    return QString::fromLocal8Bit(process.readAllStandardOutput()) +
           QString::fromLocal8Bit(process.readAllStandardError());
}

QString mainExecutablePath()
{
#ifdef Q_OS_WIN
    const QString executableName = QStringLiteral("VaporView.exe");
#else
    const QString executableName = QStringLiteral("VaporView");
#endif

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath(executableName),
        QDir(appDir).filePath(QStringLiteral("Release/%1").arg(executableName)),
        QDir(appDir).filePath(QStringLiteral("../%1").arg(executableName))
    };
    for (const QString& candidate : candidates)
    {
        if (QFileInfo::exists(candidate))
        {
            return candidate;
        }
    }
    return candidates.front();
}

#ifdef Q_OS_WIN
struct WindowSearchState
{
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto *state = reinterpret_cast<WindowSearchState *>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != state->pid)
    {
        return TRUE;
    }

    if (!IsWindowVisible(hwnd))
    {
        return TRUE;
    }

    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, static_cast<int>(sizeof(title) / sizeof(title[0])));
    if (QString::fromWCharArray(title).contains(QStringLiteral("VaporView")))
    {
        state->hwnd = hwnd;
        return FALSE;
    }

    return TRUE;
}

HWND findMainWindow(DWORD pid)
{
    WindowSearchState state;
    state.pid = pid;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&state));
    return state.hwnd;
}

QString windowTitle(HWND hwnd)
{
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, static_cast<int>(sizeof(title) / sizeof(title[0])));
    return QString::fromWCharArray(title);
}
#endif

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString exePath = mainExecutablePath();
    require(QFileInfo::exists(exePath), QStringLiteral("main executable not found at %1").arg(exePath));

    QProcess process;
    process.setProgram(exePath);
    process.setWorkingDirectory(QFileInfo(exePath).absolutePath());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000))
    {
        fail(QStringLiteral("failed to start main executable"),
             &process,
             readProcessOutput(process));
    }

    QString stdoutText;
    QString stderrText;

#ifdef Q_OS_WIN
    const DWORD pid = static_cast<DWORD>(process.processId());
    QElapsedTimer timer;
    timer.start();

    HWND hwnd = nullptr;
    while (timer.elapsed() < 15000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        stdoutText += QString::fromLocal8Bit(process.readAllStandardOutput());
        stderrText += QString::fromLocal8Bit(process.readAllStandardError());

        if (process.state() == QProcess::NotRunning)
        {
            fail(QStringLiteral("VaporView exited before its main window appeared"),
                 &process,
                 stdoutText,
                 stderrText);
        }

        hwnd = findMainWindow(pid);
        if (hwnd != nullptr)
        {
            break;
        }

        QThread::msleep(50);
    }

    if (hwnd == nullptr)
    {
        fail(QStringLiteral("VaporView main window did not appear within 15 seconds"),
             &process,
             stdoutText,
             stderrText);
    }

    const QString title = windowTitle(hwnd);
    require(title.contains(QStringLiteral("VaporView")),
            QStringLiteral("main window title did not contain VaporView: %1").arg(title));
    require(IsWindowVisible(hwnd), QStringLiteral("main window is not visible"));

    QThread::msleep(500);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    stdoutText += QString::fromLocal8Bit(process.readAllStandardOutput());
    stderrText += QString::fromLocal8Bit(process.readAllStandardError());
    require(process.state() == QProcess::Running, QStringLiteral("VaporView stopped too early"));
    require(findMainWindow(pid) != nullptr, QStringLiteral("main window disappeared too early"));

    process.kill();
    if (!process.waitForFinished(5000))
    {
        fail(QStringLiteral("failed to terminate VaporView.exe after smoke test"),
             &process,
             stdoutText,
             stderrText);
    }

    std::cout << "vaporview_startup_test passed\n";
    return 0;
#else
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        stdoutText += QString::fromLocal8Bit(process.readAllStandardOutput());
        stderrText += QString::fromLocal8Bit(process.readAllStandardError());
        if (process.state() == QProcess::NotRunning)
        {
            fail(QStringLiteral("main executable exited immediately"),
                 &process,
                 stdoutText,
                 stderrText);
        }
        QThread::msleep(50);
    }

    process.kill();
    if (!process.waitForFinished(5000))
    {
        fail(QStringLiteral("failed to terminate main executable after smoke test"),
             &process,
             stdoutText,
             stderrText);
    }

    std::cout << "vaporview_startup_test passed\n";
    return 0;
#endif
}
