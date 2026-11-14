#include "test_settings_sandbox.h"
#include "ground/main/MainWindow.h"
#include "ground/motion/HomeMotionPanel.h"
#include "ground/motion/MotionControlPage.h"
#include "shared/config/SettingsWriteBarrier.h"
#include <QApplication>
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
        auto *homeConnection = find<QLabel>(*homeMotion, "homeMotionConnectionState");
        auto *homeSelector = find<QComboBox>(window, "homeMotionChannelCombo");
        auto *homePosition = find<QLabel>(*homeMotion, "homeMotionPosition");
        auto *homeTarget = find<QDoubleSpinBox>(*homeMotion, "homeMotionTarget");
        auto *homeStep = find<QDoubleSpinBox>(*homeMotion, "homeMotionStep");
        auto *homeGo = find<QPushButton>(*homeMotion, "homeMotionGo");
        auto *homeMinus = find<QPushButton>(*homeMotion, "homeMotionMinus");
        auto *homePlus = find<QPushButton>(*homeMotion, "homeMotionPlus");
        auto *homeCard = find<QWidget>(window, "homeMotionCard");
        auto *homeHeader = homeCard->findChild<QWidget *>(QStringLiteral("sectionTitleBar"), Qt::FindDirectChildrenOnly);
        auto *homePort = find<QComboBox>(*homeCard, "homeMotionPort");
        auto *homeConnect = find<QToolButton>(*homeCard, "homeMotionConnect");
        auto *homeDisconnect = find<QToolButton>(*homeCard, "homeMotionDisconnect");
        auto *homeHome = find<QPushButton>(*homeMotion, "homeMotionHome");
        auto *homeForward = find<QPushButton>(*homeMotion, "homeMotionForward");
        auto *homeReverse = find<QPushButton>(*homeMotion, "homeMotionReverse");
        check(homeHeader && homeHeader->isAncestorOf(homePort) && homeHeader->isAncestorOf(homeConnect) &&
              homeHeader->isAncestorOf(homeDisconnect), "home serial and connection controls belong to the title bar");
        check(homePort->lineEdit()->contentsRect().width() >= homePort->fontMetrics().horizontalAdvance(QStringLiteral("COM50")),
              "home serial selector reserves room for an actual COM port name");
        for (auto *button : {homeConnect, homeDisconnect})
            check(button->text().isEmpty() && !button->icon().isNull() && !button->toolTip().isEmpty() &&
                  !button->accessibleName().isEmpty(), "home connection actions use labeled accessible icons without visible text");
        check(homeForward->isVisibleTo(homeMotion) && homeReverse->isVisibleTo(homeMotion),
              "home continuous controls are always visible without a checkbox");
        check(homeHeader && homeHeader->isAncestorOf(homeSelector) && homeSelector->isHidden(),
              "home channel selector stays hidden without removing its routing code");
        check(homeSelector->count() == 3 && homeSelector->currentIndex() == 0,
              "home controls default to channel one and retain internal multi-channel support");
        check(!homeStop->isEnabled(), "home stop is disabled while disconnected");
        check(!homeGo->isEnabled() && !homeMinus->isEnabled() && !homePlus->isEnabled(),
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
        auto *port = find<QComboBox>(*panel, "emFwc3Port"); port->setEditText(QStringLiteral("FAKE-COM"));
        check(homePort->currentText() == port->currentText(), "configuration port selection updates the home card");
        homePort->setEditText(QString());
        check(!homeConnect->isEnabled() && !connect->isEnabled(), "empty home COM selection disables both connect actions");
        homePort->setEditText(QStringLiteral("HOME-COM"));
        check(port->currentText() == QStringLiteral("HOME-COM") && homeConnect->isEnabled(),
              "home COM selection updates configuration and enables connection");
        for (int i = 1; i <= 3; ++i)
            check(panel->findChild<QLabel*>(QStringLiteral("emFwc3MotorType%1").arg(i))->text() == QStringLiteral("CEM-FPM") &&
                  !panel->findChild<QComboBox*>(QStringLiteral("emFwc3MotorType%1").arg(i)),
                  "motor model is fixed to CEM-FPM despite legacy EM-FPM settings");
        check(!page->session()->state().connected && !find<QPushButton>(*page, "emFwc3Go1")->isEnabled(), "startup never connects or enables motion");
        check(!panel->findChild<QCheckBox*>(QStringLiteral("emFwc3ConfirmTypes")) && connect->isEnabled(),
              "connection needs only a COM port with no model-confirmation checkbox");
        homeConnect->click(); until("connect completes", [&] { return page->session()->state().connected && !page->session()->state().busy; });
        check(!homeConnect->isEnabled() && homeDisconnect->isEnabled() && !homePort->isEnabled(),
              "home connection uses the same session and locks port editing");
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
            check(homeTarget->value() == i * 10.0 && homeStep->value() == i * 0.5,
                  "home editors follow the selected channel settings from the device page");
        }
        homeSelector->setCurrentIndex(1);
        homeTarget->setValue(24.0); homeStep->setValue(0.75); QApplication::processEvents();
        check(find<QDoubleSpinBox>(*page, "emFwc3Target2")->value() == 24.0 &&
              find<QDoubleSpinBox>(*page, "emFwc3Step2")->value() == 0.75,
              "home edits update the corresponding device page channel");
        homeSelector->setCurrentIndex(0); QApplication::processEvents();
        check(homeTarget->value() == 10.0 && homeStep->value() == 0.5,
              "switching channels preserves independent target and step values");
        homeSelector->setCurrentIndex(1); QApplication::processEvents();
        check(homeTarget->value() == 24.0 && homeStep->value() == 0.75,
              "returning to a channel restores its target and step");
        find<QDoubleSpinBox>(*page, "emFwc3Target2")->setValue(25.0);
        find<QDoubleSpinBox>(*page, "emFwc3Step2")->setValue(1.25);
        QApplication::processEvents();
        check(homeTarget->value() == 25.0 && homeStep->value() == 1.25,
              "device page edits immediately update the selected home channel");
        check(homeGo->isEnabled() && homeMinus->isEnabled() && homePlus->isEnabled(),
              "home motion actions are enabled for a connected idle device");
        homeGo->click();
        check(page->session()->state().busy && !homeGo->isEnabled() && !homeMinus->isEnabled() &&
                  !homePlus->isEnabled() && homeStop->isEnabled(),
              "busy home motion disables movement while retaining the stop action");
        until("home absolute move reaches selected channel exactly once", [&] {
            return motions() == 10 && homePosition->text() == QStringLiteral("25.000°") && !page->session()->state().busy;
        });
        homePlus->click();
        until("home positive step reaches selected channel exactly once", [&] {
            return motions() == 11 && homePosition->text() == QStringLiteral("26.250°") && !page->session()->state().busy;
        });
        homeMinus->click();
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
        homeGo->click(); homePlus->click(); homeMinus->click(); QApplication::processEvents();
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
        check(!homeGo->isEnabled() && !homePlus->isEnabled() && homeStop->isEnabled() &&
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
        homeReverse->click();
        until("home reverse continuous motion starts", [&] { return page->session()->state().continuousMotion && !page->session()->state().busy; });
        check(!homeForward->isEnabled() && !homeHome->isEnabled() && homeStop->isEnabled() && homeDisconnect->isEnabled(),
              "home continuous motion retains stop and disconnect without enabling other motion");
        homeStop->click();
        until("home stop ends continuous motion", [&] { return !page->session()->state().continuousMotion && !page->session()->state().busy; });
        homeDisconnect->click(); until("disconnect completes", [&] { return !page->session()->state().connected && !page->session()->state().busy; });
        check(homeConnect->isEnabled() && !homeDisconnect->isEnabled() && homePort->isEnabled(), "home disconnect restores connection controls");
        check(find<QLabel>(*page, "emFwc3Position3")->text() == QStringLiteral("—"), "disconnection clears position readbacks");
        check(!homeStop->isEnabled() && homeConnection->text() == QStringLiteral("未连接"), "disconnection updates home status and disables stop");
        check(!homeGo->isEnabled() && !homeMinus->isEnabled() && !homePlus->isEnabled(),
              "disconnection disables home motion actions");
        for (int i = 0; i < 3; ++i) {
            homeSelector->setCurrentIndex(i); QApplication::processEvents();
            check(homePosition->text() == QStringLiteral("—"), "disconnection clears every selected channel readback");
        }
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
                const auto sameRow = [&](QWidget *left, QWidget *right) {
                    const QRect a = bounds(left), b = bounds(right);
                    return a.top() <= b.bottom() && b.top() <= a.bottom() && !a.intersects(b);
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
                            check(sameRow(target, step), "wide card places target and relative step on one row");
                            check(sameRow(home, area), "wide card places motion actions and continuous controls on one row");
                            check(sameRow(speed, save), "wide card places common speed and device actions on one row");
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
