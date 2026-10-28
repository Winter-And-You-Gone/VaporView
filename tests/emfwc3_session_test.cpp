#include "ground/motion/EmFwc3Session.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLibrary>
#include <QThread>
#include <QTemporaryDir>
#include <functional>
#include <iostream>
#include <limits>

using namespace VaporView::Ground::Motion;
void check(bool ok, const char *message) { if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }
void until(const std::function<bool()>& ready)
{
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 6000) { QCoreApplication::processEvents(); QThread::msleep(5); }
    check(ready(), "asynchronous operation completed within deadline");
}
template<typename T> T fn(QLibrary& lib, const char *name)
{
    const auto result = reinterpret_cast<T>(lib.resolve(name)); check(result != nullptr, name); return result;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Explicit bench mode uses the production worker without issuing motion commands.
    // CTest only invokes the fake-SDK branch below.
    if (argc == 5 && QByteArray(argv[1]) == "--hardware-readonly") {
        EmFwc3Session session(nullptr, QString::fromLocal8Bit(argv[2])); bool finished = false; int result = -100;
        QObject::connect(&session, &EmFwc3Session::operationFinished, [&](const QString& operation, int code, const QString& detail) {
            std::cout << operation.toStdString() << ": " << code << ' ' << detail.toStdString() << std::endl;
            result = code; finished = true;
        });
        const auto type = QString::fromLocal8Bit(argv[4]);
        const auto waitForResult = [&] {
            QElapsedTimer timer; timer.start();
            while (!finished && timer.elapsed() < 15000) { QCoreApplication::processEvents(); QThread::msleep(10); }
            return finished;
        };
        session.connectDevice(QString::fromLocal8Bit(argv[3]), {type, type, type});
        if (!waitForResult() || result != 0 || !session.state().connected) return 1;
        const auto state = session.state();
        std::cout << "Speed: " << state.speed << std::endl;
        for (int i = 0; i < 3; ++i) std::cout << "Channel " << i + 1 << ": " << state.positions[i] << " deg; valid=" << state.valid[i] << std::endl;
        finished = false; session.disconnectDevice();
        if (!waitForResult()) return 1;
        session.shutdown();
        return result == 0 ? 0 : 1;
    }
    check(argc == 2, "fake SDK directory supplied");
    const QString directory = QString::fromLocal8Bit(argv[1]);
    QLibrary fake(directory + QStringLiteral("/LB_Control_Library_x64")); check(fake.load(), qPrintable(fake.errorString()));
    const auto reset = fn<void(*)()>(fake, "testReset");
    const auto motions = fn<int(*)()>(fake, "testMotionCount");
    const auto saves = fn<int(*)()>(fake, "testSaves");
    const auto closes = fn<int(*)()>(fake, "testCloses");
    const auto deinits = fn<int(*)()>(fake, "testDeinits");
    const auto stops = fn<int(*)(int)>(fake, "testStops");
    const auto failRead = fn<void(*)(int)>(fake, "testFailRead");
    const auto failCommand = fn<void(*)(int)>(fake, "testFailCommand");
    const auto nan = fn<void(*)(int)>(fake, "testNonfinite");
    const QStringList types{"EM-FPM", "EM-FPM", "EM-FPM"};
    using Command = EmFwc3Session::Command;
    reset();
    {
        EmFwc3Session session(nullptr, directory); int finished = 0, lastCode = 0;
        QObject::connect(&session, &EmFwc3Session::operationFinished, [&](const QString&, int code, const QString&) { ++finished; lastCode = code; });
        session.connectDevice(QStringLiteral("TEST-COM"), types);
        until([&] { return finished == 1; });
        check(session.state().connected && session.state().valid[2] && session.state().positions[2] == 90, "connection validates all three position readbacks");
        check(motions() == 0 && saves() == 0, "connecting never homes, moves, or writes EEPROM");
        session.execute(Command::Absolute, 2, 12.5); until([&] { return finished == 2; });
        until([&] { return session.state().positions[2] == 12.5; });
        check(session.state().positions[1] == 45, "one-byte channel index routes to only the selected axis");
        session.execute(Command::Relative, 2, -2.5); until([&] { return finished == 3; });
        until([&] { return session.state().positions[2] == 10; });
        session.execute(Command::Speed, 0, 75); until([&] { return finished == 4; });
        check(lastCode == 0 && session.state().speed == 75, "device-wide speed is read back after setting");
        session.execute(Command::Absolute, -1, 20); until([&] { return finished == 5; });
        check(lastCode == -1000, "invalid channel rejected");
        session.execute(Command::Relative, 0, std::numeric_limits<double>::infinity()); until([&] { return finished == 6; });
        check(lastCode == -1000 && motions() == 2, "nonfinite input never reaches SDK");
        failCommand(-2);
        session.execute(Command::Absolute, 0, 20); until([&] { return finished == 7; });
        check(lastCode == -2 && motions() == 2, "SDK failure is reported without a false motion success");
        session.execute(Command::StopAll); until([&] { return finished == 8; });
        check(stops(0) == 1 && stops(1) == 1 && stops(2) == 1, "stop-all attempts every axis even when earlier stops fail");
        failCommand(0);
        session.execute(Command::SavePosition); until([&] { return finished == 9; });
        check(saves() == 1, "EEPROM write requires explicit command");
        nan(1); until([&] { return !session.state().valid[0]; });
        check(!session.state().valid[1] && !session.state().valid[2], "nonfinite device positions never display as valid");
        nan(0); until([&] { return session.state().valid[0]; });
        failRead(-2); until([&] { return !session.state().connected; });
        check(closes() == 1 && deinits() == 1 && !session.state().valid[2], "lost device invalidates readings and releases the SDK");
        session.shutdown();
    }
    reset(); failRead(-2);
    {
        EmFwc3Session session(nullptr, directory); bool done = false;
        QObject::connect(&session, &EmFwc3Session::operationFinished, [&](const QString&, int, const QString&) { done = true; });
        session.connectDevice(QStringLiteral("WRONG-COM"), types); until([&] { return done; });
        check(!session.state().connected && closes() == 1 && deinits() == 1, "open serial handle with failed handshake is not a connected device");
        check(motions() == 0 && stops(0) == 0, "failed/read-only connection never sends motion or stop commands");
    }
    reset();
    {
        EmFwc3Session session(nullptr, directory); bool done = false;
        QObject::connect(&session, &EmFwc3Session::operationFinished, [&](const QString&, int, const QString&) { done = true; });
        session.connectDevice(QStringLiteral("TEST-COM"), types); until([&] { return done; });
        session.execute(Command::Continuous, 1, 1); until([&] { return !session.state().busy; });
        session.shutdown();
        check(stops(1) == 1 && stops(0) == 0 && stops(2) == 0 && closes() == 1, "shutdown stops the owned moving axis before closing");
    }
    {
        QTemporaryDir empty; EmFwc3Session missing(nullptr, empty.path()); bool done = false; QString detail;
        QObject::connect(&missing, &EmFwc3Session::operationFinished, [&](const QString&, int, const QString& error) { done = true; detail = error; });
        missing.connectDevice(QStringLiteral("TEST-COM"), types); until([&] { return done; });
        check(!missing.state().connected && !detail.isEmpty(), "missing optional SDK fails with an actionable loader diagnostic");
    }
    std::cout << "PASS: EM-FWC3 C ABI, commands, failures, polling and cleanup\n";
}
