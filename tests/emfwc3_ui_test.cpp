#include "test_settings_sandbox.h"
#include "ground/main/MainWindow.h"
#include "ground/motion/HomeMotionPanel.h"
#include "ground/motion/MotionControlPage.h"
#include "shared/config/SettingsWriteBarrier.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QLibrary>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QSettings>
#include <QVBoxLayout>
#include <atomic>
#include <functional>
#include <iostream>

using namespace VaporView::Ground::Motion;
std::atomic<int> titleBarSizeWarnings{0};
QtMessageHandler previousMessageHandler = nullptr;
void captureTitleBarSizeWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtWarningMsg && message.contains(QStringLiteral("QWidget::setMaximumSize")) &&
        message.contains(QStringLiteral("sectionTitleBar")))
        ++titleBarSizeWarnings;
    if (previousMessageHandler) previousMessageHandler(type, context, message);
}
void check(bool ok, const char *message) { if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); } }
template<typename T> T *find(QWidget& root, const char *name)
{
    auto *result = root.findChild<T*>(QString::fromLatin1(name)); check(result != nullptr, name); return result;
}
void until(const char *message, const std::function<bool()>& ready)
{
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 6000) { QApplication::processEvents(); QThread::msleep(10); }
    check(ready(), message);
}
void checkCardGeometry(QWidget& scope)
{
    auto frames = scope.findChildren<QFrame*>();
    if (auto *frame = qobject_cast<QFrame*>(&scope)) frames.prepend(frame);
    int count = 0;
    for (auto *card : frames) {
        if (!card->property("emFwc3Card").toBool()) continue;
        ++count;
        auto *header = card->findChild<QWidget*>(QStringLiteral("sectionTitleBar"), Qt::FindDirectChildrenOnly);
        auto *body = card->findChild<QWidget*>(QStringLiteral("emFwc3CardBody"), Qt::FindDirectChildrenOnly);
        check(header && body, "card title and controls have separate layout regions");
        auto *title = header->findChild<QLabel*>(QStringLiteral("sectionTitleLabel"));
        check(title && !title->text().isEmpty(), "card has a visible internal title");
        const QRect titleRect(title->mapTo(card, QPoint(0, 0)), title->size());
        check(card->rect().adjusted(2, 2, -2, -2).contains(titleRect), "title stays inside the card, clear of its outer border");
        check(title->contentsRect().height() >= title->fontMetrics().height(), "card title is not vertically clipped at larger fonts");
        check(!header->geometry().intersects(body->geometry()), "title bar and card content never overlap");
        for (auto *action : header->findChildren<QPushButton*>())
            check(action->width() >= action->fontMetrics().horizontalAdvance(action->text()) + 12,
                  "connection actions retain enough width for their complete labels");
        const auto controls = body->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (int i = 0; i < controls.size(); ++i) {
            if (!controls[i]->isVisibleTo(body)) continue;
            check(body->rect().contains(controls[i]->geometry()), "card controls stay inside their content region");
            for (int j = i + 1; j < controls.size(); ++j)
                if (controls[j]->isVisibleTo(body)) check(!controls[i]->geometry().intersects(controls[j]->geometry()), "adjacent card labels and controls do not overlap");
        }
    }
    check(count > 0, "scope contains motion-control cards");
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv); check(argc == 2, "fake SDK directory supplied");
    previousMessageHandler = qInstallMessageHandler(captureTitleBarSizeWarnings);
    qputenv("VAPORVIEW_EMFWC3_SDK_DIR", QByteArray(argv[1]));
    // Own the directory: this motion-only test never starts a recording and must
    // not depend on, or remove, the repository's shared default data directory.
    QTemporaryDir recordingDirectory;
    check(recordingDirectory.isValid(), "create isolated test recording directory");
    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        settings.setValue(QStringLiteral("recording_directory"), recordingDirectory.path());
        settings.setValue(QStringLiteral("font_scale_percent"), 100);
    }
    const auto cleanup = qScopeGuard([&] {
        // rmdir deliberately rejects unexpected files and unreleased handles.
        const bool removed = QDir().rmdir(recordingDirectory.path());
        if (!removed)
            std::cerr << "Recording directory cleanup failed: "
                      << recordingDirectory.path().toStdString() << '\n';
        check(removed, "remove empty test data directory");
        check(!QFileInfo::exists(recordingDirectory.path()), "test recording directory is removed");
    });
    QLibrary fake(QString::fromLocal8Bit(argv[1]) + QStringLiteral("/LB_Control_Library_x64")); check(fake.load(), qPrintable(fake.errorString()));
    const auto reset = reinterpret_cast<void(*)()>(fake.resolve("testReset"));
    const auto motions = reinterpret_cast<int(*)()>(fake.resolve("testMotionCount")); check(reset && motions, "test SDK symbols exist"); reset();
    const auto cemFpmCount = reinterpret_cast<int(*)()>(fake.resolve("testCemFpmCount")); check(cemFpmCount, "motor type counter exists");
    {
        QSettings settings(QStringLiteral("VaporView"), QStringLiteral("EmFwc3"));
        for (int i = 0; i < 3; ++i) settings.setValue(QStringLiteral("motor_type_%1").arg(i), QStringLiteral("EM-FPM"));
    }
    {
        MainWindow window; window.resize(1280, 850); window.show();
        auto *page = find<MotionControlPage>(window, "emFwc3ConnectionPanel");
        auto *homeMotion = find<HomeMotionPanel>(window, "homeMotionPanel");
        auto *homeStop = find<QPushButton>(*homeMotion, "homeMotionStopAll");
        auto *homeConnection = find<QLabel>(window, "homeMotionConnectionState");
        auto *homeSelector = find<QComboBox>(window, "homeMotionChannelCombo");
        auto *homePosition = find<QLabel>(*homeMotion, "homeMotionPosition");
        auto *homeTarget = find<QDoubleSpinBox>(*homeMotion, "homeMotionTarget");
        auto *homeGo = find<QPushButton>(*homeMotion, "homeMotionGo");
        auto *configMinus = find<QPushButton>(*page, "emFwc3Minus2");
        auto *configPlus = find<QPushButton>(*page, "emFwc3Plus2");
        auto *homeCard = find<QWidget>(window, "homeMotionCard");
        auto *homeHeader = homeCard->findChild<QWidget *>(QStringLiteral("sectionTitleBar"), Qt::FindDirectChildrenOnly);
        auto *homePort = find<QComboBox>(*homeCard, "homeMotionPort");
        auto *homeAction = find<QToolButton>(*homeCard, "homeMotionActionButton");
        auto *overview = find<QWidget>(window, "homeOverviewDeviceGrid");
        QToolButton *overviewMotionAction = nullptr;
        for (auto *button : overview->findChildren<QToolButton *>(QStringLiteral("homeDeviceActionButton")))
            if (button->property("motionDeviceAction").toBool()) overviewMotionAction = button;
        check(overviewMotionAction, "device overview includes the local motor action");
        QLabel *overviewMotionStatus = nullptr;
        for (auto *label : overview->findChildren<QLabel *>(QStringLiteral("homeDeviceStatusCapsule")))
            if (label->text() == QStringLiteral("EM-FWC3电机")) overviewMotionStatus = label;
        check(overviewMotionStatus, "device overview includes EM-FWC3 status");
        auto *homeHome = find<QPushButton>(*homeMotion, "homeMotionHome");
        auto *configForward = find<QPushButton>(*page, "emFwc3Forward1");
        auto *configReverse = find<QPushButton>(*page, "emFwc3Reverse1");
        check(homeHeader && homeHeader->isAncestorOf(homePort) && homeHeader->isAncestorOf(homeAction) &&
              homeHeader->isAncestorOf(homeConnection), "home serial, combined connection action and status belong to the title bar");
        check(!homeCard->findChild<QToolButton *>(QStringLiteral("homeMotionConnect")) &&
              !homeCard->findChild<QToolButton *>(QStringLiteral("homeMotionDisconnect")),
              "home card exposes only one connection action");
        auto *ai8Port = find<QComboBox>(window, "ai8TitlePortCombo");
        check(!homePort->isEditable() && homePort->property("usesSingleLevelPopupMenu").toBool() &&
              homePort->font().weight() == ai8Port->font().weight() && homePort->height() == ai8Port->height(),
              "home serial selector reuses the AI-8 title dropdown presentation");
        for (auto *button : {homeAction, overviewMotionAction})
            check(button->text().isEmpty() && !button->icon().isNull() && !button->toolTip().isEmpty() &&
                  !button->accessibleName().isEmpty(), "home connection actions use labeled accessible icons without visible text");
        for (const char *removed : {"homeMotionForward", "homeMotionReverse", "homeMotionStep", "homeMotionMinus", "homeMotionPlus"})
            check(!homeMotion->findChild<QWidget *>(QString::fromLatin1(removed)), "home card omits continuous and step controls");
        check(homeHeader && homeHeader->isAncestorOf(homeSelector) && homeSelector->isHidden(),
              "home channel selector stays hidden without removing its routing code");
        check(homeSelector->count() == 3 && homeSelector->currentIndex() == 0,
              "home controls default to channel one and retain internal multi-channel support");
        check(!homeStop->isEnabled(), "home stop is disabled while disconnected");
        check(!homeGo->isEnabled() && !configMinus->isEnabled() && !configPlus->isEnabled(),
              "home motion actions are disabled while disconnected");
        check(homePosition->text() == QStringLiteral("—"), "home position is empty while disconnected");
        int configurationRequests = 0;
        QObject::connect(homeMotion, &HomeMotionPanel::openConfigurationRequested, &window, [&] { ++configurationRequests; });
        find<QPushButton>(*homeMotion, "homeMotionConfigure")->click();
        check(configurationRequests == 1, "home configuration action emits one navigation request");
        auto *pages = find<QStackedWidget>(window, "mainPageStack");
        auto *configuration = find<QWidget>(window, "deviceConfigPage");
        check(pages->currentWidget() == configuration, "home configuration action opens device configuration");
        auto *panel = find<QWidget>(window, "emFwc3ConnectionPanel");
        auto *connect = find<QPushButton>(*panel, "emFwc3Connect");
        for (auto *action : panel->findChildren<QPushButton*>())
            check(action->focusPolicy() == Qt::TabFocus && !action->accessibleName().isEmpty(), "independent device actions follow keyboard-focus and accessibility conventions");
        auto *port = find<QComboBox>(*panel, "emFwc3Port");
        port->addItem(QStringLiteral("FAKE-COM")); port->setEditText(QStringLiteral("FAKE-COM"));
        check(homePort->currentText() == port->currentText(), "configuration port selection updates the home card");
        homePort->setCurrentIndex(0);
        check(!homeAction->isEnabled() && !overviewMotionAction->isEnabled() && !connect->isEnabled(),
              "empty home COM selection disables all connect actions");
        homePort->setCurrentIndex(homePort->findData(QStringLiteral("FAKE-COM")));
        check(port->currentText() == QStringLiteral("FAKE-COM") && homeAction->isEnabled(),
              "home COM selection updates configuration and enables connection");
        for (int i = 1; i <= 3; ++i)
            check(panel->findChild<QLabel*>(QStringLiteral("emFwc3MotorType%1").arg(i))->text() == QStringLiteral("CEM-FPM") &&
                  !panel->findChild<QComboBox*>(QStringLiteral("emFwc3MotorType%1").arg(i)),
                  "motor model is fixed to CEM-FPM despite legacy EM-FPM settings");
        check(!page->session()->state().connected && !find<QPushButton>(*page, "emFwc3Go1")->isEnabled(), "startup never connects or enables motion");
        check(!panel->findChild<QCheckBox*>(QStringLiteral("emFwc3ConfirmTypes")) && connect->isEnabled(),
              "connection needs only a COM port with no model-confirmation checkbox");
        homeAction->click();
        check(homeAction->property("state") == QStringLiteral("connecting") &&
              overviewMotionAction->property("state") == QStringLiteral("connecting") && !homeAction->isEnabled(),
              "both motor connection icons enter the shared transition");
        QElapsedTimer transitionTimer; transitionTimer.start();
        const QRect actionGeometry = homeAction->geometry();
        const QRect overviewGeometry = overviewMotionAction->geometry();
        while (transitionTimer.elapsed() < 150) { QApplication::processEvents(); QThread::msleep(10); }
        check(homeAction->property("state") == QStringLiteral("connecting") && !homeAction->isEnabled() &&
              homeAction->geometry() == actionGeometry && overviewMotionAction->geometry() == overviewGeometry,
              "fast SDK responses retain a visible transition without moving either action");
        until("connect completes", [&] { return page->session()->state().connected && !page->session()->state().busy && homeAction->isEnabled(); });
        check(homeAction->property("state") == QStringLiteral("connected") && overviewMotionAction->isEnabled() && !homePort->isEnabled(),
              "home connection uses the same session and locks port editing");
        check(overviewMotionStatus->property("connected").toBool(), "overview follows the same connected USB session");
        pages->setCurrentIndex(0); QApplication::processEvents();
        const auto homeBounds = [homeCard](QWidget *widget) {
            return QRect(widget->mapTo(homeCard, QPoint()), widget->size());
        };
        const auto sameHomeRow = [&](QWidget *left, QWidget *right) {
            const QRect a = homeBounds(left), b = homeBounds(right);
            return a.top() <= b.bottom() && b.top() <= a.bottom() && !a.intersects(b);
        };
        check(sameHomeRow(homePosition, homeTarget) && sameHomeRow(homeTarget, homeGo),
              "current angle, target angle and positioning share a row");
        auto *homeConfigure = find<QPushButton>(*homeMotion, "homeMotionConfigure");
        for (auto *button : {homeHome, homeStop, homeConfigure}) {
            check(homeCard->rect().contains(homeBounds(button)), "motion action fits within the home card");
            check(sameHomeRow(homeGo, button), "all remaining home motion controls share one row");
        }
        auto *homeTitle = find<QLabel>(*homeHeader, "sectionTitleLabel");
        check(homeBounds(homeTitle).right() < homeBounds(homePort).left() &&
              homeBounds(homePort).right() < homeBounds(homeAction).left() &&
              homeBounds(homeAction).right() < homeBounds(homeConnection).left() &&
              homeConnection->alignment().testFlag(Qt::AlignRight),
              "title, COM port and action align left with status on the right");
        check(cemFpmCount() == 3, "SDK always receives fixed CEM-FPM types while retaining its three-channel interface");
        check(homeStop->isEnabled() && homeConnection->text().contains(QStringLiteral("已连接")),
              "home panel follows the device page's connected session");
        check(motions() == 0, "connecting from configuration never causes motion");
        check(find<QSpinBox>(*page, "emFwc3Speed")->value() == 100, "speed editor starts from the actual device readback");
        auto *transport = find<QComboBox>(window, "deviceSensorTransportPathCombo");
        transport->setCurrentIndex(transport->findData(QStringLiteral("fpga_relay")));
        QApplication::processEvents();
        check(page->session()->state().connected && find<QPushButton>(*panel, "emFwc3Disconnect")->isEnabled(), "USB session stays available in FPGA mode");
        transport->setCurrentIndex(transport->findData(QStringLiteral("direct_devices")));
        QApplication::processEvents(); check(page->session()->state().connected, "returning to Direct preserves USB connection");
        pages->setCurrentWidget(configuration); QApplication::processEvents();
        check(configuration->isAncestorOf(panel) && pages->indexOf(page) == -1 && pages->count() == 6,
              "motion controls belong to device configuration without a separate navigation page");
        auto *header = panel->findChild<QWidget*>(QStringLiteral("sectionTitleBar"));
        check(header->isAncestorOf(port) && header->isAncestorOf(connect) &&
              header->isAncestorOf(find<QPushButton>(*panel, "emFwc3Disconnect")), "serial and connection actions are in the card title bar");
        auto *selector = find<QComboBox>(*page, "emFwc3ChannelSelector");
        check(selector->isHidden() && selector->currentIndex() == 0 && selector->count() == 3,
              "configuration hides channel selection and defaults to channel one");
        for (int i = 1; i <= 3; ++i) {
            selector->setCurrentIndex(i - 1); QApplication::processEvents();
            for (int j = 1; j <= 3; ++j)
                check(page->findChild<QWidget*>(QStringLiteral("emFwc3Channel%1").arg(j))->isVisibleTo(page) == (i == j), "only the selected channel is visible");
            auto *target = page->findChild<QDoubleSpinBox*>(QStringLiteral("emFwc3Target%1").arg(i));
            auto *step = page->findChild<QDoubleSpinBox*>(QStringLiteral("emFwc3Step%1").arg(i));
            auto *position = page->findChild<QLabel*>(QStringLiteral("emFwc3Position%1").arg(i));
            target->setValue(i * 10.0); step->setValue(i * 0.5);
            page->findChild<QPushButton*>(QStringLiteral("emFwc3Go%1").arg(i))->click();
            until("absolute position reaches selected channel", [&] { return position->text() == QString::number(i * 10.0, 'f', 3) + QStringLiteral("°") && !page->session()->state().busy; });
            page->findChild<QPushButton*>(QStringLiteral("emFwc3Plus%1").arg(i))->click();
            until("positive step reaches selected channel", [&] { return position->text() == QString::number(i * 10.5, 'f', 3) + QStringLiteral("°") && !page->session()->state().busy; });
            page->findChild<QPushButton*>(QStringLiteral("emFwc3Minus%1").arg(i))->click();
            until("negative step reaches selected channel", [&] { return position->text() == QString::number(i * 10.0, 'f', 3) + QStringLiteral("°") && !page->session()->state().busy; });
            const int motionCountBeforeSelection = motions();
            homeSelector->setCurrentIndex(i - 1); QApplication::processEvents();
            check(homePosition->text() == position->text(), "home position follows the selected device channel");
            check(motions() == motionCountBeforeSelection, "selecting a home channel does not send motion commands");
            for (const char *prefix : {"emFwc3Home", "emFwc3Stop", "emFwc3Reverse", "emFwc3Forward"})
                check(page->findChild<QPushButton*>(QString::fromLatin1(prefix) + QString::number(i)) != nullptr, "all per-channel actions remain available");
        }
        check(motions() == 9, "absolute and both relative commands route to each selected SDK channel");
        for (int i = 1; i <= 3; ++i) {
            homeSelector->setCurrentIndex(i - 1); QApplication::processEvents();
            check(homePosition->text() == QString::number(i * 10.0, 'f', 3) + QStringLiteral("°"),
                  "switching the home selector immediately shows the selected channel readback");
            check(homeTarget->value() == i * 10.0,
                  "home editors follow the selected channel settings from the device page");
        }
        homeSelector->setCurrentIndex(1);
        homeTarget->setValue(24.0); QApplication::processEvents();
        check(find<QDoubleSpinBox>(*page, "emFwc3Target2")->value() == 24.0,
              "home edits update the corresponding device page channel");
        homeSelector->setCurrentIndex(0); QApplication::processEvents();
        check(homeTarget->value() == 10.0,
              "switching channels preserves independent target and step values");
        homeSelector->setCurrentIndex(1); QApplication::processEvents();
        check(homeTarget->value() == 24.0,
              "returning to a channel restores its target and step");
        find<QDoubleSpinBox>(*page, "emFwc3Target2")->setValue(25.0);
        find<QDoubleSpinBox>(*page, "emFwc3Step2")->setValue(1.25);
        QApplication::processEvents();
        check(homeTarget->value() == 25.0,
              "device page edits immediately update the selected home channel");
        check(homeGo->isEnabled() && configMinus->isEnabled() && configPlus->isEnabled(),
              "home motion actions are enabled for a connected idle device");
        homeGo->click();
        check(page->session()->state().busy && !homeGo->isEnabled() && !configMinus->isEnabled() &&
                  !configPlus->isEnabled() && homeStop->isEnabled(),
              "busy home motion disables movement while retaining the stop action");
        until("home absolute move reaches selected channel exactly once", [&] {
            return motions() == 10 && homePosition->text() == QStringLiteral("25.000°") && !page->session()->state().busy;
        });
        configPlus->click();
        until("home positive step reaches selected channel exactly once", [&] {
            return motions() == 11 && homePosition->text() == QStringLiteral("26.250°") && !page->session()->state().busy;
        });
        configMinus->click();
        until("home negative step reaches selected channel exactly once", [&] {
            return motions() == 12 && homePosition->text() == QStringLiteral("25.000°") && !page->session()->state().busy;
        });
        homeSelector->setCurrentIndex(0); QApplication::processEvents();
        check(homePosition->text() == QStringLiteral("10.000°"), "home motion does not affect other channels");
        selector->setCurrentIndex(0); QApplication::processEvents();
        check(find<QDoubleSpinBox>(*page, "emFwc3Target1")->value() == 10 && find<QDoubleSpinBox>(*page, "emFwc3Step1")->value() == 0.5,
              "switching channels preserves each channel's inputs");
        const auto stops = reinterpret_cast<int(*)(int)>(fake.resolve("testStops")); check(stops, "stop counters exist");
        find<QPushButton>(*page, "emFwc3StopAll")->click();
        until("stop all reaches three channels", [&] { return stops(0) == 1 && stops(1) == 1 && stops(2) == 1 && !page->session()->state().busy; });
        homeStop->click();
        until("home stop reaches each channel once", [&] { return stops(0) == 2 && stops(1) == 2 && stops(2) == 2 && !page->session()->state().busy; });
        find<QSpinBox>(*page, "emFwc3Speed")->setValue(80);
        find<QPushButton>(*page, "emFwc3ApplySpeed")->click();
        until("speed is applied and read back", [&] { return page->session()->state().speed == 80 && !page->session()->state().busy; });
        const auto saves = reinterpret_cast<int(*)()>(fake.resolve("testSaves")); check(saves, "save counter exists");
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); check(box, "save confirmation appears"); box->button(QMessageBox::Yes)->click(); });
        find<QPushButton>(*page, "emFwc3SavePosition")->click();
        until("confirmed position save reaches SDK", [&] { return saves() == 1 && !page->session()->state().busy; });
        checkCardGeometry(*panel);
        VaporView::setSettingsWritesSuspended(true);
        find<QPushButton>(*page, "emFwc3Go3")->click(); QApplication::processEvents();
        homeGo->click(); configPlus->click(); configMinus->click(); QApplication::processEvents();
        check(motions() == 12, "UI test write barrier blocks device and home motion commands");
        homeStop->click(); QApplication::processEvents();
        check(stops(0) == 2 && stops(1) == 2 && stops(2) == 2, "write barrier also blocks home stop");
        VaporView::setSettingsWritesSuspended(false);
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); check(box, "continuous confirmation appears"); box->button(QMessageBox::Yes)->click(); });
        find<QPushButton>(*page, "emFwc3Forward1")->click();
        until("continuous motion starts", [&] { return page->session()->state().continuousMotion && !page->session()->state().busy; });
        check(homePosition->text() == QStringLiteral("读取已暂停") &&
              find<QLabel>(*page, "emFwc3Position1")->text() == QStringLiteral("读取已暂停"),
              "both panels explain why angle feedback pauses during continuous motion");
        check(!homeGo->isEnabled() && !configPlus->isEnabled() && homeStop->isEnabled() &&
              !find<QPushButton>(*page, "emFwc3ApplySpeed")->isEnabled() &&
              find<QPushButton>(*page, "emFwc3Stop1")->isEnabled(),
              "continuous motion keeps stop controls available and blocks interrupting commands");
        homeStop->click();
        until("stopping restores feedback and controls", [&] {
            return !page->session()->state().continuousMotion && !page->session()->state().busy &&
                   homePosition->text() == QStringLiteral("10.000°") && homeGo->isEnabled();
        });
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); check(box, "home homing confirmation appears"); box->button(QMessageBox::Yes)->click(); });
        homeHome->click();
        until("home card homing reaches the selected channel", [&] { return homePosition->text() == QStringLiteral("0.000°") && !page->session()->state().busy; });
        QTimer::singleShot(0, [] { auto *box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); check(box, "home continuous confirmation appears"); box->button(QMessageBox::Yes)->click(); });
        configReverse->click();
        until("home reverse continuous motion starts", [&] { return page->session()->state().continuousMotion && !page->session()->state().busy; });
        check(!configForward->isEnabled() && !homeHome->isEnabled() && homeStop->isEnabled() && homeAction->isEnabled(),
              "home continuous motion retains stop and disconnect without enabling other motion");
        homeStop->click();
        until("home stop ends continuous motion", [&] { return !page->session()->state().continuousMotion && !page->session()->state().busy; });
        homeAction->click();
        check(homeAction->property("state") == QStringLiteral("connecting") && !overviewMotionAction->isEnabled(),
              "disconnect also animates both motor actions");
        until("disconnect completes", [&] { return !page->session()->state().connected && !page->session()->state().busy && homeAction->isEnabled(); });
        check(homeAction->property("state") == QStringLiteral("disconnected") && homePort->isEnabled() &&
              !overviewMotionStatus->property("connected").toBool(), "home disconnect restores shared connection controls");
        check(find<QLabel>(*page, "emFwc3Position3")->text() == QStringLiteral("—"), "disconnection clears position readbacks");
        check(!homeStop->isEnabled() && homeConnection->text() == QStringLiteral("未连接"), "disconnection updates home status and disables stop");
        check(!homeGo->isEnabled() && !configMinus->isEnabled() && !configPlus->isEnabled(),
              "disconnection disables home motion actions");
        for (int i = 0; i < 3; ++i) {
            homeSelector->setCurrentIndex(i); QApplication::processEvents();
            check(homePosition->text() == QStringLiteral("—"), "disconnection clears every selected channel readback");
        }
        overviewMotionAction->click();
        until("overview action connects the motor", [&] { return page->session()->state().connected && homeAction->isEnabled(); });
        const auto closeDelay = reinterpret_cast<void(*)(int)>(fake.resolve("testCloseDelay"));
        check(closeDelay, "SDK close delay control exists"); closeDelay(1800);
        find<QPushButton>(*page, "emFwc3Disconnect")->click();
        check(homeAction->property("state") == QStringLiteral("connecting"), "configuration action shares the home transition");
        transitionTimer.restart();
        while (transitionTimer.elapsed() < 1200) { QApplication::processEvents(); QThread::msleep(10); }
        check(page->session()->state().busy && !homeAction->isEnabled() &&
              homeAction->property("state") == QStringLiteral("connecting") &&
              overviewMotionAction->property("state") == QStringLiteral("connecting"),
              "slow disconnect keeps both icons animating after the minimum transition time");
        until("configuration disconnect updates both home actions", [&] { return !page->session()->state().connected && homeAction->isEnabled() && overviewMotionAction->isEnabled(); });
        closeDelay(0);
        homeSelector->setCurrentIndex(0); selector->setCurrentIndex(0);
        page->setTargetAngle(0, 17.25); page->setStepAngle(0, 2.5);
        const QString originalPort = page->portName();
        const int originalSpeed = find<QSpinBox>(*page, "emFwc3Speed")->value();
        auto *uiMode = window.findChild<QAction *>(QStringLiteral("uiTestModeAction"));
        check(uiMode, "application UI test mode action exists");
        reset();
        uiMode->trigger(); QApplication::processEvents();
        check(page->session()->isUiTestMode() && page->session()->state().connected &&
              homeConnection->text().contains(QStringLiteral("模拟")) && homeGo->isEnabled(),
              "application UI test mode populates and enables both motor panels");
        homeTarget->setValue(72.5); homeGo->click();
        check(page->session()->state().positions[0] == 72.5, "home positioning works in simulation");
        homeHome->click();
        check(page->session()->state().positions[0] == 0, "home simulation resets its angle without a hardware confirmation");
        find<QPushButton>(*page, "emFwc3Plus1")->click();
        check(page->session()->state().positions[0] == 2.5, "configuration relative motion is simulated");
        configForward->click();
        until("simulated continuous motion advances", [&] { return page->session()->state().positions[0] > 2.5; });
        homeStop->click();
        check(!page->session()->state().continuousMotion, "home stop halts simulated continuous motion");
        find<QSpinBox>(*page, "emFwc3Speed")->setValue(75);
        find<QPushButton>(*page, "emFwc3ApplySpeed")->click();
        check(page->session()->state().speed == 75, "simulation applies and reads back speed");
        page->session()->setUiTestScenario(true, false);
        check(!page->session()->state().valid[0], "failure simulation invalidates motor feedback");
        page->session()->setUiTestScenario(false, false);
        find<QPushButton>(*page, "emFwc3Disconnect")->click();
        check(!page->session()->state().connected, "simulation disconnect does not use the SDK");
        connect->click();
        check(page->session()->state().connected, "simulation reconnect restores motor controls");
        check(motions() == 0 && cemFpmCount() == 0 && stops(0) == 0 && saves() == 0,
              "UI test operations never reach the SDK double");
        uiMode->trigger(); QApplication::processEvents();
        check(!page->session()->isUiTestMode() && !page->session()->state().connected &&
              page->portName() == originalPort && page->targetAngle(0) == 17.25 &&
              page->stepAngle(0) == 2.5 && find<QSpinBox>(*page, "emFwc3Speed")->value() == originalSpeed,
              "leaving UI test restores motor settings and disconnects the simulated session");
        window.close();
    }
    {
        QWidget host; auto *hostLayout = new QVBoxLayout(&host);
        auto *page = new MotionControlPage(&host, QString::fromLocal8Bit(argv[1]));
        hostLayout->addWidget(page); hostLayout->addStretch(); host.resize(1200, 1100); host.show(); QApplication::processEvents();
        for (bool english : {false, true}) for (bool dark : {false, true}) for (int scale : {100, 130}) {
            QFont font = app.font(); font.setPointSizeF(10 * scale / 100.0); host.setFont(font);
            page->setTheme(dark, scale); page->setLanguage(english);
            check(find<QComboBox>(*page, "emFwc3ChannelSelector")->isHidden(), "theme and language changes never reveal the channel selector");
            for (int width : {1200, 1000, 800, 600}) {
                host.resize(width, 1100); QApplication::processEvents();
                check(host.width() == width, "compact controls fit requested window width at both font scales");
                auto *port = find<QComboBox>(*page, "emFwc3Port");
                check(port->width() >= 80 && port->width() <= qRound(150 * scale / 100.0),
                      "serial selector remains compact at each font scale");
                const auto bounds = [page](QWidget *widget) {
                    return QRect(widget->mapTo(page, QPoint(0, 0)), widget->size());
                };
                for (int channel = 1; channel <= 3; ++channel) {
                    find<QComboBox>(*page, "emFwc3ChannelSelector")->setCurrentIndex(channel - 1);
                    {
                        QApplication::processEvents(); checkCardGeometry(*page);
                        auto *area = page->findChild<QWidget*>(QStringLiteral("emFwc3ContinuousArea%1").arg(channel));
                        auto *body = page->findChild<QWidget*>(QStringLiteral("emFwc3ContinuousBody%1").arg(channel));
                        check(!body->isHidden() && !page->findChild<QCheckBox*>(QStringLiteral("emFwc3ContinuousToggle%1").arg(channel)),
                              "continuous controls stay visible without a checkbox");
                        const auto named = [&](const QString& name) {
                            auto *widget = page->findChild<QWidget*>(name);
                            check(widget != nullptr, "motion control exists");
                            return widget;
                        };
                        const QString suffix = QString::number(channel);
                        auto *target = named(QStringLiteral("emFwc3Target") + suffix);
                        auto *step = named(QStringLiteral("emFwc3Step") + suffix);
                        auto *home = named(QStringLiteral("emFwc3Home") + suffix);
                        auto *speed = named(QStringLiteral("emFwc3Speed"));
                        auto *save = named(QStringLiteral("emFwc3SavePosition"));
                        for (auto *widget : {target, step, home, area, speed, save})
                            check(page->rect().contains(bounds(widget)), "motion controls stay within the card at wide and narrow widths");
                        if (width == 1200 && scale == 100 && !english) {
                            check(bounds(target).left() == bounds(step).left(), "target and step input columns align");
                            check(bounds(home).left() == bounds(area).left(), "motion action rows align on the left");
                            check(bounds(speed).left() == bounds(target).left(), "speed and angle input columns align");
                        }
                    }
                }

            }
        }
        page->setLanguage(false); page->setTheme(false, 100); host.setFont(app.font()); host.resize(1200, 1100);
        QApplication::processEvents(); host.close();
    }
    qInstallMessageHandler(previousMessageHandler);
    check(titleBarSizeWarnings == 0, "motion card title bars do not exceed Qt's maximum widget size");
    std::cout << "PASS: embedded motion card, channel selection, FPGA switching, commands and compact layout\n";
}
