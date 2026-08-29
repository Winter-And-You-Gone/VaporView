#include "ground/devices/DeviceRatePolicy.h"
#include "ground/navigation/EpsilonConfigPanel.h"
#include "shared/theme/AppTheme.h"
#include "test_ui_helpers.h"

#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QSet>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <vector>

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

QList<QComboBox *> packetRateCombos(
    VaporView::Ground::Navigation::EpsilonConfigPanel& panel)
{
    QList<QComboBox *> result;
    for (QComboBox *combo : panel.findChildren<QComboBox *>())
    {
        if (combo && combo->property("epsilonPacketId").isValid())
        {
            result.append(combo);
        }
    }
    return result;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    using VaporView::Ground::Navigation::EpsilonConfigPanel;
    using namespace VaporView::Ground::DeviceRates;

    EpsilonConfigPanel panel;
    panel.resize(1100, 420);
    panel.show();
    QApplication::processEvents();

    int sectionCardCount = 0;
    for (QFrame *card : panel.findChildren<QFrame *>())
    {
        if (card->property("epsilonConfigCard").toBool())
        {
            ++sectionCardCount;
        }
    }
    require(panel.findChild<QFrame *>(QStringLiteral("epsilonStatusCard")) != nullptr &&
                panel.findChild<QFrame *>(QStringLiteral("epsilonLivePacketRateCard")) != nullptr &&
                panel.findChild<QFrame *>(QStringLiteral("epsilonOutputCard")) != nullptr &&
                panel.findChild<QFrame *>(QStringLiteral("epsilonDeviceSettingsCard")) != nullptr &&
                sectionCardCount == 4,
            "panel exposes the summary, live-rate, output, and device-settings cards");
    require(panel.findChild<QWidget *>(QStringLiteral("epsilonActionsContainer")) != nullptr,
            "panel keeps a separate primary action container");
    auto *outputCard = panel.findChild<QFrame *>(QStringLiteral("epsilonOutputCard"));
    auto *outputTitleBar = outputCard
        ? outputCard->findChild<QWidget *>(QStringLiteral("sectionTitleBar"))
        : nullptr;
    auto *hintLabel = panel.findChild<QLabel *>(QStringLiteral("epsilonConfigHint"));
    auto *recommendedButton = panel.findChild<QPushButton *>(QStringLiteral("epsilonRecommendedConfigButton"));
    require(outputTitleBar != nullptr && hintLabel != nullptr && recommendedButton != nullptr &&
                outputTitleBar->isAncestorOf(hintLabel) && outputTitleBar->isAncestorOf(recommendedButton) &&
                panel.findChild<QFrame *>(QStringLiteral("epsilonStatusCard"))->findChild<QLabel *>(
                    QStringLiteral("epsilonConfigHint")) == nullptr,
            "packet-rate hint and recommended action live in the packet communication title bar");
    require(hintLabel->isVisible() && !hintLabel->text().isEmpty() && hintLabel->width() > 0,
            "packet-rate hint remains visible beside the title-bar action");
    const QRect recommendedGeometry(recommendedButton->mapTo(outputTitleBar, QPoint(0, 0)),
                                    recommendedButton->size());
    require(recommendedButton->height() <= outputTitleBar->height() - 8 &&
                recommendedGeometry.top() >= 4 &&
                recommendedGeometry.bottom() <= outputTitleBar->height() - 5 &&
                outputTitleBar->rect().contains(recommendedGeometry),
            "recommended action fits fully inside the title bar");
    const QString panelStyle = panel.styleSheet();
    require(panel.testAttribute(Qt::WA_StyledBackground) &&
                panelStyle.contains(QStringLiteral(
                    "QFrame#epsilonSectionCard { background-color:")) &&
                panelStyle.contains(QStringLiteral(
                    "QWidget[epsilonConfigCardBody=\"true\"] { background-color:")) &&
                panelStyle.contains(QStringLiteral(
                    "QPushButton#epsilonRecommendedConfigButton { min-height: 28px; max-height: 28px;")) &&
                !panelStyle.contains(QStringLiteral("QFrame#epsilonSectionCard { background-color: transparent")),
            "panel root, card bodies, and action footer resolve to the shared theme surfaces");
    for (QWidget *body : panel.findChildren<QWidget *>())
    {
        if (body->property("epsilonConfigCardBody").toBool())
        {
            require(body->testAttribute(Qt::WA_StyledBackground),
                    "each EPSILON card body paints its raised theme surface");
        }
    }

    const auto &options = epsilonPacketConfigOptions();
    const QList<QComboBox *> combos = packetRateCombos(panel);
    require(options.size() == 11, "EPSILON policy exposes exactly 11 packet options");
    require(combos.size() == 11, "panel exposes exactly 11 packet-rate controls");
    auto *rtcmDevicePortCombo = panel.findChild<QComboBox *>(QStringLiteral("epsilonRtcmDevicePortCombo"));
    require(rtcmDevicePortCombo != nullptr &&
                rtcmDevicePortCombo->property("epsilonRtcmDevicePortControl").toBool() &&
                !rtcmDevicePortCombo->property("epsilonPacketId").isValid(),
            "RTCM input port selector lives in the device settings card, not the packet-rate grid");
    VaporViewTest::requireComboPopupStyled(rtcmDevicePortCombo,
                                           "EPSILON RTCM input selector has the shared popup highlight",
                                           require);
    require(rtcmDevicePortCombo->count() == 4 &&
                rtcmDevicePortCombo->itemData(0).toInt() == 2 &&
                rtcmDevicePortCombo->itemText(0).contains(QStringLiteral("（默认）")) &&
                rtcmDevicePortCombo->itemData(3).toInt() == 5 &&
                panel.rtcmDevicePortIndex() == 2,
            "RTCM input selector exposes COMM2-COMM5 with COMM2 as the default");
    for (QComboBox *combo : combos)
    {
        VaporViewTest::requireComboPopupStyled(combo,
                                               "EPSILON packet-rate selector has the shared popup highlight",
                                               require);
    }
    panel.setRtcmDevicePortIndex(3);
    require(panel.rtcmDevicePortIndex() == 3,
            "RTCM input selector setter and getter preserve the selected device port");

    struct PacketGroupExpectation
    {
        const char *objectName;
        const char *title;
        std::set<uint8_t> packetIds;
    };
    const std::vector<PacketGroupExpectation> packetGroups = {
        {"epsilonPacketGroupSystemDiagnostics", "系统与诊断", {0x50, 0x53}},
        {"epsilonPacketGroupAttitudeRepresentation", "姿态表示", {0x63, 0x64}},
        {"epsilonPacketGroupInertialFusion", "惯导与融合", {0x40, 0x41, 0x42}},
        {"epsilonPacketGroupGnssPosition", "GNSS 与位置", {0x59, 0x5A, 0x5C, 0x5D}},
    };
    for (int groupIndex = 0; groupIndex < static_cast<int>(packetGroups.size()); ++groupIndex)
    {
        const PacketGroupExpectation& group = packetGroups.at(groupIndex);
        auto *groupLabel = panel.findChild<QLabel *>(QString::fromLatin1(group.objectName));
        require(groupLabel != nullptr && groupLabel->text() == QString::fromUtf8(group.title) &&
                    groupLabel->property("epsilonPacketGroupHeader").toBool() &&
                    groupLabel->accessibleName() == groupLabel->text(),
                "EPSILON packet group header is visible, named, and accessible");
    }

    require(outputCard != nullptr, "EPSILON output card exists for packet group geometry");
    std::vector<QRect> groupRects(packetGroups.size());
    std::vector<int> groupFieldBottoms(packetGroups.size(), -1);
    std::vector<std::map<std::pair<int, int>, QRect>> groupFieldCells(packetGroups.size());
    std::map<int, int> inputColumnLefts;
    for (int groupIndex = 0; groupIndex < static_cast<int>(packetGroups.size()); ++groupIndex)
    {
        const PacketGroupExpectation& group = packetGroups.at(groupIndex);
        auto *groupLabel = panel.findChild<QLabel *>(QString::fromLatin1(group.objectName));
        const QRect groupRect(groupLabel->mapTo(outputCard, QPoint(0, 0)), groupLabel->size());
        groupRects.at(groupIndex) = groupRect;
        int firstPacketTop = std::numeric_limits<int>::max();
        for (QComboBox *combo : combos)
        {
            const auto packetId = static_cast<uint8_t>(combo->property("epsilonPacketId").toUInt());
            if (combo->property("epsilonPacketGroup").toInt() != groupIndex)
            {
                continue;
            }
            const QRect comboRect(combo->mapTo(outputCard, QPoint(0, 0)), combo->size());
            const int visualColumn = combo->property("epsilonPacketGridColumn").toInt();
            const auto [columnLeft, inserted] = inputColumnLefts.emplace(visualColumn, comboRect.left());
            if (!inserted)
            {
                require(std::abs(columnLeft->second - comboRect.left()) <= 2,
                        "wide packet-rate input controls align within each visual column");
            }
            firstPacketTop = std::min(firstPacketTop, comboRect.top());
            groupFieldBottoms.at(groupIndex) = std::max(groupFieldBottoms.at(groupIndex), comboRect.bottom());
            const int groupFieldRow = combo->property("epsilonPacketGroupFieldRow").toInt();
            const int groupFieldColumn = combo->property("epsilonPacketGroupFieldColumn").toInt();
            require(groupFieldRow >= 0 && groupFieldColumn >= 0 && groupFieldColumn <= 1,
                    "wide packet-rate controls expose compact in-group row and column positions");
            require(groupFieldCells.at(groupIndex)
                        .emplace(std::make_pair(groupFieldRow, groupFieldColumn), comboRect)
                        .second,
                    "wide packet-rate controls occupy unique in-group grid cells");
            require(group.packetIds.find(packetId) != group.packetIds.end(),
                    "packet group geometry contains only its assigned packet controls");
        }
        require(firstPacketTop > groupRect.bottom(),
                "EPSILON packet group header precedes its fields");
    }
    for (int groupIndex = 0; groupIndex < static_cast<int>(packetGroups.size()); ++groupIndex)
    {
        const auto& cells = groupFieldCells.at(groupIndex);
        const int expectedRows = (static_cast<int>(packetGroups.at(groupIndex).packetIds.size()) + 1) / 2;
        require(static_cast<int>(cells.size()) == static_cast<int>(packetGroups.at(groupIndex).packetIds.size()),
                "wide packet group keeps every assigned packet in its compact subgrid");
        for (int row = 0; row < expectedRows; ++row)
        {
            const auto left = cells.find(std::make_pair(row, 0));
            require(left != cells.end(), "wide packet group fills the left cell of each internal row");
            const auto right = cells.find(std::make_pair(row, 1));
            if (right != cells.end())
            {
                require(right->second.left() > left->second.right(),
                        "wide packet group places two fields side-by-side inside the category");
                require(std::abs(right->second.top() - left->second.top()) <= 2,
                        "wide packet group aligns paired internal fields on the same row");
            }
            if (row > 0)
            {
                const auto previous = cells.find(std::make_pair(row - 1, 0));
                require(previous != cells.end() && left->second.top() > previous->second.bottom(),
                        "wide packet group wraps internal fields onto a second row");
            }
        }
    }
    require(std::abs(groupRects.at(0).top() - groupRects.at(1).top()) <= 2 &&
                groupRects.at(1).left() > groupRects.at(0).right(),
            "wide EPSILON packet groups place system/diagnostics and attitude representation on the first row");
    require(std::abs(groupRects.at(2).top() - groupRects.at(3).top()) <= 2 &&
                groupRects.at(3).left() > groupRects.at(2).right(),
            "wide EPSILON packet groups place inertial/fusion and GNSS/position on the second row");
    require(groupRects.at(2).top() > std::max(groupFieldBottoms.at(0), groupFieldBottoms.at(1)) &&
                std::abs(groupRects.at(0).left() - groupRects.at(2).left()) <= 2 &&
                std::abs(groupRects.at(1).left() - groupRects.at(3).left()) <= 2,
            "wide EPSILON packet group columns align as a two-by-two layout");

    QSet<int> wideColumns;
    for (QComboBox *combo : combos)
    {
        wideColumns.insert(combo->property("epsilonPacketGridColumn").toInt());
    }
    require(wideColumns == QSet<int>{0, 1, 2, 3},
            "wide panel lays packet-rate fields out in four compact visual columns");

    std::set<uint8_t> packetIds;
    std::set<QString> objectNames;
    for (QComboBox *combo : combos)
    {
        const auto packetId = static_cast<uint8_t>(combo->property("epsilonPacketId").toUInt());
        const auto optionIt = std::find_if(options.begin(), options.end(), [packetId](const auto& option) {
            return option.packet_id == packetId;
        });
        require(optionIt != options.end(), "packet-rate control maps to a policy option");
        require(packetIds.insert(packetId).second, "packet-rate control packet id is unique");
        require(combo->count() == static_cast<int>(optionIt->supported_rates_hz.size()),
                "packet-rate control preserves every supported rate");
        for (int i = 0; i < combo->count(); ++i)
        {
            require(combo->itemData(i).toInt() == optionIt->supported_rates_hz.at(i),
                    "packet-rate control preserves supported rate order and value");
        }
        require(!combo->objectName().isEmpty() && objectNames.insert(combo->objectName()).second,
                "packet-rate control objectName is non-empty and unique");
        require(!combo->accessibleName().isEmpty(), "packet-rate control has accessibleName");
        require(combo->focusPolicy() == Qt::TabFocus, "packet-rate control uses TabFocus");
        const int groupIndex = combo->property("epsilonPacketGroup").toInt();
        require(groupIndex >= 0 && groupIndex < static_cast<int>(packetGroups.size()) &&
                    packetGroups.at(groupIndex).packetIds.find(packetId) !=
                        packetGroups.at(groupIndex).packetIds.end(),
                "packet-rate control belongs to its documented output category");
    }

    const std::map<uint8_t, int> defaults = defaultEpsilonPacketRates();
    panel.setPacketRates(defaults);
    require(panel.packetRates() == defaults, "semantic packet-rate setter and getter preserve all 11 values");
    VaporView::EpsilonData liveData;
    liveData.valid = true;
    liveData.imu_packet_rate_hz = 250.0;
    liveData.ahrs_packet_rate_hz = 50.0;
    liveData.insgps_packet_rate_hz = 100.0;
    liveData.sys_state_packet_rate_hz = 100.0;
    liveData.raw_gnss_packet_rate_hz = 10.0;
    liveData.satellite_packet_rate_hz = 1.0;
    liveData.geodetic_packet_rate_hz = 10.0;
    liveData.ecef_packet_rate_hz = 10.0;
    liveData.euler_orien_packet_rate_hz = 50.0;
    liveData.quat_orien_packet_rate_hz = 50.0;
    panel.setLivePacketRates(liveData);
    auto *liveRateCard = panel.findChild<QFrame *>(QStringLiteral("epsilonLivePacketRateCard"));
    auto *liveRateTitle = panel.findChild<QLabel *>(QStringLiteral("epsilonLivePacketRateCardTitle"));
    require(liveRateCard != nullptr && liveRateTitle != nullptr &&
                liveRateTitle->text() == QStringLiteral("实时数据包频率"),
            "live packet-rate card appears below the configuration summary");
    auto *summaryCard = panel.findChild<QFrame *>(QStringLiteral("epsilonStatusCard"));
    const QRect summaryRect(summaryCard->mapTo(&panel, QPoint(0, 0)), summaryCard->size());
    const QRect liveRateRect(liveRateCard->mapTo(&panel, QPoint(0, 0)), liveRateCard->size());
    require(liveRateRect.top() > summaryRect.bottom(),
            "live packet-rate card is placed below the summary card");
    int liveRateValueCount = 0;
    QSet<int> liveRateColumns;
    std::map<std::pair<int, int>, QRect> liveRateCells;
    std::map<int, int> liveRateColumnLefts;
    for (const auto &option : options)
    {
        const QString packetId = QStringLiteral("%1").arg(
            option.packet_id, 2, 16, QLatin1Char('0')).toUpper();
        auto *field = panel.findChild<QWidget *>(
            QStringLiteral("epsilonLivePacketRateField_%1").arg(packetId));
        auto *label = panel.findChild<QLabel *>(
            QStringLiteral("epsilonLivePacketRateLabel_%1").arg(packetId));
        auto *value = panel.findChild<QLabel *>(
            QStringLiteral("epsilonLivePacketRateValue_%1").arg(packetId));
        require(field != nullptr && label != nullptr && value != nullptr && !label->text().isEmpty() &&
                    !value->text().isEmpty(),
                "live packet-rate card exposes every configured packet");
        const int row = field->property("epsilonLivePacketGridRow").toInt();
        const int column = field->property("epsilonLivePacketGridColumn").toInt();
        require(row >= 0 && column >= 0 && column <= 3 &&
                    label->property("epsilonLivePacketGridRow").toInt() == row &&
                    label->property("epsilonLivePacketGridColumn").toInt() == column &&
                    value->property("epsilonLivePacketGridRow").toInt() == row &&
                    value->property("epsilonLivePacketGridColumn").toInt() == column,
                "live packet-rate fields expose their four-column grid position");
        const QRect fieldRect(field->mapTo(liveRateCard, QPoint(0, 0)), field->size());
        liveRateColumns.insert(column);
        const auto [columnLeft, inserted] = liveRateColumnLefts.emplace(column, fieldRect.left());
        if (!inserted)
        {
            require(std::abs(columnLeft->second - fieldRect.left()) <= 2,
                    "live packet-rate fields align within their visual column");
        }
        require(liveRateCells.emplace(std::make_pair(row, column), fieldRect).second,
                "live packet-rate fields occupy unique cells");
        ++liveRateValueCount;
    }
    require(liveRateValueCount == 11, "live packet-rate card exposes all 11 packet rates");
    require(liveRateColumns == QSet<int>{0, 1, 2, 3},
            "live packet-rate card lays fields out across four columns");
    for (const auto& [cell, rect] : liveRateCells)
    {
        if (cell.first == 0)
        {
            continue;
        }
        const auto previous = liveRateCells.find(std::make_pair(cell.first - 1, cell.second));
        require(previous == liveRateCells.end() || rect.top() > previous->second.bottom(),
                "live packet-rate fields wrap downward within each column");
    }
    require(panel.findChild<QLabel *>(QStringLiteral("epsilonLivePacketRateValue_40"))->text() ==
                QStringLiteral("250.0 Hz") &&
                panel.findChild<QLabel *>(QStringLiteral("epsilonLivePacketRateValue_53"))->text().contains(
                    QStringLiteral("未收到")),
            "live packet-rate card shows measured rates and explicitly marks missing packets");
    auto *profileSummary = panel.findChild<QLabel *>(QStringLiteral("epsilonProfileSummaryValue"));
    require(profileSummary != nullptr && profileSummary->text() == QStringLiteral("逐项设置"),
            "configuration summary reflects the packet-rate editor state");

    struct ActionProbe
    {
        const char *objectName;
        bool emitted = false;
    };
    ActionProbe recommended{"epsilonRecommendedConfigButton"};
    ActionProbe save{"epsilonSaveButton"};
    ActionProbe rtcm{"epsilonRtcmPortButton"};
    ActionProbe reconfigure{"epsilonReconfigureButton"};
    QObject::connect(&panel, &EpsilonConfigPanel::recommendedProfileRequested,
                     &panel, [&recommended]() { recommended.emitted = true; });
    QObject::connect(&panel, &EpsilonConfigPanel::saveRequested,
                     &panel, [&save]() { save.emitted = true; });
    QObject::connect(&panel, &EpsilonConfigPanel::rtcmPortRequested,
                     &panel, [&rtcm]() { rtcm.emitted = true; });
    QObject::connect(&panel, &EpsilonConfigPanel::reconfigureRequested,
                     &panel, [&reconfigure]() { reconfigure.emitted = true; });

    for (ActionProbe *probe : {&recommended, &save, &rtcm, &reconfigure})
    {
        auto *button = panel.findChild<QPushButton *>(QString::fromLatin1(probe->objectName));
        require(button != nullptr, "EPSILON operation button exists");
        require(button->focusPolicy() == Qt::TabFocus && !button->accessibleName().isEmpty(),
                "EPSILON operation button is accessible and keyboard focusable");
        require(objectNames.insert(button->objectName()).second,
                "EPSILON operation button objectName is unique");
        button->click();
        require(probe->emitted, "EPSILON operation button emits its semantic request");
    }
    require(panel.findChild<QPushButton *>(QStringLiteral("epsilonRtkConfigButton")) == nullptr &&
                panel.findChild<QLabel *>(QStringLiteral("epsilonRtkSettingName")) == nullptr &&
                panel.findChild<QLabel *>(QStringLiteral("epsilonRtkSettingDescription")) == nullptr,
            "EPSILON device settings route differential positioning through the navigation bar");
    auto *reconfigureName = panel.findChild<QLabel *>(QStringLiteral("epsilonReconfigureSettingName"));
    auto *reconfigureDescription = panel.findChild<QLabel *>(QStringLiteral("epsilonReconfigureSettingDescription"));
    require(reconfigureName != nullptr && reconfigureName->text() == QStringLiteral("应用已保存配置") &&
                reconfigureDescription != nullptr &&
                reconfigureDescription->text().contains(QStringLiteral("本机已保存")) &&
                reconfigureDescription->text().contains(QStringLiteral("不会保存")),
            "EPSILON saved-configuration action explains its local source and unsaved-change behavior");
    auto *deviceSettingsCard = panel.findChild<QFrame *>(QStringLiteral("epsilonDeviceSettingsCard"));
    auto *rtcmDescription = panel.findChild<QLabel *>(QStringLiteral("epsilonRtcmSettingDescription"));
    auto *reconfigureButton = panel.findChild<QPushButton *>(QStringLiteral("epsilonReconfigureButton"));
    require(deviceSettingsCard != nullptr && rtcmDescription != nullptr && reconfigureButton != nullptr,
            "EPSILON device settings exposes labels and actions for geometry checks");
    const QRect rtcmDescriptionRect(rtcmDescription->mapTo(deviceSettingsCard, QPoint(0, 0)),
                                    rtcmDescription->size());
    const QRect rtcmDevicePortRect(rtcmDevicePortCombo->mapTo(deviceSettingsCard, QPoint(0, 0)),
                                   rtcmDevicePortCombo->size());
    const QRect reconfigureDescriptionRect(
        reconfigureDescription->mapTo(deviceSettingsCard, QPoint(0, 0)),
        reconfigureDescription->size());
    const QRect reconfigureButtonRect(reconfigureButton->mapTo(deviceSettingsCard, QPoint(0, 0)),
                                      reconfigureButton->size());
    const int reconfigureDescriptionRequiredHeight =
        reconfigureDescription->heightForWidth(reconfigureDescription->width());
    require(reconfigureDescriptionRect.width() > rtcmDescriptionRect.width() + 120 &&
                reconfigureDescriptionRect.right() >= rtcmDevicePortRect.right() - 2 &&
                reconfigureDescriptionRect.right() < reconfigureButtonRect.left() &&
                (reconfigureDescriptionRequiredHeight <= 0 ||
                 reconfigureDescriptionRequiredHeight <= reconfigureDescriptionRect.height()),
            "EPSILON saved-configuration description uses the empty selector column without clipping wrapped text");
    auto *rtcmButton = panel.findChild<QPushButton *>(QStringLiteral("epsilonRtcmPortButton"));
    require(rtcmButton != nullptr &&
                !rtcmButton->toolTip().contains(QStringLiteral("port 2"), Qt::CaseInsensitive) &&
                !rtcmButton->toolTip().contains(QStringLiteral("第二通信")),
            "RTCM input action no longer hardcodes COMM2 in user-facing help text");
    require(rtcmDevicePortCombo->focusPolicy() == Qt::TabFocus &&
                !rtcmDevicePortCombo->accessibleName().isEmpty() &&
                !rtcmDevicePortCombo->toolTip().isEmpty(),
            "RTCM input selector is accessible and keyboard focusable");

    panel.setAvailable(false);
    auto *availabilitySummary = panel.findChild<QLabel *>(
        QStringLiteral("epsilonAvailabilitySummaryValue"));
    require(!combos.front()->isEnabled(),
            "panel unavailable state disables interactive controls");
    require(!rtcmDevicePortCombo->isEnabled(),
            "panel unavailable state disables the RTCM input selector");
    require(availabilitySummary != nullptr && availabilitySummary->text() == QStringLiteral("不可用"),
            "configuration summary reports unavailable operations without fabricating device state");
    panel.setAvailable(true);
    require(combos.front()->isEnabled(),
            "panel available state re-enables interactive controls");

    panel.resize(560, 900);
    QApplication::processEvents();
    for (QComboBox *combo : combos)
    {
        require(combo->property("epsilonPacketGridColumn").toInt() == 0,
                "narrow panel collapses packet-rate fields to one visual column");
        require(combo->property("epsilonPacketGroupFieldColumn").toInt() == 0,
                "narrow panel keeps packet-rate fields in one internal category column");
        const QRect comboRect(combo->mapTo(&panel, QPoint(0, 0)), combo->size());
        require(panel.rect().contains(comboRect),
                "narrow packet-rate controls remain inside the panel");
    }
    panel.resize(1100, 420);
    QApplication::processEvents();
    wideColumns.clear();
    for (QComboBox *combo : combos)
    {
        wideColumns.insert(combo->property("epsilonPacketGridColumn").toInt());
    }
    require(wideColumns == QSet<int>{0, 1, 2, 3},
            "wide panel restores the outer two-by-two and inner two-column packet-rate layout");

    panel.setEnglish(true);
    require(panel.accessibleName() == QStringLiteral("EPSILON Configuration"),
            "English accessible name follows panel language");
    require(rtcmDevicePortCombo->currentData().toInt() == 3 &&
                rtcmDevicePortCombo->currentText().contains(QStringLiteral("COMM3")) &&
                rtcmDevicePortCombo->itemText(0).contains(QStringLiteral("(default)")) &&
                rtcmDevicePortCombo->accessibleName() == QStringLiteral("EPSILON RTCM input port"),
            "English RTCM input selector keeps the selected device port on the visible card");
    require(panel.findChild<QLabel *>(QStringLiteral("epsilonPacketGroupInertialFusion"))->text() ==
                QStringLiteral("Inertial and Fusion") &&
                panel.findChild<QLabel *>(QStringLiteral("epsilonPacketGroupGnssPosition"))->text() ==
                    QStringLiteral("GNSS and Position"),
            "EPSILON packet group titles follow the active language");
    require(liveRateTitle->text() == QStringLiteral("Live Packet Rates") &&
                panel.findChild<QLabel *>(QStringLiteral("epsilonLivePacketRateValue_53"))->text().contains(
                    QStringLiteral("no packets")),
            "live packet-rate card follows the active language");
    require(rtcmButton->toolTip().contains(QStringLiteral("communication port")) &&
                !rtcmButton->toolTip().contains(QStringLiteral("port 2"), Qt::CaseInsensitive),
            "English RTCM action help keeps the selectable device-port wording");
    panel.setEnglish(false);
    require(!panel.accessibleName().isEmpty(), "Chinese accessible name remains available");

    std::cout << "epsilon config panel tests passed\n";
    return 0;
}
