#include "ground/main/MainWindow.h"
#include "ground/main/UiLogModel.h"
#include "ground/rtk/RtkConfigDialog.h"
#include "ground/wave/TcpWavePanel.h"
#include "ground/widgets/Ai8TemperatureControllerPanel.h"
#include "ground/widgets/SegmentedSwitchButton.h"
#include "ground/widgets/TemperatureTrendPlotWidget.h"
#include "LogService.h"
#include "shared/config/SettingsWriteBarrier.h"
#include "shared/theme/AppTheme.h"
#include "test_ui_helpers.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QColor>
#include <QDateTime>
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
#include <QMessageBox>
#include <QMetaObject>
#include <QMap>
#include <QPointer>
#include <QPlainTextEdit>
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
#include <set>
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

bool settingsSnapshotsEqual(const QMap<QString, SettingsSnapshot>& actual,
                            const QMap<QString, SettingsSnapshot>& expected,
                            const char *message)
{
    if (actual == expected)
    {
        return true;
    }

    std::cerr << "FAILED: " << message << '\n';
    std::set<QString> applications;
    for (auto it = actual.cbegin(); it != actual.cend(); ++it)
    {
        applications.insert(it.key());
    }
    for (auto it = expected.cbegin(); it != expected.cend(); ++it)
    {
        applications.insert(it.key());
    }
    for (const QString& application : applications)
    {
        const SettingsSnapshot actualValues = actual.value(application);
        const SettingsSnapshot expectedValues = expected.value(application);
        if (actualValues == expectedValues)
        {
            continue;
        }

        std::cerr << "settings diff [" << application.toStdString() << "]\n";
        std::set<QString> keys;
        for (auto it = actualValues.cbegin(); it != actualValues.cend(); ++it)
        {
            keys.insert(it.key());
        }
        for (auto it = expectedValues.cbegin(); it != expectedValues.cend(); ++it)
        {
            keys.insert(it.key());
        }
        for (const QString& key : keys)
        {
            const QVariant actualValue = actualValues.value(key);
            const QVariant expectedValue = expectedValues.value(key);
            if (actualValue == expectedValue)
            {
                continue;
            }
            std::cerr << "  " << key.toStdString()
                      << ": expected='" << expectedValue.toString().toStdString()
                      << "' actual='" << actualValue.toString().toStdString()
                      << "'\n";
        }
    }
    return false;
}

void processEvents()
{
    QApplication::processEvents(QEventLoop::AllEvents, 100);
}

int findLogEventRow(QListView *logList, const QString& event)
{
    if (!logList || !logList->model())
    {
        return -1;
    }
    for (int row = 0; row < logList->model()->rowCount(); ++row)
    {
        const QString rowEvent = logList->model()->index(row, 0)
            .data(VaporView::Ground::Main::UiLogModel::EventRole)
            .toString();
        if (rowEvent == event)
        {
            return row;
        }
    }
    return -1;
}

