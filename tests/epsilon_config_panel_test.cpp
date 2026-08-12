#include "ground/devices/DeviceRatePolicy.h"
#include "ground/navigation/EpsilonConfigPanel.h"
#include "shared/theme/AppTheme.h"

#include <QApplication>
#include <QCheckBox>
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
                panel.findChild<QFrame *>(QStringLiteral("epsilonOutputCard")) != nullptr &&
                panel.findChild<QFrame *>(QStringLiteral("epsilonDeviceSettingsCard")) != nullptr &&
                sectionCardCount == 3,
            "panel exposes exactly three EPSILON business section cards");
    require(panel.findChild<QWidget *>(QStringLiteral("epsilonActionsContainer")) != nullptr &&
                panel.findChild<QWidget *>(QStringLiteral("epsilonPacketActionPanel")) != nullptr,
            "panel keeps separate grouped and primary action containers");
    const QString panelStyle = panel.styleSheet();
    require(panel.testAttribute(Qt::WA_StyledBackground) &&
                panelStyle.contains(QStringLiteral(
                    "QFrame#epsilonSectionCard { background-color:")) &&
                panelStyle.contains(QStringLiteral(
                    "QWidget[epsilonConfigCardBody=\"true\"] { background-color:")) &&
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

    struct PacketGroupExpectation
    {
        const char *objectName;
        const char *title;
        std::set<uint8_t> packetIds;
    };
    const std::vector<PacketGroupExpectation> packetGroups = {
        {"epsilonPacketGroupInertialFusion", "惯导与融合", {0x40, 0x41, 0x42}},
        {"epsilonPacketGroupSystemDiagnostics", "系统与诊断", {0x50, 0x53}},
        {"epsilonPacketGroupGnssPosition", "GNSS 与位置", {0x59, 0x5A, 0x5C, 0x5D}},
        {"epsilonPacketGroupAttitudeRepresentation", "姿态表示", {0x63, 0x64}},
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

    auto *outputCard = panel.findChild<QFrame *>(QStringLiteral("epsilonOutputCard"));
    require(outputCard != nullptr, "EPSILON output card exists for packet group geometry");
    int previousGroupHeaderBottom = -1;
    for (int groupIndex = 0; groupIndex < static_cast<int>(packetGroups.size()); ++groupIndex)
    {
        const PacketGroupExpectation& group = packetGroups.at(groupIndex);
        auto *groupLabel = panel.findChild<QLabel *>(QString::fromLatin1(group.objectName));
        const QRect groupRect(groupLabel->mapTo(outputCard, QPoint(0, 0)), groupLabel->size());
        int firstPacketTop = std::numeric_limits<int>::max();
        for (QComboBox *combo : combos)
        {
            const auto packetId = static_cast<uint8_t>(combo->property("epsilonPacketId").toUInt());
            if (combo->property("epsilonPacketGroup").toInt() != groupIndex)
            {
                continue;
            }
            const QRect comboRect(combo->mapTo(outputCard, QPoint(0, 0)), combo->size());
            firstPacketTop = std::min(firstPacketTop, comboRect.top());
            require(group.packetIds.find(packetId) != group.packetIds.end(),
                    "packet group geometry contains only its assigned packet controls");
        }
        require(firstPacketTop > groupRect.bottom() && groupRect.top() > previousGroupHeaderBottom,
                "EPSILON packet group header precedes its fields and follows the previous group");
        previousGroupHeaderBottom = groupRect.bottom();
    }

    QSet<int> wideColumns;
    for (QComboBox *combo : combos)
    {
        wideColumns.insert(combo->property("epsilonPacketGridColumn").toInt());
    }
    require(wideColumns == QSet<int>{0, 1},
            "wide panel lays packet-rate fields out in two visual columns");

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
    panel.setCustomPacketProfileEnabled(true);
    require(panel.customPacketProfileEnabled(), "semantic custom-profile state enables");
    auto *profileSummary = panel.findChild<QLabel *>(QStringLiteral("epsilonProfileSummaryValue"));
    require(profileSummary != nullptr && profileSummary->text() == QStringLiteral("自定义"),
            "configuration summary reflects the real custom-profile state");
    panel.setCustomPacketProfileEnabled(false);
    require(!panel.customPacketProfileEnabled(), "semantic custom-profile state disables");
    require(profileSummary->text() == QStringLiteral("分组模式"),
            "configuration summary reflects the real grouped-profile state");

    auto *customCheck = panel.findChild<QCheckBox *>(QStringLiteral("epsilonPacketCustomCheck"));
    require(customCheck != nullptr && customCheck->focusPolicy() == Qt::TabFocus &&
                !customCheck->accessibleName().isEmpty(),
            "custom-profile control is accessible and keyboard focusable");

    struct ActionProbe
    {
        const char *objectName;
        bool emitted = false;
    };
    ActionProbe recommended{"epsilonRecommendedConfigButton"};
    ActionProbe grouped{"epsilonGroupedConfigButton"};
    ActionProbe save{"epsilonSaveButton"};
    ActionProbe rtcm{"epsilonRtcmPortButton"};
    ActionProbe reconfigure{"epsilonReconfigureButton"};
    ActionProbe rtk{"epsilonRtkConfigButton"};
    QObject::connect(&panel, &EpsilonConfigPanel::recommendedProfileRequested,
                     &panel, [&recommended]() { recommended.emitted = true; });
    QObject::connect(&panel, &EpsilonConfigPanel::groupedProfileRequested,
                     &panel, [&grouped]() { grouped.emitted = true; });
    QObject::connect(&panel, &EpsilonConfigPanel::saveRequested,
                     &panel, [&save]() { save.emitted = true; });
    QObject::connect(&panel, &EpsilonConfigPanel::rtcmPortRequested,
                     &panel, [&rtcm]() { rtcm.emitted = true; });
    QObject::connect(&panel, &EpsilonConfigPanel::reconfigureRequested,
                     &panel, [&reconfigure]() { reconfigure.emitted = true; });
    QObject::connect(&panel, &EpsilonConfigPanel::rtkConfigRequested,
                     &panel, [&rtk]() { rtk.emitted = true; });

    for (ActionProbe *probe : {&recommended, &grouped, &save, &rtcm, &reconfigure, &rtk})
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

    panel.setAvailable(false);
    auto *availabilitySummary = panel.findChild<QLabel *>(
        QStringLiteral("epsilonAvailabilitySummaryValue"));
    require(!customCheck->isEnabled() && !combos.front()->isEnabled(),
            "panel unavailable state disables interactive controls");
    require(availabilitySummary != nullptr && availabilitySummary->text() == QStringLiteral("不可用"),
            "configuration summary reports unavailable operations without fabricating device state");
    panel.setAvailable(true);
    require(customCheck->isEnabled() && combos.front()->isEnabled(),
            "panel available state re-enables interactive controls");

    panel.resize(560, 900);
    QApplication::processEvents();
    for (QComboBox *combo : combos)
    {
        require(combo->property("epsilonPacketGridColumn").toInt() == 0,
                "narrow panel collapses packet-rate fields to one visual column");
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
    require(wideColumns == QSet<int>{0, 1},
            "wide panel restores the two-column packet-rate layout");

    panel.setEnglish(true);
    require(panel.accessibleName() == QStringLiteral("EPSILON Configuration"),
            "English accessible name follows panel language");
    require(panel.findChild<QLabel *>(QStringLiteral("epsilonPacketGroupInertialFusion"))->text() ==
                QStringLiteral("Inertial and Fusion") &&
                panel.findChild<QLabel *>(QStringLiteral("epsilonPacketGroupGnssPosition"))->text() ==
                    QStringLiteral("GNSS and Position"),
            "EPSILON packet group titles follow the active language");
    panel.setEnglish(false);
    require(!panel.accessibleName().isEmpty(), "Chinese accessible name remains available");

    std::cout << "epsilon config panel tests passed\n";
    return 0;
}
