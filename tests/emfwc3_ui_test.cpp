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
#include <QLibrary>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
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
    {
        MainWindow window; window.resize(1280, 850); window.show();
        auto *page = find<MotionControlPage>(window, "emFwc3ConnectionPanel");
        auto *homeMotion = find<HomeMotionPanel>(window, "homeMotionPanel");
        auto *homeStop = find<QPushButton>(*homeMotion, "homeMotionStopAll");
        auto *homeConnection = find<QLabel>(*homeMotion, "homeMotionConnectionState");
        check(!homeStop->isEnabled(), "home stop is disabled while disconnected");
        for (int i = 1; i <= 3; ++i)
            check(homeMotion->findChild<QLabel*>(QStringLiteral("homeMotionPosition%1").arg(i))->text() == QStringLiteral("—"),
                  "home positions are empty while disconnected");
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
        for (int i = 1; i <= 3; ++i)
            check(panel->findChild<QComboBox*>(QStringLiteral("emFwc3MotorType%1").arg(i))->currentText() == QStringLiteral("CEM-FPM"), "new setup defaults to the confirmed CEM-FPM hardware");
        check(!page->session()->state().connected && !find<QPushButton>(*page, "emFwc3Go1")->isEnabled(), "startup never connects or enables motion");
        check(!connect->isEnabled(), "motor types must be checked before connecting");
        find<QCheckBox>(*panel, "emFwc3ConfirmTypes")->setChecked(true); check(connect->isEnabled(), "explicit model confirmation enables connection");
        connect->click(); until("connect completes", [&] { return page->session()->state().connected && !page->session()->state().busy; });
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
        check(selector->count() == 3, "one selector exposes all three channels");
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
            check(homeMotion->findChild<QLabel*>(QStringLiteral("homeMotionPosition%1").arg(i))->text() == position->text(),
                  "home position follows the corresponding device channel");
            for (const char *prefix : {"emFwc3Home", "emFwc3Stop", "emFwc3Reverse", "emFwc3Forward"})
                check(page->findChild<QPushButton*>(QString::fromLatin1(prefix) + QString::number(i)) != nullptr, "all per-channel actions remain available");
        }
        check(motions() == 9, "absolute and both relative commands route to each selected SDK channel");
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
        check(motions() == 9, "UI test write barrier also blocks hardware commands");
        homeStop->click(); QApplication::processEvents();
        check(stops(0) == 2 && stops(1) == 2 && stops(2) == 2, "write barrier also blocks home stop");
        VaporView::setSettingsWritesSuspended(false);
        find<QPushButton>(*panel, "emFwc3Disconnect")->click(); until("disconnect completes", [&] { return !page->session()->state().connected && !page->session()->state().busy; });
        check(find<QLabel>(*page, "emFwc3Position3")->text() == QStringLiteral("—"), "disconnection clears position readbacks");
        check(!homeStop->isEnabled() && homeConnection->text() == QStringLiteral("未连接"), "disconnection updates home status and disables stop");
        for (int i = 1; i <= 3; ++i)
            check(homeMotion->findChild<QLabel*>(QStringLiteral("homeMotionPosition%1").arg(i))->text() == QStringLiteral("—"),
                  "disconnection clears home positions");
        window.close();
    }
    {
        QWidget host; auto *hostLayout = new QVBoxLayout(&host);
        auto *page = new MotionControlPage(&host, QString::fromLocal8Bit(argv[1]));
        hostLayout->addWidget(page); hostLayout->addStretch(); host.resize(1200, 1100); host.show(); QApplication::processEvents();
        for (bool english : {false, true}) for (bool dark : {false, true}) for (int scale : {100, 130}) {
            QFont font = app.font(); font.setPointSizeF(10 * scale / 100.0); host.setFont(font);
            page->setTheme(dark, scale); page->setLanguage(english);
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
                    for (bool expanded : {false, true}) {
                        auto *toggle = page->findChild<QCheckBox*>(QStringLiteral("emFwc3ContinuousToggle%1").arg(channel));
                        toggle->setChecked(expanded); QApplication::processEvents(); checkCardGeometry(*page);
                        auto *area = page->findChild<QWidget*>(QStringLiteral("emFwc3ContinuousArea%1").arg(channel));
                        auto *body = page->findChild<QWidget*>(QStringLiteral("emFwc3ContinuousBody%1").arg(channel));
                        check(body->isHidden() != expanded, "continuous controls collapse without an empty frame");
                        check(area->rect().contains(toggle->geometry()), "continuous-motion label stays inside its layout");
                        if (expanded) check(!toggle->geometry().intersects(body->geometry()), "continuous-motion heading and controls do not overlap");
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
                        for (auto *widget : {target, step, home, static_cast<QWidget*>(toggle), speed, save})
                            check(page->rect().contains(bounds(widget)), "motion controls stay within the card at wide and narrow widths");
                        if (width == 1200 && scale == 100 && !english) {
                            check(sameRow(target, step), "wide card places target and relative step on one row");
                            check(sameRow(home, toggle), "wide card places motion actions and continuous controls on one row");
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
