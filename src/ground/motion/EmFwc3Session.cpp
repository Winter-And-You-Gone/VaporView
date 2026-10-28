#include "EmFwc3Session.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>

namespace VaporView::Ground::Motion
{
namespace
{
constexpr int invalidArgument = -1000;
constexpr int unavailable = -100;

// Bind the C ABI directly. In particular motorNum is char, not int.
struct Sdk
{
    QLibrary usb, core, library;
    int (*init)() = nullptr;
    void (*deinit)() = nullptr;
    int (*open)(const char*, const char*) = nullptr;
    void (*close)(const char*) = nullptr;
    int (*types)(const char*, int, const char**) = nullptr;
    int (*position)(const char*, double*, char) = nullptr;
    int (*relative)(const char*, double, char) = nullptr;
    int (*absolute)(const char*, double, char) = nullptr;
    int (*home)(const char*, char) = nullptr;
    int (*continuous)(const char*, int, char) = nullptr;
    int (*setSpeed)(const char*, int) = nullptr;
    int (*getSpeed)(const char*, int*) = nullptr;
    int (*save)(const char*) = nullptr;
    QString error;

    template<typename T> bool bind(T& fn, const char *name)
    {
        fn = reinterpret_cast<T>(library.resolve(name));
        if (!fn) error = QString::fromLatin1(name) + QStringLiteral(": ") + library.errorString();
        return fn != nullptr;
    }
    bool load(const QString& directory)
    {
        if (library.isLoaded()) return true;
        const QDir dir(directory);
#ifdef Q_OS_WIN
        // Load dependencies by absolute path; never change the process-wide DLL directory.
        usb.setFileName(dir.filePath(QStringLiteral("libusb-1.0.dll")));
        core.setFileName(dir.filePath(QStringLiteral("lubonCore.dll")));
        if (!usb.load()) { error = usb.errorString(); return false; }
        if (!core.load()) { error = core.errorString(); return false; }
#endif
        library.setFileName(dir.filePath(QStringLiteral("LB_Control_Library_x64")));
        if (!library.load()) { error = library.errorString(); return false; }
        const bool ok = bind(init, "libraryInit") && bind(deinit, "libraryDeInit") &&
            bind(open, "COMMON_openSerialDevice") && bind(close, "COMMON_closeDevice") &&
            bind(types, "EMFWC3_SetMotorType") && bind(position, "EMFWC3_GetPosition") &&
            bind(relative, "EMFWC3_Run") && bind(absolute, "EMFWC3_RunPosition") &&
            bind(home, "EMFWC3_Reset") && bind(continuous, "EMFWC3_RunContinue") &&
            bind(setSpeed, "EMFWC3_SetSpeed") && bind(getSpeed, "EMFWC3_GetSpeed") &&
            bind(save, "EMFWC3_SavePosition");
        if (!ok) library.unload();
        return ok;
    }
};
}

class EmFwc3Worker final : public QObject
{
    Q_OBJECT
public:
    explicit EmFwc3Worker(QString directory) : directory_(std::move(directory)) {}
    void open(const QString& port, const QStringList& motorTypes)
    {
        if (state_.connected) return;
        state_.busy = true; emit changed(state_);
        int code = 0;
        QString detail;
        if (port.trimmed().isEmpty() || motorTypes.size() != 3 ||
            !std::all_of(motorTypes.cbegin(), motorTypes.cend(), [](const QString& t) {
                return t == QStringLiteral("EM-FPM") || t == QStringLiteral("CEM-FPM");
            })) code = invalidArgument;
        else if (!sdk_.load(directory_)) { code = unavailable; detail = sdk_.error; }
        else {
            code = sdk_.init();
            initialized_ = code == 0;
            if (code == 0) {
                code = sdk_.open(name_, port.trimmed().toUtf8().constData());
                opened_ = code == 0;
            }
            if (code == 0) {
                const std::array<QByteArray, 3> bytes{motorTypes[0].toLatin1(), motorTypes[1].toLatin1(), motorTypes[2].toLatin1()};
                const char *types[]{bytes[0].constData(), bytes[1].constData(), bytes[2].constData()};
                code = sdk_.types(name_, 3, types);
            }
            // An open COM handle alone does not establish device identity/health.
            if (code == 0) code = readPositions();
            if (code == 0) {
                code = sdk_.getSpeed(name_, &state_.speed);
                state_.speedValid = code == 0;
            }
        }
        if (code != 0) release(false);
        else {
            state_.connected = true;
            if (!timer_) {
                timer_ = new QTimer(this);
                timer_->setInterval(500);
                connect(timer_, &QTimer::timeout, this, &EmFwc3Worker::poll);
            }
            failures_ = 0;
            timer_->start();
        }
        state_.busy = false;
        emit changed(state_);
        emit finished(QStringLiteral("connect"), code, detail);
    }
    void close()
    {
        const int code = release(true);
        emit changed(state_);
        emit finished(QStringLiteral("disconnect"), code, {});
    }
    void command(EmFwc3Session::Command command, int channel, double value)
    {
        using Command = EmFwc3Session::Command;
        const auto op = [command] {
            switch (command) {
            case Command::Relative: return QStringLiteral("relative");
            case Command::Absolute: return QStringLiteral("absolute");
            case Command::Home: return QStringLiteral("home");
            case Command::Continuous: return QStringLiteral("continuous");
            case Command::StopAll: return QStringLiteral("stop_all");
            case Command::Speed: return QStringLiteral("speed");
            case Command::SavePosition: return QStringLiteral("save_position");
            }
            return QString();
        }();
        int code = invalidArgument;
        if (state_.connected && channel >= 0 && channel < 3 && std::isfinite(value)) {
            const auto motor = static_cast<char>(channel);
            // A lost acknowledgement can still mean that the device started moving.
            if (command == Command::Relative || command == Command::Absolute || command == Command::Home ||
                (command == Command::Continuous && (value == -1 || value == 1))) motionIssued_[channel] = true;
            switch (command) {
            case Command::Relative: code = sdk_.relative(name_, value, motor); break;
            case Command::Absolute: code = sdk_.absolute(name_, value, motor); break;
            case Command::Home: code = sdk_.home(name_, motor); break;
            case Command::Continuous:
                if (value == -1 || value == 0 || value == 1) code = sdk_.continuous(name_, int(value), motor);
                break;
            case Command::StopAll:
                code = stop(true); break;
            case Command::Speed:
                if (value > 0 && value <= std::numeric_limits<int>::max() && std::floor(value) == value) {
                    code = sdk_.setSpeed(name_, int(value));
                    if (code == 0) {
                        code = sdk_.getSpeed(name_, &state_.speed);
                        state_.speedValid = code == 0;
                        if (code == 0 && state_.speed != int(value)) code = unavailable;
                    }
                }
                break;
            case Command::SavePosition: code = sdk_.save(name_); break;
            }
            if (code == 0) {
                if (command == Command::Continuous && value == 0) motionIssued_[channel] = false;
            }
        }
        state_.busy = false;
        emit changed(state_);
        emit finished(op, code, {});
    }
signals:
    void changed(const VaporView::Ground::Motion::EmFwc3State& state);
    void finished(const QString& operation, int code, const QString& detail);
private:
    int readPositions()
    {
        int result = 0;
        for (int i = 0; i < 3; ++i) {
            double value = 0;
            const int code = sdk_.position(name_, &value, static_cast<char>(i));
            state_.valid[i] = code == 0 && std::isfinite(value);
            if (state_.valid[i]) state_.positions[i] = value;
            else if (result == 0) result = code == 0 ? unavailable : code;
        }
        return result;
    }
    void poll()
    {
        const int code = readPositions();
        if (code == 0) failures_ = 0;
        else if (++failures_ >= 3) {
            release(true);
            emit finished(QStringLiteral("connection_lost"), code, {});
        }
        emit changed(state_);
    }
    int stop(bool all)
    {
        int result = 0;
        for (int i = 0; i < 3; ++i) {
            if (!all && !motionIssued_[i]) continue;
            const int code = sdk_.continuous(name_, 0, static_cast<char>(i));
            if (code == 0) motionIssued_[i] = false;
            else if (result == 0) result = code;
        }
        return result;
    }
    int release(bool stopMotion)
    {
        if (timer_) timer_->stop();
        const int result = opened_ && stopMotion ? stop(false) : 0;
        if (opened_) sdk_.close(name_);
        if (initialized_) sdk_.deinit();
        opened_ = initialized_ = false;
        motionIssued_.fill(false);
        state_ = {};
        return result;
    }
    static constexpr const char *name_ = "EM-FWC3";
    QString directory_;
    Sdk sdk_;
    EmFwc3State state_;
    QTimer *timer_ = nullptr;
    std::array<bool, 3> motionIssued_{};
    bool initialized_ = false, opened_ = false;
    int failures_ = 0;
};

EmFwc3Session::EmFwc3Session(QObject *parent, const QString& sdkDirectory) : QObject(parent)
{
    qRegisterMetaType<EmFwc3State>();
    QString directory = sdkDirectory;
    if (directory.isEmpty()) directory = qEnvironmentVariable("VAPORVIEW_EMFWC3_SDK_DIR");
    if (directory.isEmpty()) directory = QCoreApplication::applicationDirPath() + QStringLiteral("/emfwc3");
    worker_ = new EmFwc3Worker(directory);
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &EmFwc3Worker::changed, this, [this](const EmFwc3State& state) {
        state_ = state; state_.busy = pending_ > 0; emit stateChanged(state_);
    });
    connect(worker_, &EmFwc3Worker::finished, this, [this](const QString& operation, int code, const QString& detail) {
        if (operation != QStringLiteral("connection_lost") && pending_ > 0) --pending_;
        state_.busy = pending_ > 0;
        emit stateChanged(state_);
        emit operationFinished(operation, code, detail);
    });
    thread_.start();
}
EmFwc3Session::~EmFwc3Session() { shutdown(); }
void EmFwc3Session::connectDevice(const QString& port, const QStringList& types)
{
    if (closing_ || state_.busy || state_.connected) return;
    ++pending_; state_.busy = true; emit stateChanged(state_);
    QMetaObject::invokeMethod(worker_, [this, port, types] { worker_->open(port, types); }, Qt::QueuedConnection);
}
void EmFwc3Session::disconnectDevice()
{
    if (closing_ || state_.busy || !state_.connected) return;
    ++pending_; state_.busy = true; emit stateChanged(state_);
    QMetaObject::invokeMethod(worker_, [this] { worker_->close(); }, Qt::QueuedConnection);
}
void EmFwc3Session::execute(Command command, int channel, double value)
{
    const bool stop = command == Command::StopAll || (command == Command::Continuous && value == 0);
    if (closing_ || !state_.connected || (state_.busy && !stop)) return;
    ++pending_; state_.busy = true; emit stateChanged(state_);
    QMetaObject::invokeMethod(worker_, [this, command, channel, value] { worker_->command(command, channel, value); }, Qt::QueuedConnection);
}
void EmFwc3Session::shutdown()
{
    if (closing_) return;
    closing_ = true;
    QMetaObject::invokeMethod(worker_, [this] { worker_->close(); }, Qt::BlockingQueuedConnection);
    thread_.quit();
    thread_.wait();
}
}
#include "EmFwc3Session.moc"
