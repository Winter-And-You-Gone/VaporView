#include "ground/devices/DeviceRatePolicy.h"
#include "ground/navigation/EpsilonConfigPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>

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
    panel.resize(1100, panel.sizeHint().height());

    const auto &options = epsilonPacketConfigOptions();
    const QList<QComboBox *> combos = packetRateCombos(panel);
    require(options.size() == 11, "EPSILON policy exposes exactly 11 packet options");
    require(combos.size() == 11, "panel exposes exactly 11 packet-rate controls");

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
    }

    const std::map<uint8_t, int> defaults = defaultEpsilonPacketRates();
    panel.setPacketRates(defaults);
    require(panel.packetRates() == defaults, "semantic packet-rate setter and getter preserve all 11 values");
    panel.setCustomPacketProfileEnabled(true);
    require(panel.customPacketProfileEnabled(), "semantic custom-profile state enables");
    panel.setCustomPacketProfileEnabled(false);
    require(!panel.customPacketProfileEnabled(), "semantic custom-profile state disables");

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
    require(!customCheck->isEnabled() && !combos.front()->isEnabled(),
            "panel unavailable state disables interactive controls");
    panel.setAvailable(true);
    require(customCheck->isEnabled() && combos.front()->isEnabled(),
            "panel available state re-enables interactive controls");

    panel.setEnglish(true);
    require(panel.accessibleName() == QStringLiteral("EPSILON Configuration"),
            "English accessible name follows panel language");
    panel.setEnglish(false);
    require(!panel.accessibleName().isEmpty(), "Chinese accessible name remains available");

    std::cout << "epsilon config panel tests passed\n";
    return 0;
}
