#include "ground/main/MainWindow.h"
#include "ground/main/UiLogModel.h"
#include "ground/rtk/RtkConfigDialog.h"
#include "ground/wave/TcpWavePanel.h"
#include "ground/widgets/Ai8TemperatureControllerPanel.h"
#include "ground/widgets/SegmentedSwitchButton.h"
#include "ground/widgets/SkyDeviceConfigDialog.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "test_ui_helpers.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QColor>
#include <QDir>
#include <QDialog>
#include <QEventLoop>
#include <QFrame>
#include <QGroupBox>
#include <QHostAddress>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMetaObject>
#include <QMap>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QVariant>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <tuple>

namespace
{

void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

using SettingsSnapshot = QMap<QString, QVariant>;

SettingsSnapshot snapshot(const QString& application)
{
    QSettings settings(QStringLiteral("VaporView"), application);
    SettingsSnapshot result;
    for (const QString& key : settings.allKeys())
    {
        result.insert(key, settings.value(key));
    }
    return result;
}

QMap<QString, SettingsSnapshot> snapshotAll()
{
    QMap<QString, SettingsSnapshot> result;
    for (const QString& application : {
             QStringLiteral("MainWindow"), QStringLiteral("SerialPortHistory"),
             QStringLiteral("RtkConfig"), QStringLiteral("TcpWavePanel"),
             QStringLiteral("SessionViewer"), QStringLiteral("TrajectoryViewer"),
             QStringLiteral("Map3D")})
    {
        result.insert(application, snapshot(application));
    }
    return result;
}

void processEvents()
{
    QApplication::processEvents(QEventLoop::AllEvents, 100);
}

int findLogMessageRow(QListView *logList, const QString& chineseText, const QString& englishText)
{
    if (!logList || !logList->model())
    {
        return -1;
    }
    for (int row = 0; row < logList->model()->rowCount(); ++row)
    {
        const QString message = logList->model()->index(row, 0)
            .data(VaporView::Ground::Main::UiLogModel::MessageRole)
            .toString();
        if (message.contains(chineseText) || message.contains(englishText))
        {
            return row;
        }
    }
    return -1;
}

QWidget *homeTelemetrySummaryContainer(QWidget *homeConfigCard)
{
    return homeConfigCard
        ? homeConfigCard->findChild<QWidget *>(QStringLiteral("homeTelemetrySummaryContainer"))
        : nullptr;
}

QList<QFrame *> sortedHomeTelemetrySections(QWidget *summaryContainer)
{
    if (!summaryContainer)
    {
        return {};
    }
    QList<QFrame *> sections =
        summaryContainer->findChildren<QFrame *>(QStringLiteral("homeTelemetrySectionCard"));
    std::sort(sections.begin(), sections.end(), [summaryContainer](QFrame *lhs, QFrame *rhs) {
        const QPoint lhsPos = lhs->mapTo(summaryContainer, QPoint(0, 0));
        const QPoint rhsPos = rhs->mapTo(summaryContainer, QPoint(0, 0));
        return std::make_tuple(lhsPos.y(), lhsPos.x()) < std::make_tuple(rhsPos.y(), rhsPos.x());
    });
    return sections;
}

bool homeTelemetrySummaryShowsUiTestRates(QWidget *homeConfigCard)
{
    QWidget *summaryContainer = homeTelemetrySummaryContainer(homeConfigCard);
    const QList<QFrame *> sections = sortedHomeTelemetrySections(summaryContainer);
    if (sections.size() < 3)
    {
        return false;
    }

    int nonZeroHzValues = 0;
    for (QLabel *valueLabel : sections.at(0)->findChildren<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel")))
    {
        const QString text = valueLabel->text();
        if (text.endsWith(QStringLiteral("Hz")) && text != QStringLiteral("0.0 Hz"))
        {
            ++nonZeroHzValues;
        }
    }

    int kbpsValues = 0;
    for (QLabel *valueLabel : sections.at(1)->findChildren<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel")))
    {
        if (valueLabel->text().contains(QStringLiteral("kbps")))
        {
            ++kbpsValues;
        }
    }

    bool sawAvailableData = false;
    for (QLabel *valueLabel : sections.at(2)->findChildren<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel")))
    {
        sawAvailableData = sawAvailableData ||
            valueLabel->text() == QStringLiteral("有") ||
            valueLabel->text() == QStringLiteral("Yes");
    }
    return nonZeroHzValues >= 6 && kbpsValues == 3 && sawAvailableData;
}

void requireCompactTelemetryPillTextGap(QFrame *pill, const char *message)
{
    require(pill != nullptr, message);
    QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
    QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
    require(nameLabel != nullptr && valueLabel != nullptr, message);
    require(valueLabel->alignment().testFlag(Qt::AlignLeft),
            "UI-test home telemetry values start near their field names");

    const int nameTextRight = nameLabel->mapTo(pill, QPoint(0, 0)).x() +
        nameLabel->fontMetrics().horizontalAdvance(nameLabel->text());
    const int valueTextLeft = valueLabel->mapTo(pill, QPoint(0, 0)).x();
    const int gap = valueTextLeft - nameTextRight;
    if (gap < -1 || gap > 6)
    {
        std::cerr << "UI-test telemetry pill text gap: name='"
                  << nameLabel->text().toStdString()
                  << "' value='" << valueLabel->text().toStdString()
                  << "' gap=" << gap << '\n';
    }
    require(gap >= -1 && gap <= 6,
            "UI-test home telemetry field/value text gap stays compact");
}

void requireUiTestHomeTelemetryCapsulesCovered(QWidget *homeConfigCard, const char *message)
{
    QWidget *summaryContainer = homeTelemetrySummaryContainer(homeConfigCard);
    require(summaryContainer != nullptr, message);
    const QList<QFrame *> sections = sortedHomeTelemetrySections(summaryContainer);
    require(sections.size() >= 3, message);

    for (QLabel *label : summaryContainer->findChildren<QLabel *>())
    {
        const QString text = label->text();
        require(!text.contains(QLatin1Char(':')) && !text.contains(QStringLiteral("：")),
                "UI-test home telemetry summary labels omit colon separators");
        if (text.isEmpty())
        {
            continue;
        }
        const int textWidth = label->fontMetrics().horizontalAdvance(text);
        if (textWidth > label->width() + 1)
        {
            std::cerr << "UI-test telemetry label clipped: object="
                      << label->objectName().toStdString()
                      << " text='" << text.toStdString()
                      << "' textWidth=" << textWidth
                      << " labelWidth=" << label->width() << '\n';
        }
        require(textWidth <= label->width() + 1,
                "UI-test home telemetry summary label text fits");
    }

    for (QFrame *section : sections)
    {
        const QList<QFrame *> sectionPills =
            section->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
        require(!sectionPills.isEmpty(), message);

        QList<QWidget *> lines;
        for (QFrame *pill : sectionPills)
        {
            const QRect pillRect(pill->mapTo(section, QPoint(0, 0)), pill->size());
            require(section->rect().adjusted(0, 0, 1, 1).contains(pillRect),
                    "UI-test home telemetry summary pill stays inside its section");
            if (QWidget *line = pill->parentWidget(); line && !lines.contains(line))
            {
                lines.append(line);
            }
            for (QLabel *pillLabel : pill->findChildren<QLabel *>())
            {
                const QRect labelRect(pillLabel->mapTo(pill, QPoint(0, 0)), pillLabel->size());
                require(pill->rect().adjusted(0, 0, 1, 1).contains(labelRect),
                        "UI-test home telemetry summary pill label stays inside its capsule");
            }
            requireCompactTelemetryPillTextGap(
                pill,
                "UI-test home telemetry summary keeps compact field/value spacing");
        }

        for (QWidget *line : lines)
        {
            QLabel *titleLabel = line->findChild<QLabel *>(
                QStringLiteral("homeTelemetrySummaryTitleLabel"),
                Qt::FindDirectChildrenOnly);
            QList<QFrame *> linePills =
                line->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"),
                                             Qt::FindDirectChildrenOnly);
            std::sort(linePills.begin(), linePills.end(), [section](QFrame *lhs, QFrame *rhs) {
                return lhs->mapTo(section, QPoint(0, 0)).x() <
                       rhs->mapTo(section, QPoint(0, 0)).x();
            });
            if (titleLabel && !linePills.isEmpty())
            {
                const QRect titleRect(titleLabel->mapTo(section, QPoint(0, 0)), titleLabel->size());
                const QRect firstPillRect(linePills.first()->mapTo(section, QPoint(0, 0)),
                                          linePills.first()->size());
                require(firstPillRect.left() > titleRect.right(),
                        "UI-test home telemetry title does not overlap the first capsule");
                const int titleTextRight =
                    titleRect.left() + titleLabel->fontMetrics().horizontalAdvance(titleLabel->text());
                require(firstPillRect.left() - titleTextRight >= 6,
                        "UI-test home telemetry title keeps a visible gap before the first capsule");
            }

            QRect previousRect;
            bool hasPrevious = false;
            for (QFrame *pill : linePills)
            {
                const QRect pillRect(pill->mapTo(section, QPoint(0, 0)), pill->size());
                require(!hasPrevious || pillRect.left() > previousRect.right(),
                        "UI-test home telemetry capsules do not overlap within a row");
                previousRect = pillRect;
                hasPrevious = true;
            }
        }
    }

    const QList<QFrame *> linkPills =
        sections.at(1)->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    require(linkPills.size() == 3, "UI-test link-rate summary keeps three capsules");
    for (QFrame *pill : linkPills)
    {
        QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
        require(valueLabel != nullptr && valueLabel->text().contains(QStringLiteral("kbps")),
                "UI-test link-rate capsule shows representative kbps data");
        require(valueLabel->width() >= valueLabel->fontMetrics().horizontalAdvance(QStringLiteral("999 kbps")),
                "UI-test link-rate capsule reserves the requested 999 kbps width");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(), "temporary settings directory created");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QApplication application(argc, argv);

    {
        QSettings mainSettings(QStringLiteral("VaporView"), QStringLiteral("MainWindow"));
        mainSettings.setValue(QStringLiteral("serial/epsilon_port"), QStringLiteral("NORMAL-COM7"));
        mainSettings.setValue(QStringLiteral("dark_theme_enabled"), false);
        mainSettings.setValue(QStringLiteral("font_scale_percent"), 100);
        mainSettings.setValue(QStringLiteral("recording_directory"), settingsDirectory.filePath(QStringLiteral("business-output")));
        mainSettings.sync();
        QSettings history(QStringLiteral("VaporView"), QStringLiteral("SerialPortHistory"));
        history.setValue(QStringLiteral("ports"), QStringList{QStringLiteral("NORMAL-COM7")});
        history.sync();
        QSettings rtkSettings(QStringLiteral("VaporView"), QStringLiteral("RtkConfig"));
        rtkSettings.setValue(QStringLiteral("server"), QStringLiteral("persisted.ui-test.caster"));
        rtkSettings.setValue(QStringLiteral("port"), QStringLiteral("2201"));
        rtkSettings.setValue(QStringLiteral("username"), QStringLiteral("persisted-user"));
        rtkSettings.setValue(QStringLiteral("password"), QStringLiteral("persisted-password"));
        rtkSettings.setValue(QStringLiteral("mountpoint"), QStringLiteral("PERSISTED_MOUNTPOINT"));
        rtkSettings.setValue(QStringLiteral("mountpoint_confirmed"), true);
        rtkSettings.setValue(QStringLiteral("timeout"), QStringLiteral("8000"));
        rtkSettings.setValue(QStringLiteral("reconnect"), QStringLiteral("2000"));
        rtkSettings.sync();
    }

    auto *window = new MainWindow();
    window->show();
    processEvents();
    QComboBox *epsilonPort = window->findChild<QComboBox *>(QStringLiteral("epsilonPortCombo"));
    QLineEdit *rtkServer = window->findChild<QLineEdit *>(QStringLiteral("rtkServerEdit"));
    QLineEdit *rtkPort = window->findChild<QLineEdit *>(QStringLiteral("rtkPortEdit"));
    QLineEdit *rtkUsername = window->findChild<QLineEdit *>(QStringLiteral("rtkUsernameEdit"));
    QLineEdit *rtkPassword = window->findChild<QLineEdit *>(QStringLiteral("rtkPasswordEdit"));
    QComboBox *rtkMountpoint = window->findChild<QComboBox *>(QStringLiteral("rtkMountpointCombo"));
    require(epsilonPort && rtkServer && rtkPort && rtkUsername && rtkPassword && rtkMountpoint,
            "main and RTK configuration controls exist");

    auto *mainPageStack = window->findChild<QStackedWidget *>(QStringLiteral("mainPageStack"));
    auto *homeScrollArea = mainPageStack
        ? qobject_cast<QScrollArea *>(mainPageStack->currentWidget())
        : nullptr;
    auto *homeBottomFade = homeScrollArea
        ? homeScrollArea->viewport()->findChild<QWidget *>(
              QStringLiteral("mainContentBottomFade"), Qt::FindDirectChildrenOnly)
        : nullptr;
    require(homeBottomFade &&
                homeBottomFade->geometry().bottom() == homeScrollArea->viewport()->rect().bottom(),
            "home bottom fade stays attached to the viewport edge above the reserved content inset");

    VaporView::Ground::Widgets::SegmentedSwitchButton *sourceModeSwitch = nullptr;
    for (auto *candidate : window->findChildren<VaporView::Ground::Widgets::SegmentedSwitchButton *>())
    {
        if ((candidate->leftSegmentText().contains(QStringLiteral("本地")) ||
             candidate->leftSegmentText().contains(QStringLiteral("Local"))) &&
            (candidate->rightSegmentText().contains(QStringLiteral("远程")) ||
             candidate->rightSegmentText().contains(QStringLiteral("Remote"))))
        {
            sourceModeSwitch = candidate;
            break;
        }
    }
    require(sourceModeSwitch, "home source mode switch exists");
    QGroupBox *homeConfigCard = nullptr;
    for (QWidget *ancestor = sourceModeSwitch->parentWidget(); ancestor && !homeConfigCard;
         ancestor = ancestor->parentWidget())
    {
        homeConfigCard = qobject_cast<QGroupBox *>(ancestor);
    }
    require(homeConfigCard, "home device overview card exists");
    QList<QPointer<QFrame>> telemetryPillsBeforeModeSwitch;
    for (QFrame *pill : homeConfigCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill")))
    {
        telemetryPillsBeforeModeSwitch.append(pill);
    }
    require(!telemetryPillsBeforeModeSwitch.isEmpty(),
            "home telemetry summary contains persistent pills before source-mode switching");
    sourceModeSwitch->click();
    processEvents();
    for (const QPointer<QFrame>& pill : telemetryPillsBeforeModeSwitch)
    {
        require(!pill.isNull(),
                "unchanged home telemetry summary pills survive switching to remote mode");
    }
    sourceModeSwitch->click();
    processEvents();
    for (const QPointer<QFrame>& pill : telemetryPillsBeforeModeSwitch)
    {
        require(!pill.isNull(),
                "unchanged home telemetry summary pills survive switching back to local mode");
    }

    epsilonPort->addItem(QStringLiteral("UNSAVED-COM42"), QStringLiteral("UNSAVED-COM42"));
    epsilonPort->setCurrentIndex(epsilonPort->count() - 1);
    rtkServer->setText(QStringLiteral("normal.unpersisted.caster"));
    QStringList epsilonItemsBefore;
    for (int index = 0; index < epsilonPort->count(); ++index)
    {
        epsilonItemsBefore.push_back(epsilonPort->itemText(index));
    }
    const auto before = snapshotAll();

    QAction *modeAction = window->findChild<QAction *>(QStringLiteral("uiTestModeAction"));
    QLabel *badge = window->findChild<QLabel *>(QStringLiteral("uiTestModeBadge"));
    QMenu *scenarioMenu = window->findChild<QMenu *>(QStringLiteral("uiTestScenarioMenu"));
    require(modeAction && badge && scenarioMenu, "UI test menu actions and title badge exist");
    modeAction->trigger();
    processEvents();
    require(modeAction->isChecked(), "UI test mode action becomes checked");
    require(!badge->isHidden(), "persistent UI test badge is visible");
    require(scenarioMenu->isEnabled(), "scenario menu is enabled in UI test mode");
    require(VaporViewTest::processEventsUntil(1500, [homeConfigCard]() {
                return homeTelemetrySummaryShowsUiTestRates(homeConfigCard);
            }),
            "UI test mode feeds representative home telemetry summary capsules");
    requireUiTestHomeTelemetryCapsulesCovered(
        homeConfigCard,
        "UI-test home telemetry summary capsules are covered in Chinese");

    auto findTitleMenuRow = [](QWidget *menu, const QStringList& texts) -> QWidget * {
        if (!menu)
        {
            return nullptr;
        }
        for (QToolButton *row : menu->findChildren<QToolButton *>())
        {
            if (!row || !row->property("titleApplicationMenuItem").toBool())
            {
                continue;
            }
            auto *label = row->findChild<QLabel *>(QStringLiteral("titleApplicationMenuText"));
            if (label && texts.contains(label->text()))
            {
                return row;
            }
        }
        return nullptr;
    };
    auto hoverTitleMenuRow = [](QWidget *row) {
        QEvent enter(QEvent::Enter);
        QApplication::sendEvent(row, &enter);
        processEvents();
    };

    auto *titleMenuButton = window->findChild<QToolButton *>(QStringLiteral("titleBarMenuButton"));
    require(titleMenuButton, "title application menu button exists in UI test mode");
    titleMenuButton->click();
    processEvents();
    auto *titleApplicationMainMenu =
        window->findChild<QFrame *>(QStringLiteral("titleApplicationMainMenu"));
    QWidget *developerRow = findTitleMenuRow(
        titleApplicationMainMenu,
        QStringList{QStringLiteral("开发者"), QStringLiteral("Developer")});
    require(developerRow, "title application menu exposes the Developer row");
    hoverTitleMenuRow(developerRow);
    auto *titleApplicationSubMenu =
        window->findChild<QFrame *>(QStringLiteral("titleApplicationSubMenu"));
    QWidget *scenarioRow = findTitleMenuRow(
        titleApplicationSubMenu,
        QStringList{QStringLiteral("界面测试场景"), QStringLiteral("UI Test Scenario")});
    require(scenarioRow && scenarioRow->isEnabled(),
            "Developer submenu exposes the enabled UI test scenario row");
    hoverTitleMenuRow(scenarioRow);
    auto *titleApplicationNestedMenu =
        window->findChild<QFrame *>(QStringLiteral("titleApplicationNestedMenu"));
    QList<QToolButton *> nestedScenarioRows;
    if (titleApplicationNestedMenu)
    {
        for (QToolButton *row : titleApplicationNestedMenu->findChildren<QToolButton *>(
                 QString(), Qt::FindDirectChildrenOnly))
        {
            if (row && row->property("titleApplicationMenuItem").toBool())
            {
                nestedScenarioRows.push_back(row);
            }
        }
    }
    require(!nestedScenarioRows.isEmpty() &&
                std::abs(nestedScenarioRows.first()->mapToGlobal(QPoint(0, 0)).y() -
                         scenarioRow->mapToGlobal(QPoint(0, 0)).y()) <= 1,
            "UI test scenario tertiary first row aligns with its secondary source row");
    titleMenuButton->click();
    processEvents();

    require(rtkServer->text() == QStringLiteral("persisted.ui-test.caster") &&
                rtkPort->text() == QStringLiteral("2201") &&
                rtkUsername->text() == QStringLiteral("persisted-user") &&
                rtkPassword->text() == QStringLiteral("persisted-password") &&
                rtkMountpoint->currentText() == QStringLiteral("PERSISTED_MOUNTPOINT"),
            "UI test mode reloads the real RTK profile as its sandbox baseline");
    QToolButton *epsilonAction = nullptr;
    for (QToolButton *button : window->findChildren<QToolButton *>(QStringLiteral("homeDeviceActionButton")))
    {
        if (!button->property("deviceConfigAction").toBool() &&
            button->toolTip().contains(QStringLiteral("EPSILON")))
        {
            epsilonAction = button;
            break;
        }
    }
    require(epsilonAction, "EPSILON home connection action exists");
    require(epsilonAction->isEnabled() &&
                epsilonAction->property("state").toString() == QStringLiteral("connected"),
            "EPSILON home action starts enabled and connected");
    epsilonAction->click();
    processEvents();
    require(epsilonAction->isEnabled() &&
                epsilonAction->property("state").toString() == QStringLiteral("disconnected"),
            "a disconnected UI-test device remains available for reconnect");
    epsilonAction->click();
    processEvents();
    require(!epsilonAction->isEnabled() &&
                epsilonAction->property("state").toString() == QStringLiteral("connecting"),
            "UI-test reconnect enters the temporary connecting state");
    require(VaporViewTest::processEventsUntil(2000, [epsilonAction]() {
                return epsilonAction->isEnabled() &&
                    epsilonAction->property("state").toString() == QStringLiteral("connected");
            }),
            "UI-test reconnect finishes and restores the connected action style");
    QLabel *ai8HomeCapsule = nullptr;
    for (QLabel *capsule : window->findChildren<QLabel *>(QStringLiteral("homeDeviceStatusCapsule")))
    {
        if (capsule->text().contains(QStringLiteral("AI-8288八路温控")))
        {
            ai8HomeCapsule = capsule;
            break;
        }
    }
    QToolButton *ai8HomeAction = nullptr;
    QToolButton *ai8DeviceAction = nullptr;
    for (QToolButton *button : window->findChildren<QToolButton *>())
    {
        if (button->property("deviceConfigAction").toBool() &&
            button->toolTip().contains(QStringLiteral("AI-8288")))
        {
            ai8DeviceAction = button;
        }
        if (!button->property("deviceConfigAction").toBool() &&
            button->objectName() == QStringLiteral("homeDeviceActionButton") &&
            button->toolTip().contains(QStringLiteral("AI-8288")))
        {
            ai8HomeAction = button;
        }
    }
    require(ai8HomeCapsule != nullptr,
            "AI-8288 home status capsule exists in UI test mode");
    require(ai8HomeAction != nullptr && ai8HomeAction->isEnabled() &&
                ai8HomeAction->property("state").toString() == QStringLiteral("connected"),
            "AI-8288 home connection action starts connected in UI test mode");
    require(ai8DeviceAction != nullptr && ai8DeviceAction->isEnabled(),
            "AI-8288 device configuration connection action exists and is enabled");
    auto *ai8TitleAction = window->findChild<QToolButton *>(QStringLiteral("ai8TitleActionButton"));
    require(ai8TitleAction != nullptr &&
                window->findChild<QToolButton *>(QStringLiteral("ai8TitleConnectButton")) == nullptr &&
                window->findChild<QToolButton *>(QStringLiteral("ai8TitleDisconnectButton")) == nullptr &&
                ai8TitleAction->property("temperatureTitleCommand").toString() == QStringLiteral("disconnect"),
            "AI-8288 temperature title uses one connected-state icon action");
    ai8HomeAction->click();
    processEvents();
    require(ai8HomeAction->isEnabled() &&
                ai8HomeAction->property("state").toString() == QStringLiteral("disconnected"),
            "AI-8288 home action supports simulated disconnect");
    require(ai8TitleAction->isEnabled() &&
                ai8TitleAction->property("temperatureTitleCommand").toString() == QStringLiteral("connect"),
            "AI-8288 temperature title reuses the same icon action for reconnect");
    ai8HomeAction->click();
    require(VaporViewTest::processEventsUntil(900, [ai8HomeAction, ai8DeviceAction, ai8TitleAction]() {
                return !ai8HomeAction->isEnabled() &&
                    ai8HomeAction->property("state").toString() == QStringLiteral("connecting") &&
                    !ai8DeviceAction->isEnabled() &&
                    ai8DeviceAction->property("state").toString() == QStringLiteral("connecting") &&
                    !ai8TitleAction->isEnabled() &&
                    ai8TitleAction->property("state").toString() == QStringLiteral("connecting") &&
                    ai8TitleAction->property("temperatureTitleCommand").toString() ==
                        QStringLiteral("disconnect");
            }),
            "AI-8288 connection icons keep the spinner state briefly after fast reconnect");
    require(VaporViewTest::processEventsUntil(2000, [ai8HomeAction]() {
                return ai8HomeAction->isEnabled() &&
                    ai8HomeAction->property("state").toString() == QStringLiteral("connected");
            }),
            "AI-8288 home action supports simulated reconnect");
    require(ai8TitleAction->isEnabled() &&
                ai8TitleAction->property("temperatureTitleCommand").toString() == QStringLiteral("disconnect"),
            "AI-8288 temperature title icon action returns to disconnect state after reconnect");

    ai8TitleAction->setEnabled(false);
    ai8TitleAction->setProperty("state", QStringLiteral("connecting"));
    ai8TitleAction->setProperty("temperatureTitleCommand", QStringLiteral("connect"));
    auto *ai8PanelForTitleRefresh =
        window->findChild<VaporView::Ground::Widgets::Ai8TemperatureControllerPanel *>(
            QStringLiteral("ai8TemperatureControllerPanel"));
    require(ai8PanelForTitleRefresh != nullptr,
            "AI-8 panel is available for title action refresh regression");
    VaporView::Ai8TemperatureControllerProtocol::LiveData ai8TitleRefreshData;
    ai8TitleRefreshData.valid = true;
    ai8TitleRefreshData.controlStatesValid = true;
    ai8TitleRefreshData.controlStates.fill(
        VaporView::Ai8TemperatureControllerProtocol::ChannelControlState::ApidOutput);
    ai8PanelForTitleRefresh->applyLiveData(ai8TitleRefreshData);
    processEvents();
    require(ai8TitleAction->isEnabled() &&
                ai8TitleAction->property("state").toString() == QStringLiteral("connected") &&
                ai8TitleAction->property("temperatureTitleCommand").toString() == QStringLiteral("disconnect"),
            "AI-8288 live data refresh restores a stale title action from connecting to disconnect");

    auto *temperaturePage = window->findChild<QWidget *>(QStringLiteral("temperaturePage"));
    auto *ai8Panel = window->findChild<QWidget *>(QStringLiteral("ai8TemperatureControllerPanel"));
    auto *ai8Plot = window->findChild<QWidget *>(QStringLiteral("ai8TemperatureTrendPlot"));
    auto *ai8ParameterStack = window->findChild<QStackedWidget *>(QStringLiteral("ai8ParameterStack"));
    auto *ai8ProtocolStatus = window->findChild<QLabel *>(QStringLiteral("ai8ProtocolStatus"));
    auto *ai8ChannelSpin = window->findChild<QSpinBox *>(QStringLiteral("ai8ChannelSpin"));
    auto *ai8MeasuredTemperature = window->findChild<QLineEdit *>(
        QStringLiteral("ai8MeasuredTemperatureEdit"));
    auto *ai8GlobalButton = window->findChild<QPushButton *>(QStringLiteral("ai8PageSelectorButton4"));
    auto *ai8ChannelButton = window->findChild<QPushButton *>(QStringLiteral("ai8PageSelectorButton1"));
    require(mainPageStack && temperaturePage && ai8Panel && ai8Plot && ai8ParameterStack &&
                ai8ProtocolStatus && ai8ChannelSpin && ai8MeasuredTemperature &&
                ai8GlobalButton && ai8ChannelButton,
            "UI test mode exposes the AI-8 temperature page and controls");
    mainPageStack->setCurrentWidget(temperaturePage);
    require(VaporViewTest::processEventsUntil(1500, [ai8Plot]() {
                return ai8Plot->isVisible() && ai8Plot->property("sampleCount").toInt() >= 2;
            }),
            "UI test mode continuously feeds AI-8 samples into the temperature trend plot");
    require(ai8Panel->isVisible() && ai8ProtocolStatus->property("protocolReady").toBool() &&
                ai8ProtocolStatus->text().contains(QStringLiteral("UI-TEST-AI8")),
            "AI-8 panel receives the simulated connected backend status");
    require(ai8Plot->property("sampleCount").toInt() >= 2 &&
                ai8Plot->property("axisLabelsVisible").toBool() &&
                ai8Plot->property("yAxisTickCount").toInt() == 7 &&
                ai8Plot->property("xAxisTickCount").toInt() == 5 &&
                ai8Plot->property("yAxisMaxC").toDouble() > ai8Plot->property("yAxisMinC").toDouble(),
            "AI-8 trend plot exposes populated samples and numeric axes in UI test mode");
    ai8ChannelSpin->setValue(8);
    processEvents();
    require(ai8MeasuredTemperature->text() != QStringLiteral("---") &&
                ai8MeasuredTemperature->text().contains(QStringLiteral("°C")) &&
                ai8Plot->property("sampleCount").toInt() > 0,
            "AI-8 UI test data follows the selected eighth channel");
    ai8GlobalButton->click();
    processEvents();
    require(ai8ParameterStack->currentIndex() == 3 && ai8GlobalButton->isChecked(),
            "AI-8 UI test page navigation reaches global parameters");
    ai8ChannelButton->click();
    processEvents();
    require(ai8ParameterStack->currentIndex() == 0 && ai8ChannelButton->isChecked(),
            "AI-8 UI test page navigation returns to channel parameters");
    const QImage ai8PlotImage = ai8Plot->grab().toImage().convertToFormat(QImage::Format_ARGB32);
    const QColor expectedSeriesColor = VaporView::appThemeColor(
        VaporView::AppThemeColor::PlotSeriesTemperature, VaporView::isDarkThemeEnabled());
    int seriesPixelCount = 0;
    for (int y = 0; y < ai8PlotImage.height(); ++y)
    {
        for (int x = 0; x < ai8PlotImage.width(); ++x)
        {
            const QColor pixel = ai8PlotImage.pixelColor(x, y);
            if (std::abs(pixel.red() - expectedSeriesColor.red()) <= 8 &&
                std::abs(pixel.green() - expectedSeriesColor.green()) <= 8 &&
                std::abs(pixel.blue() - expectedSeriesColor.blue()) <= 8)
            {
                ++seriesPixelCount;
            }
        }
    }
    require(!ai8PlotImage.isNull() && ai8PlotImage.width() > 0 && ai8PlotImage.height() > 0 &&
                seriesPixelCount > 0,
            "AI-8 trend plot renders the temperature series in a QWidget snapshot");
    QDialog testCreatedAuxiliary;
    testCreatedAuxiliary.show();
    processEvents();
    require(testCreatedAuxiliary.isVisible(), "test-created auxiliary window is visible during UI test mode");
    TcpWavePanel *wavePanel = window->findChild<TcpWavePanel *>();
    RtkConfigDialog *rtkDialog = window->findChild<RtkConfigDialog *>();
    require(wavePanel && rtkDialog, "TCP waveform and RTK test-session participants exist");
    require(wavePanel->isConnected(), "TCP waveform panel starts connected in UI test mode");
    wavePanel->toggleConnection();
    require(!wavePanel->isConnected(), "TCP waveform disconnect is simulated in memory");
    wavePanel->toggleConnection();
    require(wavePanel->isConnected(), "TCP waveform reconnect is simulated in memory");

    QTcpServer mountpointCaster;
    require(mountpointCaster.listen(QHostAddress::LocalHost, 0),
            "UI-test real mountpoint caster starts");
    bool mountpointRequestReceived = false;
    QObject::connect(&mountpointCaster, &QTcpServer::newConnection,
                     [&mountpointCaster, &mountpointRequestReceived]() {
        while (QTcpSocket *socket = mountpointCaster.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &mountpointRequestReceived]() {
                if (socket->readAll().isEmpty())
                {
                    return;
                }
                mountpointRequestReceived = true;
                const QByteArray body =
                    "STR;PERSISTED_MOUNTPOINT;Saved mountpoint;RTCM 3;1004(1);2;GPS;NONE;B;N;0;0;VaporView;none;B;N;0;\r\n"
                    "STR;REAL_UI_TEST_MOUNTPOINT;UI test mountpoint;RTCM 3;1004(1);2;GPS;NONE;B;N;0;0;VaporView;none;B;N;0;\r\n"
                    "ENDSOURCETABLE\r\n";
                const QByteArray response =
                    "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\n\r\n" + body;
                socket->write(response);
                socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    auto *rtkServiceLog = rtkDialog->findChild<QTextEdit *>(QStringLiteral("rtkServiceLogTextEdit"));
    require(rtkServiceLog, "RTK service log exists");
    rtkServer->setText(QStringLiteral("127.0.0.1"));
    rtkPort->setText(QString::number(mountpointCaster.serverPort()));
    require(QMetaObject::invokeMethod(rtkDialog, "onFetchMountpointsClicked", Qt::DirectConnection),
            "RTK real mountpoint request invoked in UI test mode");
    require(VaporViewTest::processEventsUntil(5000, [rtkDialog, rtkMountpoint, rtkServiceLog, &mountpointRequestReceived]() {
                const QString log = rtkServiceLog->toPlainText();
                return mountpointRequestReceived && !rtkDialog->hasActiveExternalOperation() &&
                    rtkMountpoint->findText(QStringLiteral("REAL_UI_TEST_MOUNTPOINT")) >= 0 &&
                    (log.contains(QStringLiteral("[界面测试] 已从真实源表获取")) ||
                     log.contains(QStringLiteral("[界面测试] Fetched")));
            }),
            "UI-test mountpoint detection sends a real sourcetable request");
    auto *ggaMonitorLog = rtkDialog->findChild<QTextEdit *>(QStringLiteral("rtkGgaTextEdit"));
    require(ggaMonitorLog, "RTK GGA monitor output exists");
    ggaMonitorLog->clear();
    require(QMetaObject::invokeMethod(rtkDialog, "onGgaToggleClicked", Qt::DirectConnection),
            "RTK simulated GGA monitor starts");
    require(VaporViewTest::processEventsUntil(2500, [ggaMonitorLog]() {
                return ggaMonitorLog->toPlainText().count(QStringLiteral("$GPGGA,")) >= 3;
            }),
            "RTK simulated GGA monitor continuously appends one-hertz data");
    require(QMetaObject::invokeMethod(rtkDialog, "onGgaToggleClicked", Qt::DirectConnection),
            "RTK simulated GGA monitor stops");
    const int stoppedGgaCount = ggaMonitorLog->toPlainText().count(QStringLiteral("$GPGGA,"));
    require(!VaporViewTest::processEventsUntil(1200, [ggaMonitorLog, stoppedGgaCount]() {
                return ggaMonitorLog->toPlainText().count(QStringLiteral("$GPGGA,")) > stoppedGgaCount;
            }),
            "RTK simulated GGA monitor stops appending after the user stops it");

    QTcpServer ntripValidationCaster;
    require(ntripValidationCaster.listen(QHostAddress::LocalHost, 0),
            "UI-test real NTRIP validation caster starts");
    bool ntripRequestReceived = false;
    QObject::connect(&ntripValidationCaster, &QTcpServer::newConnection,
                     [&ntripValidationCaster, &ntripRequestReceived]() {
        while (QTcpSocket *socket = ntripValidationCaster.nextPendingConnection())
        {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &ntripRequestReceived]() {
                if (socket->readAll().isEmpty())
                {
                    return;
                }
                ntripRequestReceived = true;
                if (socket->property("sentHeader").toBool())
                {
                    return;
                }
                socket->setProperty("sentHeader", true);
                socket->write("ICY 200 OK\r\n\r\n");
                auto *burstTimer = new QTimer(socket);
                burstTimer->setInterval(50);
                QObject::connect(burstTimer, &QTimer::timeout, socket, [socket, burstTimer]() {
                    const int count = socket->property("burstCount").toInt();
                    if (count >= 12)
                    {
                        burstTimer->stop();
                        socket->disconnectFromHost();
                        return;
                    }
                    socket->write(QByteArray(48, '\xD3'));
                    socket->flush();
                    socket->setProperty("burstCount", count + 1);
                });
                burstTimer->start();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    require(rtkServer && rtkPort && rtkMountpoint && rtkServiceLog,
            "RTK sandbox fields and service log exist");
    rtkServer->setText(QStringLiteral("127.0.0.1"));
    rtkPort->setText(QString::number(ntripValidationCaster.serverPort()));
    rtkMountpoint->setCurrentText(QStringLiteral("REAL_UI_TEST_MOUNTPOINT"));
    require(QMetaObject::invokeMethod(rtkDialog, "onTestClicked", Qt::DirectConnection),
            "RTK real NTRIP validation action invoked in UI test mode");
    require(VaporViewTest::processEventsUntil(6000, [rtkDialog, rtkServiceLog, &ntripRequestReceived]() {
                const QString log = rtkServiceLog->toPlainText();
                return ntripRequestReceived && !rtkDialog->hasActiveExternalOperation() &&
                    (log.contains(QStringLiteral("[界面测试] 真实 NTRIP 验证成功")) ||
                     log.contains(QStringLiteral("[界面测试] Real NTRIP validation succeeded")));
            }),
            "UI-test NTRIP validation sends a real request and receives RTCM locally");
    require(QMetaObject::invokeMethod(rtkDialog, "onStartClicked", Qt::DirectConnection),
            "RTK simulated start action invoked");
    require(rtkDialog->isRunning(), "RTK simulated service enters running state");
    require(QMetaObject::invokeMethod(rtkDialog, "onStopClicked", Qt::DirectConnection),
            "RTK simulated stop action invoked");

    VaporView::SkyDeviceConfigDialog skyDialog(nullptr);
    skyDialog.setUiTestMode(true);
    require(QMetaObject::invokeMethod(&skyDialog, "onReadClicked", Qt::DirectConnection),
            "Sky fixed configuration read action invoked");
    require(QMetaObject::invokeMethod(&skyDialog, "onApplyClicked", Qt::DirectConnection),
            "Sky configuration validation action invoked");
    require(QMetaObject::invokeMethod(&skyDialog, "onSaveClicked", Qt::DirectConnection),
            "Sky simulated save action invoked");

    QAction *partialFailureAction = nullptr;
    QAction *stalledAction = nullptr;
    for (QAction *action : scenarioMenu->actions())
    {
        if (action->data().toInt() == 1) partialFailureAction = action;
        if (action->data().toInt() == 2) stalledAction = action;
    }
    require(partialFailureAction && stalledAction, "all UI test scenarios are present");
    partialFailureAction->trigger();
    stalledAction->trigger();
    processEvents();

    auto *recordingCard = window->findChild<QFrame *>(QStringLiteral("recordingStatusCard"));
    auto *recordingStatus = window->findChild<QLabel *>(QStringLiteral("recordingStatusLabel"));
    require(recordingCard && recordingStatus, "recording status card exists");
    QLabel *recordingTitle = nullptr;
    for (QLabel *label : recordingCard->findChildren<QLabel *>())
    {
        if (label->text().contains(QStringLiteral("记录状态")))
        {
            recordingTitle = label;
            break;
        }
    }
    require(recordingTitle && recordingTitle->text() == QStringLiteral("记录状态（界面测试）"),
            "recording card title identifies UI test mode");
    require(QMetaObject::invokeMethod(window, "onStartRecordingClicked", Qt::DirectConnection),
            "simulated recording start slot invoked");
    require(VaporViewTest::processEventsUntil(1500, [recordingStatus]() {
                return recordingStatus->text().contains(QStringLiteral("记录：进行中（界面测试）")) &&
                    recordingStatus->text().contains(QStringLiteral("会话：UI-TEST-SESSION")) &&
                    recordingStatus->text().contains(QStringLiteral("设备行数：")) &&
                    !recordingStatus->text().contains(QStringLiteral("设备行数：0\n")) &&
                    recordingStatus->text().contains(QStringLiteral("文件写入：无（仅内存模拟）"));
            }),
            "simulated recording displays deterministic in-memory counters");
    require(QMetaObject::invokeMethod(window, "onPauseRecordingClicked", Qt::DirectConnection),
            "simulated recording pause slot invoked");
    processEvents();
    const QString pausedRecordingText = recordingStatus->text();
    require(pausedRecordingText.contains(QStringLiteral("记录：已暂停（界面测试）")),
            "simulated recording displays the paused UI-test state");
    require(!VaporViewTest::processEventsUntil(400, [recordingStatus, pausedRecordingText]() {
                return recordingStatus->text() != pausedRecordingText;
            }),
            "simulated recording counters freeze while paused");
    require(QMetaObject::invokeMethod(window, "onStartRecordingClicked", Qt::DirectConnection),
            "simulated recording resume slot invoked");
    require(VaporViewTest::processEventsUntil(1500, [recordingStatus, pausedRecordingText]() {
                return recordingStatus->text().contains(QStringLiteral("记录：进行中（界面测试）")) &&
                    recordingStatus->text() != pausedRecordingText;
            }),
            "simulated recording counters resume without starting the real recorder");
    require(QMetaObject::invokeMethod(window, "onStopRecordingClicked", Qt::DirectConnection),
            "simulated recording stop slot invoked");
    processEvents();
    require(recordingStatus->text().contains(QStringLiteral("记录：未记录（界面测试）")) &&
                recordingStatus->text().contains(QStringLiteral("设备行数：0")) &&
                recordingStatus->text().contains(QStringLiteral("文件写入：无（仅内存模拟）")),
            "stopping simulated recording clears only its in-memory counters");
    require(QMetaObject::invokeMethod(window, "onDisconnectClicked", Qt::DirectConnection),
            "simulated disconnect slot invoked");
    require(QMetaObject::invokeMethod(window, "onConnectClicked", Qt::DirectConnection),
            "simulated connect slot invoked");
    require(QMetaObject::invokeMethod(window, "onCancelConnectClicked", Qt::DirectConnection),
            "simulated cancel slot invoked");
    require(QMetaObject::invokeMethod(window, "onRefreshPortsClicked", Qt::DirectConnection),
            "fixed-port refresh slot invoked");
    processEvents();
    require(snapshotAll() == before, "all settings namespaces remain byte-for-byte equivalent during UI test mode");
    require(!QDir(settingsDirectory.filePath(QStringLiteral("business-output"))).exists(),
            "simulated recording did not create its configured business directory");

    modeAction->trigger();
    processEvents();
    require(!modeAction->isChecked(), "UI test mode action clears after exit");
    require(badge->isHidden(), "UI test badge hides after exit");
    require(!scenarioMenu->isEnabled(), "scenario menu is disabled after exit");
    require(!testCreatedAuxiliary.isVisible(), "test-created auxiliary window closes on UI test exit");
    require(snapshotAll() == before, "all settings namespaces remain unchanged after normal UI test exit");
    require(epsilonPort->currentText() == QStringLiteral("UNSAVED-COM42") &&
                rtkServer->text() == QStringLiteral("normal.unpersisted.caster"),
            "unsaved normal-mode control values are restored after UI test mode");
    QStringList epsilonItemsAfter;
    for (int index = 0; index < epsilonPort->count(); ++index)
    {
        epsilonItemsAfter.push_back(epsilonPort->itemText(index));
    }
    require(epsilonItemsAfter == epsilonItemsBefore,
            "normal-mode serial choices are restored without UI-test entries");
    window->close();
    delete window;
    const auto beforeDirectClose = snapshotAll();

    auto *directCloseWindow = new MainWindow();
    directCloseWindow->show();
    processEvents();
    QAction *directCloseModeAction = directCloseWindow->findChild<QAction *>(QStringLiteral("uiTestModeAction"));
    require(directCloseModeAction, "UI test action exists after recreating main window");
    directCloseModeAction->trigger();
    processEvents();
    directCloseWindow->close();
    delete directCloseWindow;
    require(VaporView::settingsWritesSuspended(), "direct close keeps the write barrier active through destruction");
    require(snapshotAll() == beforeDirectClose, "direct close from UI test mode does not persist destructor state");
    VaporView::setSettingsWritesSuspended(false);

    {
        VaporView::setSettingsWritesSuspended(true);
        QTcpServer localWaveSource;
        require(localWaveSource.listen(QHostAddress::LocalHost),
                "local TCP waveform source starts for title-bar connection coverage");
        auto *connectionWindow = new MainWindow();
        connectionWindow->show();
        processEvents();
        VaporView::Ground::Widgets::SegmentedSwitchButton *connectionSourceModeSwitch = nullptr;
        for (auto *candidate : connectionWindow->findChildren<VaporView::Ground::Widgets::SegmentedSwitchButton *>())
        {
            if ((candidate->leftSegmentText().contains(QStringLiteral("本地")) ||
                 candidate->leftSegmentText().contains(QStringLiteral("Local"))) &&
                (candidate->rightSegmentText().contains(QStringLiteral("远程")) ||
                 candidate->rightSegmentText().contains(QStringLiteral("Remote"))))
            {
                connectionSourceModeSwitch = candidate;
                break;
            }
        }
        require(connectionSourceModeSwitch != nullptr,
                "normal-mode source mode switch exists");
        if (connectionSourceModeSwitch->switchChecked())
        {
            connectionSourceModeSwitch->click();
            processEvents();
        }
        auto *connectionWavePanel = connectionWindow->findChild<TcpWavePanel *>();
        require(connectionWavePanel != nullptr,
                "normal-mode window exposes its TCP waveform panel");
        auto *connectionLogList = connectionWindow->findChild<QListView *>(QStringLiteral("logListView"));
        require(connectionLogList && connectionLogList->model(),
                "normal-mode window exposes the log model");
        const QList<QLineEdit *> waveInputs = connectionWavePanel->findChildren<QLineEdit *>();
        require(waveInputs.size() >= 2,
                "normal-mode TCP waveform panel exposes host and port inputs");
        waveInputs.at(0)->setText(QStringLiteral("127.0.0.1"));
        waveInputs.at(1)->setText(QString::number(localWaveSource.serverPort()));
        for (const QString& objectName : {
                 QStringLiteral("epsilonPortCombo"),
                 QStringLiteral("pressurePortCombo"),
                 QStringLiteral("humidityPortCombo"),
                 QStringLiteral("lidarPortCombo"),
                 QStringLiteral("temperaturePortCombo"),
                 QStringLiteral("deviceAi8TemperaturePortCombo")})
        {
            if (QComboBox *combo = connectionWindow->findChild<QComboBox *>(objectName))
            {
                combo->setCurrentIndex(0);
            }
        }
        VaporView::setSettingsWritesSuspended(false);
        require(QMetaObject::invokeMethod(connectionWindow, "onConnectClicked", Qt::DirectConnection),
                "normal-mode title-bar connection slot invoked");
        VaporView::setSettingsWritesSuspended(true);
        require(VaporViewTest::processEventsUntil(3000, [connectionWavePanel, connectionLogList]() {
                    return connectionWavePanel->isConnected() &&
                        findLogMessageRow(connectionLogList,
                                          QStringLiteral("连接摘要"),
                                          QStringLiteral("Connection Summary")) >= 0 &&
                        findLogMessageRow(connectionLogList,
                                          QStringLiteral("串口设备阶段"),
                                          QStringLiteral("Serial device phase")) >= 0 &&
                        findLogMessageRow(connectionLogList,
                                          QStringLiteral("正在连接 TCP 波形"),
                                          QStringLiteral("Connecting TCP wave link")) >= 0 &&
                        findLogMessageRow(connectionLogList,
                                          QStringLiteral("TCP 波形已连接"),
                                          QStringLiteral("TCP wave link connected")) >= 0;
                }),
                "title-bar connection connects the local TCP waveform source and flushes its logs");
        const int summaryRow = findLogMessageRow(connectionLogList,
                                                 QStringLiteral("连接摘要"),
                                                 QStringLiteral("Connection Summary"));
        const int serialPhaseRow = findLogMessageRow(connectionLogList,
                                                      QStringLiteral("串口设备阶段"),
                                                      QStringLiteral("Serial device phase"));
        const int waveformConnectingRow = findLogMessageRow(connectionLogList,
                                                             QStringLiteral("正在连接 TCP 波形"),
                                                             QStringLiteral("Connecting TCP wave link"));
        const int waveformConnectedRow = findLogMessageRow(connectionLogList,
                                                            QStringLiteral("TCP 波形已连接"),
                                                            QStringLiteral("TCP wave link connected"));
        require(serialPhaseRow >= 0 && waveformConnectingRow > serialPhaseRow,
                "title-bar waveform connection starts after the serial device phase");
        require(waveformConnectedRow == waveformConnectingRow + 1,
                "TCP waveform connection logs remain adjacent");
        require(summaryRow > waveformConnectedRow,
                "connection summary follows both waveform logs");
        require(QMetaObject::invokeMethod(connectionWindow, "onDisconnectClicked", Qt::DirectConnection),
                "normal-mode title-bar disconnect slot invoked");
        require(VaporViewTest::processEventsUntil(1500, [connectionWavePanel]() {
                    return !connectionWavePanel->isConnected();
                }),
                "title-bar disconnect also disconnects the local TCP waveform source");
        connectionWindow->close();
        delete connectionWindow;
        VaporView::setSettingsWritesSuspended(false);

        VaporView::setSettingsWritesSuspended(true);
        QTcpServer reservedClosedPort;
        require(reservedClosedPort.listen(QHostAddress::LocalHost),
                "closed TCP waveform port can be reserved");
        const quint16 closedWavePort = reservedClosedPort.serverPort();
        reservedClosedPort.close();

        auto *failedConnectionWindow = new MainWindow();
        failedConnectionWindow->show();
        processEvents();
        VaporView::Ground::Widgets::SegmentedSwitchButton *failedSourceModeSwitch = nullptr;
        for (auto *candidate : failedConnectionWindow->findChildren<VaporView::Ground::Widgets::SegmentedSwitchButton *>())
        {
            if ((candidate->leftSegmentText().contains(QStringLiteral("本地")) ||
                 candidate->leftSegmentText().contains(QStringLiteral("Local"))) &&
                (candidate->rightSegmentText().contains(QStringLiteral("远程")) ||
                 candidate->rightSegmentText().contains(QStringLiteral("Remote"))))
            {
                failedSourceModeSwitch = candidate;
                break;
            }
        }
        require(failedSourceModeSwitch != nullptr,
                "failed-path normal-mode source mode switch exists");
        if (failedSourceModeSwitch->switchChecked())
        {
            failedSourceModeSwitch->click();
            processEvents();
        }
        auto *failedWavePanel = failedConnectionWindow->findChild<TcpWavePanel *>();
        require(failedWavePanel != nullptr,
                "failed-path normal-mode window exposes its TCP waveform panel");
        auto *failedLogList = failedConnectionWindow->findChild<QListView *>(QStringLiteral("logListView"));
        require(failedLogList && failedLogList->model(),
                "failed-path normal-mode window exposes the log model");
        const QList<QLineEdit *> failedWaveInputs = failedWavePanel->findChildren<QLineEdit *>();
        require(failedWaveInputs.size() >= 2,
                "failed-path TCP waveform panel exposes host and port inputs");
        failedWaveInputs.at(0)->setText(QStringLiteral("127.0.0.1"));
        failedWaveInputs.at(1)->setText(QString::number(closedWavePort));
        for (const QString& objectName : {
                 QStringLiteral("epsilonPortCombo"),
                 QStringLiteral("pressurePortCombo"),
                 QStringLiteral("humidityPortCombo"),
                 QStringLiteral("lidarPortCombo"),
                 QStringLiteral("temperaturePortCombo"),
                 QStringLiteral("deviceAi8TemperaturePortCombo")})
        {
            if (QComboBox *combo = failedConnectionWindow->findChild<QComboBox *>(objectName))
            {
                combo->setCurrentIndex(0);
            }
        }
        VaporView::setSettingsWritesSuspended(false);
        require(QMetaObject::invokeMethod(failedConnectionWindow, "onConnectClicked", Qt::DirectConnection),
                "failed-path normal-mode title-bar connection slot invoked");
        VaporView::setSettingsWritesSuspended(true);
        const bool failedLogsFlushed = VaporViewTest::processEventsUntil(5000, [failedLogList]() {
            return findLogMessageRow(failedLogList,
                                     QStringLiteral("没有串口设备连接成功"),
                                     QStringLiteral("No serial devices connected")) >= 0 &&
                findLogMessageRow(failedLogList,
                                  QStringLiteral("正在连接 TCP 波形"),
                                  QStringLiteral("Connecting TCP wave link")) >= 0 &&
                findLogMessageRow(failedLogList,
                                  QStringLiteral("TCP 波形 socket 错误"),
                                  QStringLiteral("TCP wave socket error")) >= 0 &&
                findLogMessageRow(failedLogList,
                                  QStringLiteral("连接摘要"),
                                  QStringLiteral("Connection Summary")) >= 0;
        });
        require(failedLogsFlushed,
                "failed-path title-bar connection flushes serial, TCP error, and summary logs");
        const int failedNoSerialRow = findLogMessageRow(failedLogList,
                                                        QStringLiteral("没有串口设备连接成功"),
                                                        QStringLiteral("No serial devices connected"));
        const int failedWaveformConnectingRow = findLogMessageRow(failedLogList,
                                                                  QStringLiteral("正在连接 TCP 波形"),
                                                                  QStringLiteral("Connecting TCP wave link"));
        const int failedWaveformErrorRow = findLogMessageRow(failedLogList,
                                                             QStringLiteral("TCP 波形 socket 错误"),
                                                             QStringLiteral("TCP wave socket error"));
        const int failedSummaryRow = findLogMessageRow(failedLogList,
                                                       QStringLiteral("连接摘要"),
                                                       QStringLiteral("Connection Summary"));
        require(failedNoSerialRow >= 0 && failedWaveformConnectingRow > failedNoSerialRow,
                "failed-path TCP waveform connection starts after the no-serial-device log");
        require(failedWaveformErrorRow > failedWaveformConnectingRow,
                "failed-path TCP waveform error follows its connecting log");
        require(failedSummaryRow > failedWaveformErrorRow,
                "failed-path connection summary follows the TCP waveform error");
        failedConnectionWindow->close();
        delete failedConnectionWindow;
        VaporView::setSettingsWritesSuspended(false);
    }
    require(snapshotAll() == beforeDirectClose,
            "normal-mode waveform connection coverage does not persist temporary inputs");

    std::cout << "ui_test_mode_window_test passed\n";
    return 0;
}
