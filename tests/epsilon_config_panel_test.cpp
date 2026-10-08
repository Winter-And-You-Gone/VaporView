#include "ground/devices/DeviceRatePolicy.h"
#include "ground/navigation/EpsilonConfigPanel.h"
#include "shared/theme/AppTheme.h"
#include "test_ui_helpers.h"
#include "test_settings_sandbox.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QStackedWidget>
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
        if (card->property("epsilonConfigCard").toBool() &&
            !card->property("epsilonParameterCard").toBool())
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
    require(options.size() == 12, "EPSILON policy exposes 12 packet options including PPK");
    require(combos.size() == 12, "panel exposes all 12 packet-rate controls");
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
        {"epsilonPacketGroupGnssPosition", "GNSS 与位置", {0x59, 0x5A, 0x5C, 0x5D, 0x77}},
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
    require(panel.packetRates() == defaults, "semantic packet-rate setter and getter preserve all 12 values");
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
    liveData.raw_satellite_epoch_rate_hz = 5.0;
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
    require(liveRateValueCount == 12, "live packet-rate card exposes all 12 packet rates");
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
    require(panel.width() < 980, "wide grid minimum does not prevent the narrow layout breakpoint");
    for (QWidget *field : panel.findChildren<QWidget *>())
        if (field->property("epsilonLivePacketRateField").toBool())
        {
            require(field->property("epsilonLivePacketGridColumn").toInt() < 2,
                    "narrow live packet rates reflow into two columns");
            for (QLabel *label : field->findChildren<QLabel *>())
                require(label->width() >= label->fontMetrics().horizontalAdvance(label->text()),
                        "narrow live rate labels and values remain fully visible");
        }
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

    auto *pages = panel.findChild<QStackedWidget *>(QStringLiteral("epsilonSettingsPages"));
    auto *installationTab = panel.findChild<QPushButton *>(QStringLiteral("epsilonSettingsTab_1"));
    auto *fusionTab = panel.findChild<QPushButton *>(QStringLiteral("epsilonSettingsTab_2"));
    auto *communicationTab = panel.findChild<QPushButton *>(QStringLiteral("epsilonSettingsTab_0"));
    auto *readButton = panel.findChild<QPushButton *>(QStringLiteral("epsilonSettingsReadButton"));
    auto *saveButton = panel.findChild<QPushButton *>(QStringLiteral("epsilonSaveButton"));
    auto *restartButton = panel.findChild<QPushButton *>(QStringLiteral("epsilonDeviceRestartButton"));
    auto *settingsStatus = panel.findChild<QLabel *>(QStringLiteral("epsilonSettingsStatus"));
    auto *settingsTabs = panel.findChild<QFrame *>(QStringLiteral("epsilonSettingsTabs"));
    auto *settingsTrack = panel.findChild<QFrame *>(QStringLiteral("epsilonSettingsTabTrack"));
    require(pages && pages->count() == 3 && installationTab && fusionTab && communicationTab &&
            readButton && saveButton && restartButton && settingsStatus,
            "EPSILON exposes three internal pages and shared read/save/restart actions");
    require(settingsTabs && settingsTabs->height() == 36 && settingsTrack &&
            settingsTrack->parentWidget() == settingsTabs &&
            communicationTab->parentWidget() == settingsTrack &&
            communicationTab->height() == installationTab->height() &&
            installationTab->height() == fusionTab->height() &&
            std::abs(communicationTab->width() - installationTab->width()) <= 1 &&
            std::abs(installationTab->width() - fusionTab->width()) <= 1,
            "parameter navigation uses the same equal-height three-segment capsule geometry");
    installationTab->click();
    panel.setSettingsAvailable(true);
    QApplication::processEvents();
    auto *firstInstallationRow = panel.findChild<QWidget *>(QStringLiteral("epsilonParameterRow_BODY_TO_VEHICLE_ALGN_ROLL"));
    require(firstInstallationRow != nullptr, "installation has a first parameter row");
    const int firstInstallationTop = firstInstallationRow->mapTo(&panel, QPoint()).y();
    for (auto *button : {readButton, saveButton, restartButton})
        require(button->mapTo(&panel, QPoint()).y() + button->height() <= firstInstallationTop,
                "parameter read/save/restart actions precede the first field");
    require(settingsStatus->mapTo(&panel, QPoint()).y() + settingsStatus->height() <= firstInstallationTop,
            "parameter operation status appears above the form");
    bool installationHasSecondColumn = false;
    for (QWidget *row : panel.findChild<QWidget *>(QStringLiteral("epsilonInstallationPage"))->findChildren<QWidget *>())
        installationHasSecondColumn |= row->property("epsilonSettingsFieldColumn").toInt() == 1;
    require(installationHasSecondColumn, "wide installation form uses two compact columns");
    auto *parametersCard = panel.findChild<QFrame *>(QStringLiteral("epsilonParameterActionsCard"));
    auto requireAlignedParameterCards = [&](int group, bool wide) {
        const QRect outer(parametersCard->mapTo(&panel, QPoint()), parametersCard->size());
        const int count = group == 0 ? 3 : 4;
        for (int i = 0; i < count; ++i)
        {
            auto *card = panel.findChild<QFrame *>(QStringLiteral("epsilonParameterCard_%1_%2").arg(group).arg(i));
            require(card != nullptr, "parameter card exists for edge alignment");
            const QRect rect(card->mapTo(&panel, QPoint()), card->size());
            if (!wide || i % 2 == 0)
                require(rect.left() == outer.left(), "parameter cards share the standard left page inset");
            if (!wide || i % 2 == 1 || (group == 0 && i == 2))
                require(rect.right() == outer.right(), "parameter cards share the standard right page inset");
        }
    };
    require(parametersCard && parametersCard->property("epsilonParameterCard").toBool() &&
            parametersCard->isAncestorOf(readButton) && parametersCard->isAncestorOf(saveButton) &&
            parametersCard->isAncestorOf(restartButton) && parametersCard->isAncestorOf(settingsStatus),
            "device actions, status, and guidance share the standard parameter card");
    requireAlignedParameterCards(0, true);
    for (int i = 0; i < 3; ++i)
        require(panel.findChild<QFrame *>(QStringLiteral("epsilonParameterCard_0_%1").arg(i)) != nullptr,
                "installation uses three standard grouped cards");
    for (int i = 0; i < 4; ++i)
        require(panel.findChild<QFrame *>(QStringLiteral("epsilonParameterCard_1_%1").arg(i)) != nullptr,
                "fusion uses four standard grouped cards");
    auto *leverCard = panel.findChild<QFrame *>(QStringLiteral("epsilonParameterCard_0_1"));
    int previousBottom = -1;
    for (const auto *axis : {"X", "Y", "Z"})
    {
        auto *axisRow = panel.findChild<QWidget *>(QStringLiteral("epsilonParameterRow_GNSS_L_IMU_ANT1_%1").arg(QString::fromLatin1(axis)));
        require(axisRow && leverCard->isAncestorOf(axisRow), "XYZ lever-arm fields remain in their common card");
        const QRect rect(axisRow->mapTo(leverCard, QPoint()), axisRow->size());
        require(rect.top() > previousBottom, "XYZ fields are ordered vertically without overlaps");
        previousBottom = rect.bottom();
    }
    require(pages->currentIndex() == 1 && readButton->isVisible() && !saveButton->isEnabled(),
            "unread settings cannot be saved");
    require(firstInstallationRow->findChildren<QLabel *>().last()->isHidden(),
            "ordinary unread fields omit repetitive visible status lines");
    auto *dualXRow = panel.findChild<QWidget *>(QStringLiteral("epsilonParameterRow_GNSS_L_ANT2_ANT1_X"));
    auto *dualYRow = panel.findChild<QWidget *>(QStringLiteral("epsilonParameterRow_GNSS_L_ANT2_ANT1_Y"));
    auto *dualHeadingRow = panel.findChild<QWidget *>(QStringLiteral("epsilonParameterRow_GNSS_ANTS_HEADING_BIAS"));
    require(dualXRow && dualYRow && dualHeadingRow &&
            dualYRow->geometry().top() > dualXRow->geometry().bottom() &&
            dualHeadingRow->geometry().left() > dualXRow->geometry().right(),
            "wide dual antenna card keeps XYZ on the left and heading/baseline on the right");
    for (QWidget *editor : panel.findChildren<QWidget *>())
        if (editor->property("epsilonParameterName").isValid())
            require(!editor->isEnabled(), "unread parameter editors remain disabled");

    const auto& installationDescriptors = VaporView::epsilonParameterDescriptors(VaporView::EpsilonSettingsGroup::Installation);
    const auto editable = std::find_if(installationDescriptors.begin(), installationDescriptors.end(),
        [](const auto& descriptor) { return descriptor.writable && descriptor.kind == VaporView::EpsilonParameterKind::Real; });
    require(editable != installationDescriptors.end(), "installation has a verified editable real parameter");
    const auto *unsupportedDescriptor = &installationDescriptors.back();
    if (unsupportedDescriptor->name == editable->name)
        unsupportedDescriptor = &installationDescriptors.front();
    VaporView::EpsilonSettingsSnapshot snapshot;
    snapshot.group = VaporView::EpsilonSettingsGroup::Installation;
    snapshot.values[editable->name] = 0;
    snapshot.values[installationDescriptors[1].name] = 0;
    snapshot.unsupported.push_back(unsupportedDescriptor->name);
    panel.setSettingsSnapshot(snapshot);
    auto *editor = panel.findChild<QDoubleSpinBox *>(QStringLiteral("epsilonParameter_%1").arg(QString::fromStdString(editable->name)));
    auto *unsupportedEditor = panel.findChild<QWidget *>(QStringLiteral("epsilonParameter_%1").arg(QString::fromStdString(unsupportedDescriptor->name)));
    require(editor && editor->isEnabled() && unsupportedEditor && !unsupportedEditor->isEnabled() &&
            !saveButton->isEnabled(), "successful reads enable only supported fields and start clean");
    require(editor->width() <= 170, "numeric device values use compact editors instead of full-width fields");
    require(settingsStatus->text().contains(QStringLiteral("已读取 2 项")) &&
            settingsStatus->text().contains(QStringLiteral("2 项可编辑")) &&
            settingsStatus->text().contains(QStringLiteral("1 项不支持")) &&
            !settingsStatus->text().contains(QStringLiteral("后才能编辑")),
            "read completion status reports current group values and unsupported fields");
    auto *unsupportedRow = panel.findChild<QWidget *>(QStringLiteral("epsilonParameterRow_%1").arg(QString::fromStdString(unsupportedDescriptor->name)));
    bool explainsUnsupported = false;
    for (auto *label : unsupportedRow->findChildren<QLabel *>())
        explainsUnsupported |= label->text().contains(QStringLiteral("不支持"));
    require(explainsUnsupported, "unsupported firmware parameters have a visible explanation");
    editor->setValue(1);
    require(saveButton->isEnabled(), "a supported changed field enables saving");
    fusionTab->click();
    QApplication::processEvents();
    requireAlignedParameterCards(1, true);
    require(panel.currentSettingsGroup() == VaporView::EpsilonSettingsGroup::Fusion && !saveButton->isEnabled(),
            "unread fusion page has independent save eligibility");
    installationTab->click();
    require(editor->value() == 1 && saveButton->isEnabled(), "switching internal pages preserves edits");
    VaporView::EpsilonSettingsOperation captured;
    int settingsApplyCount = 0;
    QObject::connect(&panel, &EpsilonConfigPanel::settingsApplyRequested, [&captured, &settingsApplyCount](const auto& operation) {
        captured = operation;
        ++settingsApplyCount;
    });
    save.emitted = false;
    saveButton->click();
    require(settingsApplyCount == 1 && captured.values.size() == 1 && captured.values.at(editable->name) == 1,
            "parameter save submits only changed successfully read values");
    require(!save.emitted, "device parameter saving does not emit the legacy local packet-profile save signal");
    panel.setSettingsOperationPending(true);
    require(!editor->isEnabled() && !saveButton->isEnabled() && !readButton->isEnabled() &&
            settingsStatus->text().contains(QStringLiteral("正在")), "pending operation disables device actions with visible busy state");
    panel.setSettingsError(QStringLiteral("测试读取失败"));
    panel.setSettingsOperationPending(false);
    require(readButton->isEnabled() && settingsStatus->text() == QStringLiteral("测试读取失败"),
            "failure restores read eligibility while exposing its reason");
    snapshot.values[editable->name] = 1;
    snapshot.values.erase(installationDescriptors[1].name);
    snapshot.saved = true;
    snapshot.readback_verified = true;
    snapshot.restart_required = true;
    panel.setSettingsSnapshot(snapshot, true);
    auto *unchangedEditor = panel.findChild<QWidget *>(QStringLiteral("epsilonParameter_%1").arg(QString::fromStdString(installationDescriptors[1].name)));
    require(!saveButton->isEnabled() && restartButton->isEnabled() &&
            settingsStatus->text().contains(QStringLiteral("回读确认")) && unchangedEditor && unchangedEditor->isEnabled(),
            "saved ACK confirms changed values and preserves previously read unchanged editors");
    snapshot.saved = false;
    snapshot.restart_required = false;
    panel.setSettingsSnapshot(snapshot, true);
    require(restartButton->isEnabled() && unchangedEditor->isEnabled(),
            "no-op apply partial snapshot preserves unchanged editors and pending restart");
    require(!settingsStatus->text().contains(QStringLiteral("后才能编辑")),
            "no-op apply does not revert the status to an unread instruction");
    panel.resize(620, 900);
    panel.setEnglish(true);
    QApplication::processEvents();
    requireAlignedParameterCards(0, false);
    for (auto *button : {communicationTab, installationTab, fusionTab})
        require(button->fontMetrics().horizontalAdvance(button->text()) + 20 <= button->width(),
                "equal navigation segments retain enough room for their complete English labels");
    require(dualHeadingRow->geometry().left() == dualXRow->geometry().left() &&
            dualHeadingRow->geometry().top() > dualYRow->geometry().bottom(),
            "narrow dual antenna card restores the continuous single-column parameter order");
    require(installationTab->text().contains(QStringLiteral("Installation")) &&
            settingsStatus->text().contains(QStringLiteral("readback confirmed")), "parameter pages translate labels and confirmation state");
    for (auto *button : {installationTab, fusionTab, communicationTab, readButton, saveButton, restartButton})
    {
        const QRect rect(button->mapTo(&panel, QPoint()), button->size());
        require(panel.rect().contains(rect), "narrow English page keeps navigation and action buttons inside the panel");
    }
    for (QWidget *row : panel.findChild<QWidget *>(QStringLiteral("epsilonInstallationPage"))->findChildren<QWidget *>())
    {
        if (!row->property("epsilonSettingsFieldColumn").isValid()) continue;
        require(row->property("epsilonSettingsFieldColumn").toInt() == 0,
                "narrow installation form collapses to one column");
        const QRect rect(row->mapTo(&panel, QPoint()), row->size());
        require(rect.left() >= 0 && rect.right() < panel.width(), "narrow parameter field rows fit the available width");
    }
    fusionTab->click();
    QApplication::processEvents();
    requireAlignedParameterCards(1, false);
    auto *unreadDynamics = panel.findChild<QComboBox *>(QStringLiteral("epsilonParameter_DYNAMICS_MODEL"));
    require(unreadDynamics && unreadDynamics->currentIndex() == -1 && unreadDynamics->width() >= 190,
            "unread dynamics combo retains a visible input without inventing a selected value");
    const QRect dynamicsRect(unreadDynamics->geometry());
    for (auto *label : unreadDynamics->parentWidget()->findChildren<QLabel *>())
        if (label->property("epsilonParameterLabel").toBool())
            require(!label->geometry().intersects(dynamicsRect), "dynamics label and stable-width input do not overlap");
    for (auto *check : panel.findChild<QWidget *>(QStringLiteral("epsilonFusionPage"))->findChildren<QCheckBox *>())
        require(check->text() == QStringLiteral("Not read"), "unknown fusion booleans do not look like enabled values");
    VaporView::EpsilonSettingsSnapshot fusionSnapshot;
    fusionSnapshot.group = VaporView::EpsilonSettingsGroup::Fusion;
    const auto& fusionDescriptors = VaporView::epsilonParameterDescriptors(fusionSnapshot.group);
    const auto boolean = std::find_if(fusionDescriptors.begin(), fusionDescriptors.end(), [](const auto& descriptor) {
        return descriptor.writable && descriptor.kind == VaporView::EpsilonParameterKind::Boolean;
    });
    const auto enumeration = std::find_if(fusionDescriptors.begin(), fusionDescriptors.end(), [](const auto& descriptor) {
        return descriptor.writable && descriptor.kind == VaporView::EpsilonParameterKind::Enumeration;
    });
    require(boolean != fusionDescriptors.end() && enumeration != fusionDescriptors.end(), "fusion exposes boolean and enum parameters");
    fusionSnapshot.values[boolean->name] = 0;
    fusionSnapshot.values[enumeration->name] = enumeration->options.front().value;
    panel.setSettingsSnapshot(fusionSnapshot);
    auto *booleanEditor = panel.findChild<QCheckBox *>(QStringLiteral("epsilonParameter_%1").arg(QString::fromStdString(boolean->name)));
    auto *enumEditor = panel.findChild<QComboBox *>(QStringLiteral("epsilonParameter_%1").arg(QString::fromStdString(enumeration->name)));
    require(booleanEditor && enumEditor && booleanEditor->isEnabled() && enumEditor->isEnabled(), "read boolean and enum editors become available");
    require(booleanEditor->property("epsilonThemedIndicator").toBool() &&
            booleanEditor->styleSheet().contains(QStringLiteral("image: none")),
            "fusion checkbox uses a local theme-painted indicator instead of the dark native glyph");
    QApplication::processEvents();
    auto *firstFusionRow = booleanEditor->parentWidget();
    for (auto *button : {readButton, saveButton, restartButton})
        require(button->mapTo(&panel, QPoint()).y() + button->height() <= firstFusionRow->mapTo(&panel, QPoint()).y(),
                "fusion operation actions remain above the first field after switching pages");
    require(!saveButton->isEnabled() && !booleanEditor->isTristate() && !booleanEditor->isChecked(),
            "reading an unchecked boolean clears unknown state without creating a dirty value");
    booleanEditor->setChecked(true);
    enumEditor->setCurrentIndex(1);
    saveButton->click();
    require(captured.group == fusionSnapshot.group && captured.values.size() == 2 &&
            captured.values.at(boolean->name) == 1 && captured.values.at(enumeration->name) == enumeration->options[1].value,
            "fusion submission preserves typed boolean and enumeration values");
    fusionSnapshot.values[enumeration->name] = 99;
    fusionSnapshot.values[boolean->name] = 7;
    panel.setSettingsSnapshot(fusionSnapshot);
    const int applyCountBeforeUnknown = settingsApplyCount;
    saveButton->click();
    require(!saveButton->isEnabled() && settingsApplyCount == applyCountBeforeUnknown &&
            !enumEditor->isEnabled() && enumEditor->currentText().contains(QStringLiteral("99")) &&
            !booleanEditor->isEnabled() && booleanEditor->checkState() == Qt::PartiallyChecked,
            "unknown enum and boolean device values stay visible, read only, and cannot create a submission");
    installationTab->click();
    VaporView::EpsilonSettingsSnapshot outOfRange;
    outOfRange.group = VaporView::EpsilonSettingsGroup::Installation;
    outOfRange.values[editable->name] = editable->maximum + 10;
    outOfRange.values[installationDescriptors[1].name] = 0;
    panel.setSettingsSnapshot(outOfRange);
    const int applyCountBeforeRange = settingsApplyCount;
    saveButton->click();
    require(editor->value() == editable->maximum + 10 && !editor->isEnabled() &&
            !saveButton->isEnabled() && settingsApplyCount == applyCountBeforeRange,
            "out of range device values are displayed without clamping or automatically becoming dirty");
    auto *otherEditor = qobject_cast<QDoubleSpinBox *>(unchangedEditor);
    require(otherEditor && otherEditor->isEnabled(), "an unrelated supported parameter remains editable");
    otherEditor->setValue(1);
    saveButton->click();
    require(captured.values.size() == 1 && captured.values.count(editable->name) == 0 &&
            captured.values.at(installationDescriptors[1].name) == 1,
            "saving a different parameter never writes a clamped out of range device value");
    panel.setSettingsAvailable(false);
    require(!readButton->isEnabled() && !restartButton->isEnabled() && !editor->isEnabled(),
            "offline parameter operations remain disabled independently of packet-rate editing");
    panel.invalidateSettings();
    panel.setSettingsAvailable(true);
    require(!editor->isEnabled() && editor->text() == QStringLiteral("--") && !restartButton->isEnabled(),
            "device invalidation clears current values and pending restart state");
    communicationTab->click();
    QApplication::processEvents();
    require(saveButton->isEnabled() && !readButton->isVisible(), "communication page retains offline save semantics");
    require(saveButton->mapTo(&panel, QPoint()).y() >= deviceSettingsCard->mapTo(&panel, QPoint()).y() + deviceSettingsCard->height(),
            "communication save action remains below its existing four cards");
    panel.setEnglish(false);
    panel.resize(1100, 420);
    std::cout << "epsilon config panel tests passed\n";
    return 0;
}
