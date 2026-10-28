#include "test_settings_sandbox.h"
#include "ground/main/MainWindow.h"
#include "ground/motion/MotionControlPage.h"
#include "ground/session/GroundRecordingService.h"
#include "shared/config/SettingsWriteBarrier.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QLabel>
#include <QLibrary>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSpinBox>
#include <QThread>
#include <functional>
#include <iostream>

using namespace VaporView::Ground::Motion;
void check(bool ok, const char *message) { if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }
template<typename T> T *find(QWidget& root, const char *name)
{
    auto *result = root.findChild<T*>(QString::fromLatin1(name)); check(result != nullptr, name); return result;
}
void until(const std::function<bool()>& ready)
{
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 6000) { QApplication::processEvents(); QThread::msleep(10); }
    check(ready(), "UI asynchronous operation completes");
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv); check(argc == 2, "fake SDK directory supplied");
    qputenv("VAPORVIEW_EMFWC3_SDK_DIR", QByteArray(argv[1]));
    const auto directory = VaporView::Ground::Session::GroundRecordingService::defaultRecordingDirectory();
    const bool existed = QFileInfo::exists(directory);
    const auto cleanup = qScopeGuard([&] { if (!existed) check(QDir().rmdir(directory), "remove empty test data directory"); });
    QLibrary fake(QString::fromLocal8Bit(argv[1]) + QStringLiteral("/LB_Control_Library_x64")); check(fake.load(), qPrintable(fake.errorString()));
    const auto reset = reinterpret_cast<void(*)()>(fake.resolve("testReset"));
    const auto motions = reinterpret_cast<int(*)()>(fake.resolve("testMotionCount")); check(reset && motions, "test SDK symbols exist"); reset();
    {
        MainWindow window; window.resize(1280, 850); window.show();
        auto *page = find<MotionControlPage>(window, "motionControlPage");
        auto *panel = find<QWidget>(window, "emFwc3ConnectionPanel");
        auto *connect = find<QPushButton>(*panel, "emFwc3Connect");
        for (auto *action : panel->findChildren<QPushButton*>())
            check(action->focusPolicy() == Qt::TabFocus && !action->accessibleName().isEmpty(), "independent device actions follow keyboard-focus and accessibility conventions");
        auto *port = find<QComboBox>(*panel, "emFwc3Port"); port->setEditText(QStringLiteral("FAKE-COM"));
        for (int i = 1; i <= 3; ++i)
            check(panel->findChild<QComboBox*>(QStringLiteral("emFwc3MotorType%1").arg(i))->currentText() == QStringLiteral("CEM-FPM"), "new setup defaults to the confirmed CEM-FPM hardware");
        check(!page->session()->state().connected && !find<QPushButton>(*page, "emFwc3Go1")->isEnabled(), "startup never connects or enables motion");
        check(!connect->isEnabled(), "motor types must be checked before connecting");
        find<QCheckBox>(*panel, "emFwc3ConfirmTypes")->setChecked(true); check(connect->isEnabled(), "explicit model confirmation enables connection");
        connect->click(); until([&] { return page->session()->state().connected && !page->session()->state().busy; });
        check(motions() == 0, "connecting from configuration never causes motion");
        check(find<QSpinBox>(*page, "emFwc3Speed")->value() == 100, "speed editor starts from the actual device readback");
        auto *transport = find<QComboBox>(window, "deviceSensorTransportPathCombo");
        transport->setCurrentIndex(transport->findData(QStringLiteral("fpga_relay")));
        QApplication::processEvents();
        check(page->session()->state().connected && find<QPushButton>(*panel, "emFwc3Disconnect")->isEnabled(), "USB session stays available in FPGA mode");
        transport->setCurrentIndex(transport->findData(QStringLiteral("direct_devices")));
        QApplication::processEvents(); check(page->session()->state().connected, "returning to Direct preserves USB connection");
        find<QPushButton>(*panel, "emFwc3OpenControls")->click();
        check(find<QStackedWidget>(window, "mainPageStack")->currentWidget() == page, "configuration navigates to independent motion page");
        find<QDoubleSpinBox>(*page, "emFwc3Target3")->setValue(12.5);
        find<QPushButton>(*page, "emFwc3Go3")->click();
        until([&] { return find<QLabel>(*page, "emFwc3Position3")->text() == QStringLiteral("12.500°"); });
        check(motions() == 1, "selected channel UI command reaches SDK");
        const auto firstCard = find<QGroupBox>(*page, "emFwc3Channel1")->geometry();
        const auto secondCard = find<QGroupBox>(*page, "emFwc3Channel2")->geometry();
        check(firstCard.y() == secondCard.y(), "first opening in the main window uses settled viewport width for side-by-side channels");
        VaporView::setSettingsWritesSuspended(true);
        find<QPushButton>(*page, "emFwc3Go3")->click(); QApplication::processEvents();
        check(motions() == 1, "UI test write barrier also blocks hardware commands");
        VaporView::setSettingsWritesSuspended(false);
        find<QPushButton>(*page, "emFwc3ConnectionSettings")->click();
        check(find<QStackedWidget>(window, "mainPageStack")->currentWidget()->objectName() == QStringLiteral("deviceConfigPage"), "motion page navigates to connection settings");
        find<QPushButton>(*panel, "emFwc3Disconnect")->click(); until([&] { return !page->session()->state().connected && !page->session()->state().busy; });
        check(find<QLabel>(*page, "emFwc3Position3")->text() == QStringLiteral("—"), "disconnection clears position readbacks");
        window.close();
    }
    {
        MotionControlPage page(nullptr, QString::fromLocal8Bit(argv[1]));
        QWidget settings; page.createConnectionPanel(&settings);
        page.resize(1200, 1000); page.show(); QApplication::processEvents();
        for (bool dark : {false, true}) for (int scale : {100, 130}) {
            QFont font = app.font(); font.setPointSizeF(10 * scale / 100.0); page.setFont(font);
            page.setTheme(dark, scale); page.setLanguage(true);
            for (int width : {1200, 600}) {
                page.resize(width, 1000); QApplication::processEvents();
                const auto a = find<QGroupBox>(page, "emFwc3Channel1")->geometry();
                const auto b = find<QGroupBox>(page, "emFwc3Channel2")->geometry();
                const auto c = find<QGroupBox>(page, "emFwc3Channel3")->geometry();
                check(!a.intersects(b) && !b.intersects(c) && !a.intersects(c), "channel cards reflow without overlap at both font scales");
                for (int i = 1; i <= 3; ++i) {
                    const auto cardName = QStringLiteral("emFwc3Channel%1").arg(i);
                    auto *card = page.findChild<QGroupBox*>(cardName);
                    for (auto *button : card->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly))
                        check(card->rect().contains(button->geometry()), "channel action remains inside its card");
                }
            }
        }
        page.setLanguage(false); page.setTheme(false, 100); page.resize(1200, 1000);
        QApplication::processEvents();
        page.close();
    }
    std::cout << "PASS: independent motion UI, FPGA switching, commands and responsive layout\n";
}