void dumpLogRows(QListView *logList, const char *prefix)
{
    if (!logList || !logList->model())
    {
        std::cerr << prefix << ": no log model\n";
        return;
    }
    std::cerr << prefix << ": rowCount=" << logList->model()->rowCount() << '\n';
    for (int row = 0; row < logList->model()->rowCount(); ++row)
    {
        const QModelIndex index = logList->model()->index(row, 0);
        std::cerr << "  row=" << row
                  << " event=" << index.data(VaporView::Ground::Main::UiLogModel::EventRole)
                                      .toString().toStdString()
                  << " message=" << index.data(VaporView::Ground::Main::UiLogModel::MessageRole)
                                        .toString().toStdString()
                  << '\n';
    }
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

    auto numericPrefix = [](const QString& text) {
        bool ok = false;
        const double value = text.section(QLatin1Char(' '), 0, 0).toDouble(&ok);
        return ok ? value : -1.0;
    };

    int threeDigitHzValues = 0;
    for (QLabel *valueLabel : sections.at(0)->findChildren<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel")))
    {
        const QString text = valueLabel->text();
        if (text.endsWith(QStringLiteral("Hz")) && numericPrefix(text) >= 100.0)
        {
            ++threeDigitHzValues;
        }
    }

    int threeDigitMbpsValues = 0;
    for (QLabel *valueLabel : sections.at(1)->findChildren<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel")))
    {
        const QString text = valueLabel->text();
        const double value = numericPrefix(text);
        if (text.contains(QStringLiteral("Mbps")) && value >= 100.0 && value < 1000.0)
        {
            ++threeDigitMbpsValues;
        }
    }

    bool sawAvailableData = false;
    for (QLabel *valueLabel : sections.at(2)->findChildren<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel")))
    {
        sawAvailableData = sawAvailableData ||
            valueLabel->text() == QStringLiteral("有") ||
            valueLabel->text() == QStringLiteral("Yes");
    }
    return threeDigitHzValues >= 6 && threeDigitMbpsValues == 3 && sawAvailableData;
}

bool homeTelemetrySummaryHasStableCompactTextGaps(QWidget *homeConfigCard)
{
    QWidget *summaryContainer = homeTelemetrySummaryContainer(homeConfigCard);
    const QList<QFrame *> sections = sortedHomeTelemetrySections(summaryContainer);
    if (sections.size() < 3)
    {
        return false;
    }

    for (QFrame *section : sections)
    {
        const QList<QFrame *> pills =
            section->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
        if (pills.isEmpty())
        {
            return false;
        }
        for (QFrame *pill : pills)
        {
            QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
            QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
            if (!nameLabel || !valueLabel)
            {
                return false;
            }
            const int nameTextRight = nameLabel->mapTo(pill, QPoint(0, 0)).x() +
                nameLabel->fontMetrics().horizontalAdvance(nameLabel->text());
            const int valueTextLeft = valueLabel->mapTo(pill, QPoint(0, 0)).x();
            const int gap = valueTextLeft - nameTextRight;
            if (gap < 3 || gap > 10)
            {
                return false;
            }
        }
    }
    return true;
}

QList<QFrame *> sortedHomeTelemetryPills(QWidget *homeConfigCard)
{
    QWidget *summaryContainer = homeTelemetrySummaryContainer(homeConfigCard);
    QList<QFrame *> result;
    for (QFrame *section : sortedHomeTelemetrySections(summaryContainer))
    {
        QList<QFrame *> sectionPills =
            section->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
        std::sort(sectionPills.begin(), sectionPills.end(), [section](QFrame *lhs, QFrame *rhs) {
            const QPoint lhsPos = lhs->mapTo(section, QPoint(0, 0));
            const QPoint rhsPos = rhs->mapTo(section, QPoint(0, 0));
            return std::make_tuple(lhsPos.y(), lhsPos.x()) <
                   std::make_tuple(rhsPos.y(), rhsPos.x());
        });
        result.append(sectionPills);
    }
    return result;
}

bool sameWidgetSet(const QList<QFrame *>& actual, const QList<QFrame *>& expected)
{
    if (actual.size() != expected.size())
    {
        return false;
    }
    for (QFrame *widget : expected)
    {
        if (!actual.contains(widget))
        {
            return false;
        }
    }
    return true;
}

QStringList homeTelemetryDynamicSummaryValues(QWidget *homeConfigCard)
{
    QWidget *summaryContainer = homeTelemetrySummaryContainer(homeConfigCard);
    const QList<QFrame *> sections = sortedHomeTelemetrySections(summaryContainer);
    QStringList values;
    const int sectionCount = std::min(2, static_cast<int>(sections.size()));
    for (int sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
    {
        QList<QLabel *> labels =
            sections.at(sectionIndex)->findChildren<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
        std::sort(labels.begin(), labels.end(), [summaryContainer](QLabel *lhs, QLabel *rhs) {
            const QPoint lhsPos = lhs->mapTo(summaryContainer, QPoint(0, 0));
            const QPoint rhsPos = rhs->mapTo(summaryContainer, QPoint(0, 0));
            return std::make_tuple(lhsPos.y(), lhsPos.x()) <
                   std::make_tuple(rhsPos.y(), rhsPos.x());
        });
        for (QLabel *label : labels)
        {
            values << label->text();
        }
    }
    return values;
}

QString homeTelemetryPillValue(QWidget *homeConfigCard, const QString& name)
{
    QWidget *summaryContainer = homeTelemetrySummaryContainer(homeConfigCard);
    for (QFrame *pill : summaryContainer
             ? summaryContainer->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"))
             : QList<QFrame *>())
    {
        QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
        QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
        if (nameLabel && valueLabel && nameLabel->text() == name)
        {
            return valueLabel->text();
        }
    }
    return QString();
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
    if (gap < 3 || gap > 10)
    {
        std::cerr << "UI-test telemetry pill text gap: name='"
                  << nameLabel->text().toStdString()
                  << "' value='" << valueLabel->text().toStdString()
                  << "' gap=" << gap << '\n';
    }
    require(gap >= 3 && gap <= 10,
            "UI-test home telemetry field/value text gap keeps a small reserved space");
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
                const QRect labelBounds = pill->rect().adjusted(0, -2, 1, 2);
                if (!labelBounds.contains(labelRect))
                {
                    std::cerr << "UI-test telemetry pill label outside capsule: object="
                              << pillLabel->objectName().toStdString()
                              << " text='" << pillLabel->text().toStdString()
                              << "' pill=" << pill->rect().x() << ','
                              << pill->rect().y() << ','
                              << pill->rect().width() << 'x'
                              << pill->rect().height()
                              << " label=" << labelRect.x() << ','
                              << labelRect.y() << ','
                              << labelRect.width() << 'x'
                              << labelRect.height()
                              << " textWidth="
                              << pillLabel->fontMetrics().horizontalAdvance(pillLabel->text())
                              << '\n';
                }
                require(labelBounds.contains(labelRect),
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

    const QList<QLabel *> rateValueLabels =
        sections.at(0)->findChildren<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
    require(rateValueLabels.size() >= 6, "UI-test data-stream summary keeps six rate capsules");
    for (QLabel *valueLabel : rateValueLabels)
    {
        require(valueLabel->width() >= valueLabel->fontMetrics().horizontalAdvance(QStringLiteral("999.9 Hz")),
                "UI-test data-stream rate capsule reserves the requested 999.9 Hz width");
    }

    const QList<QFrame *> linkPills =
        sections.at(1)->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill"));
    require(linkPills.size() == 4, "UI-test link-rate summary keeps target plus three rate capsules");
    for (QFrame *pill : linkPills)
    {
        QLabel *nameLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryNameLabel"));
        if (nameLabel && (nameLabel->text() == QStringLiteral("目标") ||
                          nameLabel->text() == QStringLiteral("Target")))
        {
            continue;
        }
        QLabel *valueLabel = pill->findChild<QLabel *>(QStringLiteral("homeTelemetrySummaryValueLabel"));
        require(valueLabel != nullptr && valueLabel->text().contains(QStringLiteral("Mbps")),
                "UI-test link-rate capsule shows representative Mbps data");
        require(valueLabel->width() >= valueLabel->fontMetrics().horizontalAdvance(QStringLiteral("999.9 Mbps")),
                "UI-test link-rate capsule reserves the requested 999.9 Mbps width");
    }
}

bool temperatureOverviewOutputPercentShows(QLabel *pill, const QString& expectedValue)
{
    return pill &&
        pill->textFormat() == Qt::RichText &&
        pill->text().contains(QStringLiteral("<br/>")) &&
        (pill->text().contains(QStringLiteral("输出百分比")) ||
         pill->text().contains(QStringLiteral("Output Percent"))) &&
        pill->text().contains(expectedValue);
}

bool temperatureOverviewOutputPercentShowsNonZero(QLabel *pill)
{
    return pill &&
        pill->textFormat() == Qt::RichText &&
        pill->text().contains(QStringLiteral("<br/>")) &&
        (pill->text().contains(QStringLiteral("输出百分比")) ||
         pill->text().contains(QStringLiteral("Output Percent"))) &&
        pill->text().contains(QLatin1Char('%')) &&
        !pill->text().contains(QStringLiteral("0.00%")) &&
        !pill->text().contains(QStringLiteral("---"));
}

bool acceptOpenMessageBoxesAsYes()
{
    bool accepted = false;
    for (QWidget *widget : QApplication::topLevelWidgets())
    {
        if (auto *messageBox = qobject_cast<QMessageBox *>(widget))
        {
            if (QAbstractButton *yesButton = messageBox->button(QMessageBox::Yes))
            {
                yesButton->click();
            }
            else
            {
                messageBox->done(QMessageBox::Yes);
            }
            accepted = true;
        }
    }
    return accepted;
}

QAction *findUiTestScenarioAction(MainWindow *window,
                                  int scenario,
                                  const QStringList& labels)
{
    if (!window)
    {
        return nullptr;
    }
    for (QAction *action : window->findChildren<QAction *>())
    {
        if (action && action->isCheckable() && action->data().toInt() == scenario &&
            labels.contains(action->text()))
        {
            return action;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    QTemporaryDir settingsDirectory;
    require(settingsDirectory.isValid(), "temporary settings directory created");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDirectory.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());
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
    const int telemetryPillCountBeforeModeSwitch =
        homeConfigCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill")).size();
    require(telemetryPillCountBeforeModeSwitch > 0,
            "home telemetry summary contains persistent pills before source-mode switching");
    require(homeTelemetryPillValue(homeConfigCard, QStringLiteral("目标")) == QStringLiteral("本机"),
            "home telemetry target starts as the local host");
    sourceModeSwitch->click();
    processEvents();
    require(homeTelemetryPillValue(homeConfigCard, QStringLiteral("目标")).startsWith(QStringLiteral("TCP ")),
            "home telemetry target updates to the remote Sky TCP endpoint");
    require(homeConfigCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill")).size() ==
                telemetryPillCountBeforeModeSwitch,
            "home telemetry summary keeps the same pill count when switching to remote mode");
    sourceModeSwitch->click();
    processEvents();
    require(homeTelemetryPillValue(homeConfigCard, QStringLiteral("目标")) == QStringLiteral("本机"),
            "home telemetry target returns to the local host");
    require(homeConfigCard->findChildren<QFrame *>(QStringLiteral("homeTelemetrySummaryPill")).size() ==
                telemetryPillCountBeforeModeSwitch,
            "home telemetry summary keeps the same pill count when switching back to local mode");

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
    QPointer<QMenu> scenarioMenu =
        window->findChild<QMenu *>(QStringLiteral("uiTestScenarioMenu"));
    require(modeAction && badge && !scenarioMenu.isNull(), "UI test menu actions and title badge exist");
    modeAction->trigger();
    processEvents();
    require(modeAction->isChecked(), "UI test mode action becomes checked");
    require(!badge->isHidden(), "persistent UI test badge is visible");
    require(scenarioMenu->isEnabled(), "scenario menu is enabled in UI test mode");
    require(VaporViewTest::processEventsUntil(1500, [homeConfigCard]() {
                return homeTelemetrySummaryShowsUiTestRates(homeConfigCard);
            }),
            "UI test mode feeds representative home telemetry summary capsules");
    require(VaporViewTest::processEventsUntil(1500, [homeConfigCard]() {
                return homeTelemetrySummaryHasStableCompactTextGaps(homeConfigCard);
            }),
            "UI test mode settles home telemetry capsule layout after dynamic data arrives");
    requireUiTestHomeTelemetryCapsulesCovered(
        homeConfigCard,
        "UI-test home telemetry summary capsules are covered in Chinese");
    const QStringList dynamicValuesBefore = homeTelemetryDynamicSummaryValues(homeConfigCard);
    require(!dynamicValuesBefore.isEmpty(),
            "UI test mode exposes dynamic home telemetry capsule values");
    const QList<QFrame *> telemetryPillsBeforeRefresh = sortedHomeTelemetryPills(homeConfigCard);
    require(!telemetryPillsBeforeRefresh.isEmpty(),
            "UI test mode exposes home telemetry capsule widgets before dynamic refresh");
    require(VaporViewTest::processEventsUntil(1600, [homeConfigCard, dynamicValuesBefore]() {
                return homeTelemetryDynamicSummaryValues(homeConfigCard) != dynamicValuesBefore;
            }),
            "UI test mode refreshes home telemetry capsule values over time");
    require(sameWidgetSet(sortedHomeTelemetryPills(homeConfigCard), telemetryPillsBeforeRefresh),
            "UI test mode refreshes home telemetry values without rebuilding capsule widgets");
    require(VaporViewTest::processEventsUntil(1500, [homeConfigCard]() {
                return homeTelemetrySummaryHasStableCompactTextGaps(homeConfigCard);
            }),
            "UI test mode settles home telemetry capsule layout after dynamic refresh");
    requireUiTestHomeTelemetryCapsulesCovered(
        homeConfigCard,
        "UI-test home telemetry summary capsules stay covered after dynamic refresh");

    auto *temperatureOutputPercentPill =
        window->findChild<QLabel *>(QStringLiteral("temperatureOverviewOutputPercentPill"));
    auto *temperatureOutputSwitch =
        window->findChild<QPushButton *>(QStringLiteral("temperatureOverviewOutputSwitch"));
    QWidget *temperatureOverviewPlot = nullptr;
    for (QWidget *plot : window->findChildren<QWidget *>(QStringLiteral("temperatureTrendPlot")))
    {
        if (plot && plot->property("temperatureOverviewPlot").toBool())
        {
            temperatureOverviewPlot = plot;
            break;
        }
    }
    require(temperatureOutputPercentPill != nullptr && temperatureOutputSwitch != nullptr,
            "UI test mode exposes the home temperature output percent capsule and switch");
    require(temperatureOverviewPlot != nullptr,
            "UI test mode exposes the home temperature overview trend plot");
    require(VaporViewTest::processEventsUntil(1500, [temperatureOverviewPlot]() {
                return temperatureOverviewPlot->property("xAxisTimeMode").toBool() &&
                    temperatureOverviewPlot->property("xAxisTimeLabelFormat").toString() ==
                        QStringLiteral("hh:mm:ss") &&
                    temperatureOverviewPlot->property("sampleCount").toInt() >= 2 &&
                    temperatureOverviewPlot->property("xAxisTimeSampleCount").toInt() ==
                        temperatureOverviewPlot->property("sampleCount").toInt();
            }),
            "UI test mode feeds timestamped samples into the home temperature overview time axis");
    temperatureOverviewPlot->repaint();
    const double overviewTargetGuideLineY =
        temperatureOverviewPlot->property("targetGuideLineY").toDouble();
    require(temperatureOverviewPlot->property("targetGuideLineVisible").toBool() &&
                temperatureOverviewPlot->property("targetGuideLineColor").toString() ==
                    VaporView::appThemeColor(VaporView::AppThemeColor::ToolbarGreen,
                                             VaporView::isDarkThemeEnabled()).name(QColor::HexRgb) &&
                std::abs(temperatureOverviewPlot->property("targetGuideLineWidth").toDouble() - 1.0) < 0.001 &&
                std::isfinite(overviewTargetGuideLineY) &&
                overviewTargetGuideLineY > 0.0 &&
                overviewTargetGuideLineY < temperatureOverviewPlot->height(),
            "UI test mode draws a thin green target-temperature guide line in the home overview plot");
    const int defaultOverviewTimeTickCount = temperatureOverviewPlot->property("xAxisTickCount").toInt();
    const double defaultOverviewTimeSpan = temperatureOverviewPlot->property("xAxisTimeSpanSeconds").toDouble();
    const double defaultOverviewTimeFirstTick = temperatureOverviewPlot->property("xAxisTimeFirstTickX").toDouble();
    const double defaultOverviewTimeLastTick = temperatureOverviewPlot->property("xAxisTimeLastTickX").toDouble();
    const double defaultOverviewTimeFirstLabelCenter =
        temperatureOverviewPlot->property("xAxisTimeFirstLabelCenterX").toDouble();
    const double defaultOverviewTimeLastLabelCenter =
        temperatureOverviewPlot->property("xAxisTimeLastLabelCenterX").toDouble();
    const double defaultOverviewTimePlotBottom =
        temperatureOverviewPlot->property("xAxisPlotBottomY").toDouble();
    const double defaultOverviewTimeLabelTop =
        temperatureOverviewPlot->property("xAxisLabelTopY").toDouble();
    const double defaultOverviewTimeLabelGap =
        temperatureOverviewPlot->property("xAxisLabelGapFromPlot").toDouble();
    const double defaultOverviewTimeTickLength =
        temperatureOverviewPlot->property("xAxisTickLength").toDouble();
    require(defaultOverviewTimeTickCount >= 2 &&
                std::abs(defaultOverviewTimeSpan - (defaultOverviewTimeTickCount - 1)) < 1e-6 &&
                std::abs(defaultOverviewTimeFirstTick - defaultOverviewTimeFirstLabelCenter) <= 2.0 &&
                std::abs(defaultOverviewTimeLastTick - defaultOverviewTimeLastLabelCenter) <= 2.0 &&
                defaultOverviewTimeLastTick < temperatureOverviewPlot->width() - 4.0,
            "default home temperature overview centers endpoint times on their ticks");
    require(defaultOverviewTimeLabelGap >= 6.0 &&
                std::abs(defaultOverviewTimeLabelTop - defaultOverviewTimePlotBottom -
                         defaultOverviewTimeLabelGap) < 1e-6 &&
                defaultOverviewTimeTickLength >= 4.0,
            "default home temperature overview separates x-axis labels and draws aligned ticks");
    TemperatureTrendPlotWidget adaptiveTimePlot;
    adaptiveTimePlot.setTimeAxisEnabled(true);
    adaptiveTimePlot.setSamples({25.0, 25.5});
    const double localTimeSeconds = static_cast<double>(QDateTime::currentMSecsSinceEpoch()) / 1000.0;
    adaptiveTimePlot.setSampleTimes({localTimeSeconds - 26.0, localTimeSeconds});
    adaptiveTimePlot.resize(800, 180);
    adaptiveTimePlot.show();
    adaptiveTimePlot.repaint();
    processEvents();
    const int defaultTimeTickCount = adaptiveTimePlot.property("xAxisTickCount").toInt();
    const double defaultTimeSpan = adaptiveTimePlot.property("xAxisTimeSpanSeconds").toDouble();
    const double defaultTimeFirstTick = adaptiveTimePlot.property("xAxisTimeFirstTickX").toDouble();
    const double defaultTimeLastTick = adaptiveTimePlot.property("xAxisTimeLastTickX").toDouble();
    const double defaultMinimumTickSpacing =
        adaptiveTimePlot.property("xAxisTimeMinimumTickSpacing").toDouble();
    adaptiveTimePlot.resize(1600, 180);
    adaptiveTimePlot.repaint();
    processEvents();
    const int wideTimeTickCount = adaptiveTimePlot.property("xAxisTickCount").toInt();
    const double wideTimeSpan = adaptiveTimePlot.property("xAxisTimeSpanSeconds").toDouble();
    const double wideTimeFirstTick = adaptiveTimePlot.property("xAxisTimeFirstTickX").toDouble();
    const double wideTimeLastTick = adaptiveTimePlot.property("xAxisTimeLastTickX").toDouble();
    const double wideMinimumTickSpacing =
        adaptiveTimePlot.property("xAxisTimeMinimumTickSpacing").toDouble();
    const double wideTimeMax = adaptiveTimePlot.property("xAxisTimeMaxSeconds").toDouble();
    const QString wideTimeRightLabel = adaptiveTimePlot.property("xAxisTimeRightLabel").toString();
    const QString expectedWideTimeRightLabel = QDateTime::fromSecsSinceEpoch(
        static_cast<qint64>(std::floor(wideTimeMax)))
        .toLocalTime()
        .toString(QStringLiteral("hh:mm:ss"));
    adaptiveTimePlot.close();
    const double defaultTimeTickSpacing =
        (defaultTimeLastTick - defaultTimeFirstTick) / std::max(1, defaultTimeTickCount - 1);
    const double wideTimeTickSpacing =
        (wideTimeLastTick - wideTimeFirstTick) / std::max(1, wideTimeTickCount - 1);
    require(defaultTimeTickCount >= 10 &&
                wideTimeTickCount >= defaultTimeTickCount * 2 - 2 &&
                std::abs(defaultTimeSpan - (defaultTimeTickCount - 1)) < 1e-6 &&
                std::abs(wideTimeSpan - (wideTimeTickCount - 1)) < 1e-6 &&
                defaultMinimumTickSpacing > 0.0 &&
                wideMinimumTickSpacing > 0.0 &&
                defaultTimeTickSpacing >= defaultMinimumTickSpacing - 1.0 &&
                defaultTimeTickSpacing <= defaultMinimumTickSpacing + 8.0 &&
                wideTimeTickSpacing >= wideMinimumTickSpacing - 1.0 &&
                wideTimeTickSpacing <= wideMinimumTickSpacing + 8.0 &&
                std::abs(defaultTimeTickSpacing - wideTimeTickSpacing) <= 8.0 &&
                std::abs(wideTimeMax - localTimeSeconds) <= 1.0 &&
                wideTimeRightLabel == expectedWideTimeRightLabel,
            "temperature overview time axis packs the densest stable tick spacing as the plot widens");

    require(VaporViewTest::processEventsUntil(1500, [temperatureOutputPercentPill, temperatureOutputSwitch]() {
                return temperatureOutputSwitch->isChecked() &&
                    temperatureOverviewOutputPercentShowsNonZero(temperatureOutputPercentPill);
            }),
            "UI test mode feeds a non-zero home temperature output percent capsule by default");
    require(temperatureOutputSwitch->isEnabled() && temperatureOutputSwitch->isChecked(),
            "UI test mode starts the home temperature output switch enabled and on");
    temperatureOutputSwitch->click();
    require(VaporViewTest::processEventsUntil(1500, [temperatureOutputPercentPill, temperatureOutputSwitch]() {
                return !temperatureOutputSwitch->isChecked() &&
                    temperatureOverviewOutputPercentShows(
                        temperatureOutputPercentPill, QStringLiteral("0.00%"));
            }),
            "UI test mode returns the home temperature output percent capsule to zero after disabling RD105 output");

    bool sawTemperatureOutputPrompt = false;
    QTimer temperatureOutputPromptCloser;
    QObject::connect(&temperatureOutputPromptCloser, &QTimer::timeout, [&sawTemperatureOutputPrompt]() {
        sawTemperatureOutputPrompt =
            acceptOpenMessageBoxesAsYes() || sawTemperatureOutputPrompt;
    });
    temperatureOutputPromptCloser.start(10);
    temperatureOutputSwitch->click();
    temperatureOutputPromptCloser.stop();
    require(sawTemperatureOutputPrompt,
            "UI test mode confirms the home temperature output re-enable prompt");
    const bool temperatureOutputPercentBecameNonZero =
        VaporViewTest::processEventsUntil(1500, [temperatureOutputPercentPill, temperatureOutputSwitch]() {
                return temperatureOutputSwitch->isChecked() &&
                    temperatureOverviewOutputPercentShowsNonZero(temperatureOutputPercentPill);
            });
    if (!temperatureOutputPercentBecameNonZero)
    {
        std::cerr << "home temperature output percent after enable: checked="
                  << temperatureOutputSwitch->isChecked()
                  << " enabled=" << temperatureOutputSwitch->isEnabled()
                  << " text='" << temperatureOutputPercentPill->text().toStdString()
                  << "'\n";
    }
    require(temperatureOutputPercentBecameNonZero,
            "UI test mode drives the home temperature output percent capsule after enabling RD105 output");
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
    require(VaporViewTest::processEventsUntil(2000, [ai8HomeAction, ai8DeviceAction, ai8TitleAction]() {
                return ai8HomeAction->isEnabled() &&
                    ai8HomeAction->property("state").toString() == QStringLiteral("connected") &&
                    ai8DeviceAction->isEnabled() &&
                    ai8DeviceAction->property("state").toString() == QStringLiteral("connected") &&
                    ai8TitleAction->isEnabled() &&
                    ai8TitleAction->property("state").toString() == QStringLiteral("connected") &&
                    ai8TitleAction->property("temperatureTitleCommand").toString() ==
                        QStringLiteral("disconnect");
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
                ai8Plot->property("xAxisTimeMode").toBool() &&
                ai8Plot->property("xAxisTimeSampleCount").toInt() ==
                    ai8Plot->property("sampleCount").toInt() &&
                ai8Plot->property("xAxisTimeLabelFormat").toString() ==
                    QStringLiteral("hh:mm:ss") &&
                ai8Plot->property("xAxisTickCount").toInt() >= 2 &&
                ai8Plot->property("yAxisMaxC").toDouble() > ai8Plot->property("yAxisMinC").toDouble(),
            "AI-8 trend plot exposes populated samples and a time axis in UI test mode");
    ai8Plot->repaint();
    const double ai8TargetGuideLineY = ai8Plot->property("targetGuideLineY").toDouble();
    require(ai8Plot->property("targetGuideLineVisible").toBool() &&
                ai8Plot->property("targetGuideLineColor").toString() ==
                    VaporView::appThemeColor(VaporView::AppThemeColor::ToolbarGreen,
                                             VaporView::isDarkThemeEnabled()).name(QColor::HexRgb) &&
                std::abs(ai8Plot->property("targetGuideLineWidth").toDouble() - 1.0) < 0.001 &&
                std::isfinite(ai8TargetGuideLineY) &&
                ai8TargetGuideLineY > 0.0 &&
                ai8TargetGuideLineY < ai8Plot->height(),
            "AI-8 trend plot draws the same bright green target-temperature guide line");
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

    QPushButton *deviceConfigNav = nullptr;
    for (QPushButton *button : window->findChildren<QPushButton *>())
    {
        if (button &&
            (button->accessibleName() == QStringLiteral("设备配置") ||
             button->accessibleName() == QStringLiteral("Device")))
        {
            deviceConfigNav = button;
            break;
        }
    }
    require(deviceConfigNav, "device configuration navigation exists in UI test mode");
    deviceConfigNav->click();
    processEvents();
    QWidget *deviceConfigPage = window->findChild<QWidget *>(QStringLiteral("deviceConfigPage"));
    require(deviceConfigPage && deviceConfigPage->isVisible(),
            "unified device configuration page is visible in UI test mode");
    QComboBox *deviceSourceMode =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceDataSourceModeCombo"));
    require(deviceSourceMode && deviceSourceMode->count() >= 2,
            "unified device configuration page exposes the source-mode selector");
    deviceSourceMode->setCurrentIndex(1);
    processEvents();
    auto *deviceRemoteCard =
        deviceConfigPage->findChild<QGroupBox *>(QStringLiteral("deviceRemoteSkyConfigCard"));
    auto *deviceRemoteRead =
        deviceConfigPage->findChild<QPushButton *>(QStringLiteral("deviceRemoteSkyReadButton"));
    auto *deviceRemoteApply =
        deviceConfigPage->findChild<QPushButton *>(QStringLiteral("deviceRemoteSkyApplyButton"));
    auto *deviceRemoteSave =
        deviceConfigPage->findChild<QPushButton *>(QStringLiteral("deviceRemoteSkySaveButton"));
    auto *deviceRemoteRaw =
        deviceConfigPage->findChild<QPushButton *>(QStringLiteral("deviceRemoteSkyRawModeButton"));
    auto *deviceRemoteRawJson =
        deviceConfigPage->findChild<QPlainTextEdit *>(QStringLiteral("deviceRemoteSkyRawJsonEdit"));
    auto *deviceRemoteStatus =
        deviceConfigPage->findChild<QLabel *>(QStringLiteral("deviceRemoteSkyConfigStatus"));
    auto *deviceRemoteRd105Slave =
        deviceConfigPage->findChild<QSpinBox *>(QStringLiteral("deviceRemoteSkyRd105SlaveSpin"));
    auto *deviceEpsilonPort =
        deviceConfigPage->findChild<QComboBox *>(QStringLiteral("deviceEpsilonPortCombo"));
    require(deviceRemoteCard && deviceRemoteCard->isVisible() &&
                deviceRemoteRead && deviceRemoteApply && deviceRemoteSave &&
                deviceRemoteRaw && deviceRemoteRawJson && deviceRemoteStatus &&
                deviceRemoteRd105Slave && deviceEpsilonPort,
            "remote sky configuration controls live on the unified device configuration page");
    deviceRemoteRead->click();
    require(VaporViewTest::processEventsUntil(1500, [deviceEpsilonPort]() {
                return deviceEpsilonPort->currentText() == QStringLiteral("UI-TEST-EPSILON");
            }),
            "UI-test Remote Sky read loads a fixed config into the shared device rows");
    require(deviceRemoteStatus->property("status").toString() == QStringLiteral("success"),
            "UI-test Remote Sky read uses the synced/success status vocabulary");
    deviceRemoteRd105Slave->setValue(23);
    processEvents();
    require(deviceRemoteApply->isEnabled(),
            "editing Remote Sky fields marks the unified page dirty and enables Apply in UI test mode");
    require(deviceRemoteStatus->property("status").toString() == QStringLiteral("dirty"),
            "UI-test Remote Sky edit uses the dirty status vocabulary");
    deviceRemoteRaw->click();
    require(VaporViewTest::processEventsUntil(800, [deviceRemoteRawJson]() {
                return deviceRemoteRawJson->isVisible() &&
                    deviceRemoteRawJson->toPlainText().contains(QStringLiteral("\"wave_tcp\"")) &&
                    deviceRemoteRawJson->toPlainText().contains(QStringLiteral("\"slave_address\": 23"));
            }),
            "unified device configuration exposes Remote Sky Raw JSON round-trip");
    deviceRemoteApply->click();
    require(VaporViewTest::processEventsUntil(800, [deviceRemoteStatus]() {
                const QString text = deviceRemoteStatus->text();
                return text.contains(QStringLiteral("已验证")) ||
                    text.contains(QStringLiteral("validated"));
            }),
            "UI-test Remote Sky apply validates and applies on the unified page");
    require(deviceRemoteStatus->property("status").toString() == QStringLiteral("success"),
            "UI-test Remote Sky apply returns to the synced/success status vocabulary");
    deviceRemoteSave->click();
    require(VaporViewTest::processEventsUntil(800, [deviceRemoteStatus]() {
                const QString text = deviceRemoteStatus->text();
                return text.contains(QStringLiteral("模拟保存")) ||
                    text.contains(QStringLiteral("Simulated"));
            }),
            "UI-test Remote Sky save is simulated from the unified page");
    require(deviceRemoteStatus->property("status").toString() == QStringLiteral("success"),
            "UI-test Remote Sky save keeps the saved/success status vocabulary");

    QAction *partialFailureAction = findUiTestScenarioAction(
        window,
        1,
        QStringList{QStringLiteral("部分设备异常"), QStringLiteral("Partial Device Failure")});
    QAction *stalledAction = findUiTestScenarioAction(
        window,
        2,
        QStringList{QStringLiteral("数据停更"), QStringLiteral("Data Stalled")});
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
    require(settingsSnapshotsEqual(snapshotAll(), before,
                                   "all settings namespaces remain byte-for-byte equivalent during UI test mode"),
            "all settings namespaces remain byte-for-byte equivalent during UI test mode");
    require(!QDir(settingsDirectory.filePath(QStringLiteral("business-output"))).exists(),
            "simulated recording did not create its configured business directory");

    modeAction->trigger();
    processEvents();
    require(!modeAction->isChecked(), "UI test mode action clears after exit");
    require(badge->isHidden(), "UI test badge hides after exit");
    QMenu *scenarioMenuAfterExit = window->findChild<QMenu *>(QStringLiteral("uiTestScenarioMenu"));
    const bool cachedScenarioMenuEnabled = !scenarioMenu.isNull() && scenarioMenu->isEnabled();
    const bool currentScenarioMenuEnabled = scenarioMenuAfterExit && scenarioMenuAfterExit->isEnabled();
    if (cachedScenarioMenuEnabled || currentScenarioMenuEnabled)
    {
        std::cerr << "scenario menu after exit: found=" << (scenarioMenuAfterExit != nullptr)
                  << " cachedAlive=" << !scenarioMenu.isNull()
                  << " cachedEnabled=" << cachedScenarioMenuEnabled
                  << " currentEnabled=" << currentScenarioMenuEnabled
                  << " actionChecked=" << modeAction->isChecked()
                  << " badgeHidden=" << badge->isHidden()
                  << '\n';
    }
    require(!cachedScenarioMenuEnabled && !currentScenarioMenuEnabled,
            "scenario menu is disabled after exit");
    require(!testCreatedAuxiliary.isVisible(), "test-created auxiliary window closes on UI test exit");
    require(settingsSnapshotsEqual(snapshotAll(), before,
                                   "all settings namespaces remain unchanged after normal UI test exit"),
            "all settings namespaces remain unchanged after normal UI test exit");
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
    require(settingsSnapshotsEqual(snapshotAll(), beforeDirectClose,
                                   "direct close from UI test mode does not persist destructor state"),
            "direct close from UI test mode does not persist destructor state");
    VaporView::setSettingsWritesSuspended(false);

    {
        VaporView::setSettingsWritesSuspended(true);
        QTemporaryDir connectionLogDirectory;
        require(connectionLogDirectory.isValid(), "temporary connection log directory created");
        VaporView::LogService connectionLogService(
            QStringLiteral("VaporViewUiTestModeWindowConnectionTest"),
            nullptr,
            connectionLogDirectory.path(),
            connectionLogDirectory.path());
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
        auto *connectionLogAllAction =
            connectionWindow->findChild<QAction *>(QStringLiteral("logFilterAllMenuAction"));
        require(connectionLogAllAction != nullptr,
                "normal-mode window exposes the all-log view action");
        connectionLogAllAction->trigger();
        processEvents();
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
        const bool connectionLogsFlushed =
            VaporViewTest::processEventsUntil(7000, [connectionWavePanel, connectionLogList]() {
                    return connectionWavePanel->isConnected() &&
                        findLogEventRow(connectionLogList,
                                        QStringLiteral("local_connection_summary")) >= 0 &&
                        findLogEventRow(connectionLogList,
                                        QStringLiteral("local_serial_device_phase_completed")) >= 0 &&
                        findLogEventRow(connectionLogList,
                                        QStringLiteral("tcp_wave_connection_started")) >= 0 &&
                        findLogEventRow(connectionLogList,
                                        QStringLiteral("tcp_wave_connected")) >= 0;
                });
        if (connectionLogsFlushed == false)
        {
            std::cerr << "title-bar connection log rows: connected="
                      << connectionWavePanel->isConnected()
                      << " summary=" << findLogEventRow(connectionLogList,
                                                        QStringLiteral("local_connection_summary"))
                      << " serial=" << findLogEventRow(connectionLogList,
                                                       QStringLiteral("local_serial_device_phase_completed"))
                      << " waveConnecting=" << findLogEventRow(connectionLogList,
                                                               QStringLiteral("tcp_wave_connection_started"))
                      << " waveConnected=" << findLogEventRow(connectionLogList,
                                                              QStringLiteral("tcp_wave_connected"))
                      << "\n";
            dumpLogRows(connectionLogList, "title-bar connection log rows");
        }
        require(connectionLogsFlushed,
                "title-bar connection connects the local TCP waveform source and flushes its logs");
        const int summaryRow = findLogEventRow(connectionLogList,
                                               QStringLiteral("local_connection_summary"));
        const int serialPhaseRow = findLogEventRow(connectionLogList,
                                                   QStringLiteral("local_serial_device_phase_completed"));
        const int waveformConnectingRow = findLogEventRow(connectionLogList,
                                                          QStringLiteral("tcp_wave_connection_started"));
        const int waveformConnectedRow = findLogEventRow(connectionLogList,
                                                         QStringLiteral("tcp_wave_connected"));
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
        auto *failedLogAllAction =
            failedConnectionWindow->findChild<QAction *>(QStringLiteral("logFilterAllMenuAction"));
        require(failedLogAllAction != nullptr,
                "failed-path normal-mode window exposes the all-log view action");
        failedLogAllAction->trigger();
        processEvents();
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
            return findLogEventRow(failedLogList,
                                   QStringLiteral("local_serial_devices_not_connected")) >= 0 &&
                findLogEventRow(failedLogList,
                                QStringLiteral("tcp_wave_connection_started")) >= 0 &&
                findLogEventRow(failedLogList,
                                QStringLiteral("tcp_wave_socket_error")) >= 0 &&
                findLogEventRow(failedLogList,
                                QStringLiteral("local_connection_summary")) >= 0;
        });
        if (!failedLogsFlushed)
        {
            dumpLogRows(failedLogList, "failed-path title-bar connection log rows");
        }
        require(failedLogsFlushed,
                "failed-path title-bar connection flushes serial, TCP error, and summary logs");
        const int failedNoSerialRow = findLogEventRow(
            failedLogList, QStringLiteral("local_serial_devices_not_connected"));
        const int failedWaveformConnectingRow = findLogEventRow(
            failedLogList, QStringLiteral("tcp_wave_connection_started"));
        const int failedWaveformErrorRow = findLogEventRow(
            failedLogList, QStringLiteral("tcp_wave_socket_error"));
        const int failedSummaryRow = findLogEventRow(
            failedLogList, QStringLiteral("local_connection_summary"));
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
    require(settingsSnapshotsEqual(snapshotAll(), beforeDirectClose,
                                   "normal-mode waveform connection coverage does not persist temporary inputs"),
            "normal-mode waveform connection coverage does not persist temporary inputs");

    std::cout << "ui_test_mode_window_test passed\n";
    return 0;
}
