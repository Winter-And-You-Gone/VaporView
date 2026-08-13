#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QTemporaryDir>

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

bool isElevationRequiredError(const QString& errorString)
{
    const QString lower = errorString.toLower();
    return lower.contains(QStringLiteral("elevation")) ||
           lower.contains(QStringLiteral("提升")) ||
           lower.contains(QStringLiteral("需要管理员")) ||
           lower.contains(QStringLiteral("requires elevation"));
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
    QString requiredTitle;
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
    const QString windowTitle = QString::fromWCharArray(title);
    if ((!state->requiredTitle.isEmpty() && windowTitle == state->requiredTitle) ||
        (state->requiredTitle.isEmpty() && windowTitle.contains(QStringLiteral("VaporView"))))
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

HWND findStartupSplashWindow(DWORD pid)
{
    WindowSearchState state;
    state.pid = pid;
    state.requiredTitle = QStringLiteral("Startup Splash");
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&state));
    return state.hwnd;
}

QString windowTitle(HWND hwnd)
{
    wchar_t title[256] = {};
    GetWindowTextW(hwnd, title, static_cast<int>(sizeof(title) / sizeof(title[0])));
    return QString::fromWCharArray(title);
}

bool isWindowOpaque(HWND hwnd)
{
    if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) == 0)
    {
        return true;
    }

    BYTE alpha = 255;
    DWORD flags = 0;
    if (!GetLayeredWindowAttributes(hwnd, nullptr, &alpha, &flags))
    {
        return true;
    }
    return (flags & LWA_ALPHA) == 0 || alpha > 0;
}
#endif

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QString exePath = QDir::toNativeSeparators(mainExecutablePath());
    require(QFileInfo::exists(exePath), QStringLiteral("main executable not found at %1").arg(exePath));

    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(),
            QStringLiteral("temporary settings directory created for startup smoke test"));

    QProcess process;
    process.setProgram(exePath);
    process.setWorkingDirectory(QDir::toNativeSeparators(QFileInfo(exePath).absolutePath()));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("VAPORVIEW_SETTINGS_DIR"), settingsDirectory.path());
    environment.insert(QStringLiteral("VAPORVIEW_CONFIG_FILE"),
                       QDir(settingsDirectory.path()).filePath(QStringLiteral("vaporview.ini")));
    process.setProcessEnvironment(environment);
#ifdef Q_OS_WIN
    process.setProcessChannelMode(QProcess::SeparateChannels);
#else
    process.setProcessChannelMode(QProcess::SeparateChannels);
#endif
    process.start();
    if (!process.waitForStarted(5000))
    {
        if (isElevationRequiredError(process.errorString()))
        {
            std::cerr << "SKIP: VaporView.exe requires elevation in this environment; "
                         "QProcess cannot launch elevated GUI applications.\n";
            return 77;
        }
        fail(QStringLiteral("failed to start main executable: %1\nprogram: %2\nworking directory: %3")
                 .arg(process.errorString(), process.program(), process.workingDirectory()),
             &process,
             readProcessOutput(process));
    }

    QString stdoutText;
    QString stderrText;

#ifdef Q_OS_WIN
    const DWORD pid = static_cast<DWORD>(process.processId());
    QElapsedTimer timer;
    timer.start();

    HWND splashHwnd = nullptr;
    while (timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        stdoutText += QString::fromLocal8Bit(process.readAllStandardOutput());
        stderrText += QString::fromLocal8Bit(process.readAllStandardError());

        if (process.state() == QProcess::NotRunning)
        {
            fail(QStringLiteral("VaporView exited before its startup splash appeared"),
                 &process,
                 stdoutText,
                 stderrText);
        }
        if (findMainWindow(pid) != nullptr)
        {
            fail(QStringLiteral("VaporView main window appeared before its startup splash"),
                 &process,
                 stdoutText,
                 stderrText);
        }

        splashHwnd = findStartupSplashWindow(pid);
        if (splashHwnd != nullptr)
        {
            break;
        }
        QThread::msleep(20);
    }

    require(splashHwnd != nullptr,
            QStringLiteral("VaporView startup splash did not become visible within 5 seconds"));
    require(IsWindowVisible(splashHwnd), QStringLiteral("startup splash is not visible"));
    require((GetWindowLongPtrW(splashHwnd, GWL_STYLE) & WS_CAPTION) == 0,
            QStringLiteral("startup splash unexpectedly has a system caption"));
    const LONG_PTR splashExStyle = GetWindowLongPtrW(splashHwnd, GWL_EXSTYLE);
    require((splashExStyle & WS_EX_TOOLWINDOW) != 0 || GetWindow(splashHwnd, GW_OWNER) != nullptr,
            QStringLiteral("startup splash can create a separate taskbar entry"));

    RECT splashRect = {};
    require(GetWindowRect(splashHwnd, &splashRect) != FALSE,
            QStringLiteral("failed to read startup splash geometry"));
    const int splashWidth = splashRect.right - splashRect.left;
    const int splashHeight = splashRect.bottom - splashRect.top;
    require(splashWidth > 0 && splashHeight > 0,
            QStringLiteral("startup splash geometry is empty"));
    const double splashRatio = static_cast<double>(splashWidth) / splashHeight;
    require(splashRatio > 0.94 && splashRatio < 1.06,
            QStringLiteral("startup splash aspect ratio is not square"));
    require(splashWidth >= 180 && splashWidth <= 340,
            QStringLiteral("startup splash is outside the compact app-icon size range"));

    timer.restart();

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
        if (hwnd != nullptr && isWindowOpaque(hwnd))
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
    require(isWindowOpaque(hwnd), QStringLiteral("main window is visible but fully transparent"));

    timer.restart();
    while (timer.elapsed() < 3000 && findStartupSplashWindow(pid) != nullptr)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(20);
    }
    require(findStartupSplashWindow(pid) == nullptr,
            QStringLiteral("startup splash remained visible after the main-window transition"));

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
