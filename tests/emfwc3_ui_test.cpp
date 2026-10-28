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
#include <QFrame>
#include <QLabel>
#include <QLibrary>
#include <QPushButton>
#include <QScopeGuard>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSpinBox>
#include <QThread>
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
void until(const std::function<bool()>& ready)
{
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 6000) { QApplication::processEvents(); QThread::msleep(10); }
    check(ready(), "UI asynchronous operation completes");
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
        const auto firstCard = find<QFrame>(*page, "emFwc3Channel1")->geometry();
        const auto secondCard = find<QFrame>(*page, "emFwc3Channel2")->geometry();
        check(firstCard.y() == secondCard.y(), "first opening in the main window uses settled viewport width for side-by-side channels");
        checkCardGeometry(*page);
        VaporView::setSettingsWritesSuspended(true);
        find<QPushButton>(*page, "emFwc3Go3")->click(); QApplication::processEvents();
        check(motions() == 1, "UI test write barrier also blocks hardware commands");
        VaporView::setSettingsWritesSuspended(false);
        find<QPushButton>(*page, "emFwc3ConnectionSettings")->click();
        check(find<QStackedWidget>(window, "mainPageStack")->currentWidget()->objectName() == QStringLiteral("deviceConfigPage"), "motion page navigates to connection settings");
        checkCardGeometry(*panel);
        find<QPushButton>(*panel, "emFwc3Disconnect")->click(); until([&] { return !page->session()->state().connected && !page->session()->state().busy; });
        check(find<QLabel>(*page, "emFwc3Position3")->text() == QStringLiteral("—"), "disconnection clears position readbacks");
        window.close();
    }
    {
        MotionControlPage page(nullptr, QString::fromLocal8Bit(argv[1]));
        QWidget settings; auto *settingsLayout = new QVBoxLayout(&settings);
        settingsLayout->addWidget(page.createConnectionPanel(&settings));
        settingsLayout->addStretch();
        settings.resize(1200, 1100); settings.show();
        page.resize(1200, 1000); page.show(); QApplication::processEvents();
        for (bool english : {false, true}) for (bool dark : {false, true}) for (int scale : {100, 130}) {
            QFont font = app.font(); font.setPointSizeF(10 * scale / 100.0); page.setFont(font);
            settings.setFont(font); page.setTheme(dark, scale); page.setLanguage(english);
            for (int width : {1200, 600}) {
                page.resize(width, 1000); settings.resize(width, 1100); QApplication::processEvents();
                const auto a = find<QFrame>(page, "emFwc3Channel1")->geometry();
                const auto b = find<QFrame>(page, "emFwc3Channel2")->geometry();
                const auto c = find<QFrame>(page, "emFwc3Channel3")->geometry();
                check(!a.intersects(b) && !b.intersects(c) && !a.intersects(c), "channel cards reflow without overlap at both font scales");
                for (bool expanded : {false, true}) {
                    for (int i = 1; i <= 3; ++i)
                        page.findChild<QCheckBox*>(QStringLiteral("emFwc3ContinuousToggle%1").arg(i))->setChecked(expanded);
                    QApplication::processEvents(); checkCardGeometry(page); checkCardGeometry(settings);
                    for (int i = 1; i <= 3; ++i) {
                        auto *area = page.findChild<QWidget*>(QStringLiteral("emFwc3ContinuousArea%1").arg(i));
                        auto *toggle = page.findChild<QCheckBox*>(QStringLiteral("emFwc3ContinuousToggle%1").arg(i));
                        auto *body = page.findChild<QWidget*>(QStringLiteral("emFwc3ContinuousBody%1").arg(i));
                        check(body->isHidden() != expanded, "continuous controls collapse without an empty frame");
                        check(area->rect().contains(toggle->geometry()), "continuous-motion label stays inside its layout");
                        if (expanded) check(!toggle->geometry().intersects(body->geometry()), "continuous-motion heading and controls do not overlap");
                    }
                }
            }
        }
        page.setLanguage(false); page.setTheme(false, 100); page.resize(1200, 1000);
        QApplication::processEvents();
        page.close(); settings.close();
    }
    qInstallMessageHandler(previousMessageHandler);
    check(titleBarSizeWarnings == 0, "motion card title bars do not exceed Qt's maximum widget size");
    std::cout << "PASS: independent motion UI, FPGA switching, commands and responsive layout\n";
}
