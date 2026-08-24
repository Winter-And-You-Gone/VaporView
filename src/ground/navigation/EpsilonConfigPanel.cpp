#include "ground/navigation/EpsilonConfigPanel.h"

#include "ground/devices/DeviceRatePolicy.h"
#include "ground/main/GroundMainWindowSupport.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"

#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace VaporView::Ground::Navigation
{
namespace
{

constexpr int kTwoColumnMinimumWidth = 980;
constexpr int kPacketWideGridColumnCount = 5;
constexpr int kPacketOuterGroupColumns = 2;
constexpr int kPacketInnerFieldColumns = 2;
constexpr int kPacketVisualColumnCount = kPacketOuterGroupColumns * kPacketInnerFieldColumns;

enum class PacketRateGroup
{
    SystemAndDiagnostics,
    AttitudeRepresentation,
    InertialAndFusion,
    GnssAndPosition,
};

constexpr int kPacketRateGroupCount = 4;

PacketRateGroup packetRateGroupForId(quint8 packetId)
{
    switch (packetId)
    {
    case 0x40:
    case 0x41:
    case 0x42:
        return PacketRateGroup::InertialAndFusion;
    case 0x50:
    case 0x53:
        return PacketRateGroup::SystemAndDiagnostics;
    case 0x59:
    case 0x5A:
    case 0x5C:
    case 0x5D:
        return PacketRateGroup::GnssAndPosition;
    case 0x63:
    case 0x64:
        return PacketRateGroup::AttitudeRepresentation;
    default:
        return PacketRateGroup::InertialAndFusion;
    }
}

int packetRateGroupIndex(PacketRateGroup group)
{
    return static_cast<int>(group);
}

QString packetRateGroupObjectName(PacketRateGroup group)
{
    switch (group)
    {
    case PacketRateGroup::InertialAndFusion:
        return QStringLiteral("epsilonPacketGroupInertialFusion");
    case PacketRateGroup::SystemAndDiagnostics:
        return QStringLiteral("epsilonPacketGroupSystemDiagnostics");
    case PacketRateGroup::GnssAndPosition:
        return QStringLiteral("epsilonPacketGroupGnssPosition");
    case PacketRateGroup::AttitudeRepresentation:
        return QStringLiteral("epsilonPacketGroupAttitudeRepresentation");
    }
    return QString();
}

QString packetRateGroupTitle(PacketRateGroup group, bool english)
{
    switch (group)
    {
    case PacketRateGroup::InertialAndFusion:
        return english ? QStringLiteral("Inertial and Fusion") : QStringLiteral("惯导与融合");
    case PacketRateGroup::SystemAndDiagnostics:
        return english ? QStringLiteral("System and Diagnostics") : QStringLiteral("系统与诊断");
    case PacketRateGroup::GnssAndPosition:
        return english ? QStringLiteral("GNSS and Position") : QStringLiteral("GNSS 与位置");
    case PacketRateGroup::AttitudeRepresentation:
        return english ? QStringLiteral("Attitude Representation") : QStringLiteral("姿态表示");
    }
    return QString();
}

QString rtcmDevicePortText(int portIndex, bool english)
{
    QString text = english
        ? QStringLiteral("COMM%1 input").arg(portIndex)
        : QStringLiteral("串口%1输入").arg(portIndex);
    if (portIndex == 2)
    {
        text += english ? QStringLiteral(" (default)") : QStringLiteral("（默认）");
    }
    return text;
}

struct SectionCard
{
    QFrame *card = nullptr;
    QWidget *title_bar = nullptr;
    QHBoxLayout *title_layout = nullptr;
    QLabel *title = nullptr;
    QVBoxLayout *body_layout = nullptr;
};

SectionCard createSectionCard(QWidget *parent,
                              const QString& objectName,
                              const QString& iconName)
{
    SectionCard result;
    result.card = new QFrame(parent);
    result.card->setObjectName(objectName);
    result.card->setProperty("epsilonConfigCard", true);
    result.card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    configureTopLevelCard(result.card);

    auto *cardLayout = new QVBoxLayout(result.card);
    cardLayout->setContentsMargins(1, 0, 1, 1);
    cardLayout->setSpacing(0);

    auto *titleBar = new QWidget(result.card);
    titleBar->setObjectName(QStringLiteral("sectionTitleBar"));
    titleBar->setFixedHeight(VaporView::Ground::MainSupport::kMainPageTitleBarHeight);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(10, 2, 10, 2);
    titleLayout->setSpacing(8);
    QWidget *titleCluster = nullptr;
    result.title = VaporView::Ground::MainSupport::createSectionTitleCluster(
        titleBar, iconName,
        VaporView::Ground::MainSupport::kMainPageButtonHeight, &titleCluster);
    titleLayout->addWidget(titleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    titleLayout->addStretch(1);
    cardLayout->addWidget(titleBar);
    result.title_bar = titleBar;
    result.title_layout = titleLayout;

    auto *body = new QWidget(result.card);
    body->setObjectName(objectName + QStringLiteral("Body"));
    body->setProperty("epsilonConfigCardBody", true);
    body->setAttribute(Qt::WA_StyledBackground, true);
    body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    result.body_layout = new QVBoxLayout(body);
    result.body_layout->setContentsMargins(12, 10, 12, 12);
    result.body_layout->setSpacing(10);
    cardLayout->addWidget(body);
    return result;
}

struct SummaryField
{
    QWidget *field = nullptr;
    QLabel *name = nullptr;
    QLabel *value = nullptr;
};

SummaryField createSummaryField(QWidget *parent, const QString& objectName)
{
    SummaryField result;
    result.field = new QWidget(parent);
    result.field->setObjectName(objectName);
    result.field->setProperty("epsilonSummaryField", true);
    result.field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(result.field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    result.name = new QLabel(result.field);
    result.name->setProperty("epsilonSummaryName", true);
    result.value = new QLabel(result.field);
    result.value->setProperty("epsilonSummaryValue", true);
    layout->addWidget(result.name);
    layout->addWidget(result.value);
    return result;
}

QComboBox *createPacketRateCombo(QWidget *parent, int width)
{
    auto *combo = new QComboBox(parent);
    combo->setFixedHeight(VaporView::Ground::MainSupport::kMainPageInputHeight);
    combo->setFixedWidth(width);
    combo->setMaxVisibleItems(15);
    VaporView::configureComboBoxPopup(combo, VaporView::isDarkThemeEnabled());
    return combo;
}

QComboBox *createRtcmDevicePortCombo(QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->setFixedHeight(VaporView::Ground::MainSupport::kMainPageInputHeight);
    combo->setMinimumWidth(156);
    combo->setMaximumWidth(190);
    combo->setFocusPolicy(Qt::TabFocus);
    combo->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    for (int portIndex = 2; portIndex <= 5; ++portIndex)
    {
        combo->addItem(QStringLiteral("COMM%1").arg(portIndex), portIndex);
    }
    VaporView::configureComboBoxPopup(combo, VaporView::isDarkThemeEnabled());
    return combo;
}

QPushButton *createActionButton(QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setFixedHeight(VaporView::Ground::MainSupport::kMainPageButtonHeight);
    button->setFocusPolicy(Qt::TabFocus);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return button;
}

double livePacketRateForId(const VaporView::EpsilonData& data, quint8 packetId)
{
    switch (packetId)
    {
    case 0x40:
        return data.imu_packet_rate_hz;
    case 0x41:
        return data.ahrs_packet_rate_hz;
    case 0x42:
        return data.insgps_packet_rate_hz;
    case 0x50:
        return data.sys_state_packet_rate_hz;
    case 0x53:
        return data.status_packet_rate_hz;
    case 0x59:
        return data.raw_gnss_packet_rate_hz;
    case 0x5A:
        return data.satellite_packet_rate_hz;
    case 0x5C:
        return data.geodetic_packet_rate_hz;
    case 0x5D:
        return data.ecef_packet_rate_hz;
    case 0x63:
        return data.euler_orien_packet_rate_hz;
    case 0x64:
        return data.quat_orien_packet_rate_hz;
    default:
        return 0.0;
    }
}

QString livePacketRateText(double rateHz, bool english)
{
    if (!std::isfinite(rateHz) || rateHz <= 0.0)
    {
        return english ? QStringLiteral("0.0 Hz (no packets)")
                       : QStringLiteral("0.0 Hz（未收到）");
    }
    return QStringLiteral("%1 Hz").arg(rateHz, 0, 'f', 1);
}

} // namespace

EpsilonConfigPanel::EpsilonConfigPanel(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("epsilonSectionCard"));
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);

    auto *panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(12);

    const SectionCard summaryCard = createSectionCard(
        this, QStringLiteral("epsilonStatusCard"), QStringLiteral("satellite"));
    summary_title_label_ = summaryCard.title;
    auto *summaryFields = new QWidget(summaryCard.card);
    summaryFields->setObjectName(QStringLiteral("epsilonSummaryFields"));
    auto *summaryFieldsLayout = new QHBoxLayout(summaryFields);
    summaryFieldsLayout->setContentsMargins(0, 0, 0, 0);
    summaryFieldsLayout->setSpacing(24);
    const SummaryField availabilityField = createSummaryField(
        summaryFields, QStringLiteral("epsilonAvailabilitySummary"));
    availability_name_label_ = availabilityField.name;
    availability_value_label_ = availabilityField.value;
    availability_name_label_->setObjectName(QStringLiteral("epsilonAvailabilitySummaryName"));
    availability_value_label_->setObjectName(QStringLiteral("epsilonAvailabilitySummaryValue"));
    const SummaryField profileField = createSummaryField(
        summaryFields, QStringLiteral("epsilonProfileSummary"));
    profile_name_label_ = profileField.name;
    profile_value_label_ = profileField.value;
    profile_name_label_->setObjectName(QStringLiteral("epsilonProfileSummaryName"));
    profile_value_label_->setObjectName(QStringLiteral("epsilonProfileSummaryValue"));
    const SummaryField packetCountField = createSummaryField(
        summaryFields, QStringLiteral("epsilonPacketCountSummary"));
    packet_count_name_label_ = packetCountField.name;
    packet_count_value_label_ = packetCountField.value;
    packet_count_name_label_->setObjectName(QStringLiteral("epsilonPacketCountSummaryName"));
    packet_count_value_label_->setObjectName(QStringLiteral("epsilonPacketCountSummaryValue"));
    summaryFieldsLayout->addWidget(availabilityField.field, 1);
    summaryFieldsLayout->addWidget(profileField.field, 1);
    summaryFieldsLayout->addWidget(packetCountField.field, 1);
    summaryCard.body_layout->addWidget(summaryFields);

    panelLayout->addWidget(summaryCard.card);

    const SectionCard livePacketRateCard = createSectionCard(
        this, QStringLiteral("epsilonLivePacketRateCard"), QStringLiteral("activity"));
    live_packet_rate_title_label_ = livePacketRateCard.title;
    live_packet_rate_title_label_->setObjectName(QStringLiteral("epsilonLivePacketRateCardTitle"));
    auto *livePacketRateGridWidget = new QWidget(livePacketRateCard.card);
    livePacketRateGridWidget->setObjectName(QStringLiteral("epsilonLivePacketRateGrid"));
    livePacketRateGridWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    live_packet_rate_grid_ = new QGridLayout(livePacketRateGridWidget);
    live_packet_rate_grid_->setContentsMargins(0, 0, 0, 0);
    live_packet_rate_grid_->setHorizontalSpacing(24);
    live_packet_rate_grid_->setVerticalSpacing(6);
    live_packet_rate_grid_->setColumnStretch(1, 1);
    for (const auto &option : VaporView::Ground::DeviceRates::epsilonPacketConfigOptions())
    {
        const QString packetId = QStringLiteral("%1").arg(
            option.packet_id, 2, 16, QLatin1Char('0')).toUpper();
        auto *label = new QLabel(livePacketRateGridWidget);
        label->setObjectName(QStringLiteral("epsilonLivePacketRateLabel_%1").arg(packetId));
        label->setProperty("epsilonLivePacketRateLabel", true);
        label->setWordWrap(false);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        label->setFocusPolicy(Qt::NoFocus);
        auto *value = new QLabel(livePacketRateGridWidget);
        value->setObjectName(QStringLiteral("epsilonLivePacketRateValue_%1").arg(packetId));
        value->setProperty("epsilonLivePacketRateValue", true);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        value->setFocusPolicy(Qt::NoFocus);
        live_packet_rate_grid_->addWidget(label, live_packet_rate_labels_.size(), 0,
                                          Qt::AlignLeft | Qt::AlignVCenter);
        live_packet_rate_grid_->addWidget(value, live_packet_rate_values_.size(), 1,
                                          Qt::AlignRight | Qt::AlignVCenter);
        live_packet_rate_labels_.append(label);
        live_packet_rate_values_.append(value);
    }
    livePacketRateCard.body_layout->addWidget(livePacketRateGridWidget);
    panelLayout->addWidget(livePacketRateCard.card);

    const SectionCard outputCard = createSectionCard(
        this, QStringLiteral("epsilonOutputCard"), QStringLiteral("activity"));
    output_title_label_ = outputCard.title;
    auto *outputTitleActions = new QWidget(outputCard.title_bar);
    outputTitleActions->setObjectName(QStringLiteral("epsilonOutputTitleActions"));
    outputTitleActions->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *outputTitleActionsLayout = new QHBoxLayout(outputTitleActions);
    outputTitleActionsLayout->setContentsMargins(0, 0, 0, 0);
    outputTitleActionsLayout->setSpacing(12);
    hint_label_ = new QLabel(outputTitleActions);
    hint_label_->setObjectName(QStringLiteral("epsilonConfigHint"));
    hint_label_->setWordWrap(false);
    hint_label_->setProperty("epsilonSecondaryText", true);
    hint_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    hint_label_->setToolTip(QStringLiteral("包频率会用于后续连接和重配；已选择 EPSILON 串口时，保存后会立即应用。"));
    recommended_button_ = createActionButton(outputTitleActions);
    recommended_button_->setFixedHeight(28);
    recommended_button_->setObjectName(QStringLiteral("epsilonRecommendedConfigButton"));
    recommended_button_->setProperty("epsilonSecondaryAction", true);
    outputTitleActionsLayout->addWidget(hint_label_, 1, Qt::AlignVCenter | Qt::AlignRight);
    outputTitleActionsLayout->addWidget(recommended_button_, 0, Qt::AlignVCenter | Qt::AlignRight);
    outputTitleActions->setMinimumWidth(0);
    outputCard.title_layout->addWidget(outputTitleActions, 1, Qt::AlignVCenter | Qt::AlignRight);
    auto *packetGridWidget = new QWidget(outputCard.card);
    packetGridWidget->setObjectName(QStringLiteral("epsilonPacketGrid"));
    packetGridWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    packet_grid_ = new QGridLayout(packetGridWidget);
    packet_grid_->setContentsMargins(0, 0, 0, 0);
    packet_grid_->setHorizontalSpacing(12);
    packet_grid_->setVerticalSpacing(6);

    int comboWidth = 0;
    {
        QComboBox probe(this);
        const QFontMetrics metrics(probe.font());
        for (const auto &option : VaporView::Ground::DeviceRates::epsilonPacketConfigOptions())
        {
            for (int rateHz : option.supported_rates_hz)
            {
                comboWidth = std::max(
                    comboWidth,
                    metrics.horizontalAdvance(
                        VaporView::Ground::DeviceRates::epsilonPacketRateDisplayText(
                            rateHz, is_english_)));
            }
        }
    }
    comboWidth = std::clamp(comboWidth + 42, 116, 148);

    for (const auto &option : VaporView::Ground::DeviceRates::epsilonPacketConfigOptions())
    {
        const PacketRateGroup group = packetRateGroupForId(option.packet_id);
        const int groupIndex = packetRateGroupIndex(group);
        auto *field = new QWidget(packetGridWidget);
        field->setObjectName(QStringLiteral("epsilonPacketField_%1").arg(
            option.packet_id, 2, 16, QLatin1Char('0')));
        field->setProperty("epsilonPacketId", static_cast<uint>(option.packet_id));
        field->setProperty("epsilonPacketGroup", groupIndex);
        field->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        auto *fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(6);

        auto *label = new QLabel(field);
        label->setObjectName(QStringLiteral("epsilonPacketRateLabel_%1").arg(option.packet_id, 2, 16, QLatin1Char('0')));
        label->setProperty("epsilonPacketId", static_cast<uint>(option.packet_id));
        label->setProperty("epsilonPacketGroup", groupIndex);
        label->setWordWrap(false);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        label->setFocusPolicy(Qt::NoFocus);

        auto *combo = createPacketRateCombo(field, comboWidth);
        combo->setObjectName(QStringLiteral("epsilonPacketRateCombo_%1").arg(option.packet_id, 2, 16, QLatin1Char('0')));
        combo->setProperty("epsilonPacketId", static_cast<uint>(option.packet_id));
        combo->setProperty("epsilonPacketGroup", groupIndex);
        combo->setFocusPolicy(Qt::TabFocus);
        for (int rateHz : option.supported_rates_hz)
        {
            combo->addItem(
                VaporView::Ground::DeviceRates::epsilonPacketRateDisplayText(rateHz, is_english_),
                rateHz);
        }
        combo->setAccessibleName(QStringLiteral("EPSILON packet %1 rate").arg(option.packet_id, 2, 16, QLatin1Char('0')));
        fieldLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        fieldLayout->addWidget(combo, 0, Qt::AlignLeft | Qt::AlignVCenter);

        packet_rate_fields_.append(field);
        packet_rate_labels_.append(label);
        packet_rate_combos_.append(combo);
        packet_rate_group_ids_.append(groupIndex);
    }
    for (int groupIndex = 0; groupIndex < kPacketRateGroupCount; ++groupIndex)
    {
        const PacketRateGroup group = static_cast<PacketRateGroup>(groupIndex);
        auto *groupLabel = new QLabel(packetGridWidget);
        groupLabel->setObjectName(packetRateGroupObjectName(group));
        groupLabel->setProperty("epsilonPacketGroupHeader", true);
        groupLabel->setProperty("epsilonPacketGroup", groupIndex);
        groupLabel->setFocusPolicy(Qt::NoFocus);
        groupLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        packet_group_labels_.append(groupLabel);
    }
    arrangePacketFields(true);
    outputCard.body_layout->addWidget(packetGridWidget);

    panelLayout->addWidget(outputCard.card);

    const SectionCard deviceSettingsCard = createSectionCard(
        this, QStringLiteral("epsilonDeviceSettingsCard"), QStringLiteral("sliders-vertical"));
    device_settings_title_label_ = deviceSettingsCard.title;
    auto *deviceGrid = new QGridLayout();
    deviceGrid->setContentsMargins(0, 0, 0, 0);
    deviceGrid->setHorizontalSpacing(12);
    deviceGrid->setVerticalSpacing(10);
    deviceGrid->setColumnStretch(1, 1);
    deviceGrid->setColumnStretch(2, 0);
    deviceGrid->setColumnStretch(3, 0);
    auto addDeviceAction = [deviceGrid, card = deviceSettingsCard.card](
                               int row,
                               QLabel **nameOut,
                               QLabel **descriptionOut,
                               QPushButton **buttonOut,
                               const QString& nameObjectName,
                               const QString& descriptionObjectName,
                               const QString& buttonObjectName,
                               int descriptionColumnSpan) {
        *nameOut = new QLabel(card);
        (*nameOut)->setObjectName(nameObjectName);
        (*nameOut)->setProperty("epsilonSettingName", true);
        (*nameOut)->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        *descriptionOut = new QLabel(card);
        (*descriptionOut)->setObjectName(descriptionObjectName);
        (*descriptionOut)->setProperty("epsilonSecondaryText", true);
        (*descriptionOut)->setWordWrap(true);
        (*descriptionOut)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        *buttonOut = createActionButton(card);
        (*buttonOut)->setObjectName(buttonObjectName);
        (*buttonOut)->setProperty("epsilonSecondaryAction", true);
        deviceGrid->addWidget(*nameOut, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        deviceGrid->addWidget(*descriptionOut, row, 1, 1, descriptionColumnSpan);
        deviceGrid->addWidget(*buttonOut, row, 3, Qt::AlignRight | Qt::AlignVCenter);
    };
    addDeviceAction(0, &rtcm_name_label_, &rtcm_description_label_, &rtcm_port_button_,
                    QStringLiteral("epsilonRtcmSettingName"),
                    QStringLiteral("epsilonRtcmSettingDescription"),
                    QStringLiteral("epsilonRtcmPortButton"), 1);
    rtcm_device_port_combo_ = createRtcmDevicePortCombo(deviceSettingsCard.card);
    rtcm_device_port_combo_->setObjectName(QStringLiteral("epsilonRtcmDevicePortCombo"));
    rtcm_device_port_combo_->setProperty("epsilonRtcmDevicePortControl", true);
    deviceGrid->addWidget(rtcm_device_port_combo_, 0, 2, Qt::AlignLeft | Qt::AlignVCenter);
    addDeviceAction(1, &reconfigure_name_label_, &reconfigure_description_label_, &reconfigure_button_,
                    QStringLiteral("epsilonReconfigureSettingName"),
                    QStringLiteral("epsilonReconfigureSettingDescription"),
                    QStringLiteral("epsilonReconfigureButton"), 2);
    deviceSettingsCard.body_layout->addLayout(deviceGrid);
    panelLayout->addWidget(deviceSettingsCard.card);

    auto *actionsContainer = new QWidget(this);
    actionsContainer->setObjectName(QStringLiteral("epsilonActionsContainer"));
    actionsContainer->setAttribute(Qt::WA_StyledBackground, true);
    auto *actionsLayout = new QHBoxLayout(actionsContainer);
    actionsLayout->setContentsMargins(0, 0, 0, 2);
    actionsLayout->setSpacing(8);
    save_button_ = createActionButton(actionsContainer);
    save_button_->setObjectName(QStringLiteral("epsilonSaveButton"));
    actionsLayout->addStretch(1);
    actionsLayout->addWidget(save_button_, 0, Qt::AlignRight | Qt::AlignVCenter);
    panelLayout->addWidget(actionsContainer);

    connect(recommended_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::recommendedProfileRequested);
    connect(save_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::saveRequested);
    connect(rtcm_port_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::rtcmPortRequested);
    connect(reconfigure_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::reconfigureRequested);

    QWidget::setTabOrder(recommended_button_, packet_rate_combos_.value(0));
    for (int i = 0; i + 1 < packet_rate_combos_.size(); ++i)
    {
        QWidget::setTabOrder(packet_rate_combos_.at(i), packet_rate_combos_.at(i + 1));
    }
    QWidget::setTabOrder(packet_rate_combos_.last(), rtcm_device_port_combo_);
    QWidget::setTabOrder(rtcm_device_port_combo_, rtcm_port_button_);
    QWidget::setTabOrder(rtcm_port_button_, reconfigure_button_);
    QWidget::setTabOrder(reconfigure_button_, save_button_);

    updateTexts();
    applyAppearance();
}

void EpsilonConfigPanel::setEnglish(bool english)
{
    is_english_ = english;
    updateTexts();
}

void EpsilonConfigPanel::setAvailable(bool available)
{
    is_available_ = available;
    updateSummaryTexts();
    setEnabled(available);
}

void EpsilonConfigPanel::setPacketRates(const std::map<uint8_t, int>& packetRates)
{
    for (QComboBox *combo : packet_rate_combos_)
    {
        if (!combo)
        {
            continue;
        }
        const auto packetId = static_cast<uint8_t>(combo->property("epsilonPacketId").toUInt());
        const auto it = packetRates.find(packetId);
        if (it == packetRates.end())
        {
            continue;
        }
        const int index = combo->findData(it->second);
        if (index >= 0)
        {
            const QSignalBlocker blocker(combo);
            combo->setCurrentIndex(index);
        }
    }
}

void EpsilonConfigPanel::setLivePacketRates(const VaporView::EpsilonData& epsilonData)
{
    live_epsilon_data_ = epsilonData;
    updateLivePacketRateTexts();
}

std::map<uint8_t, int> EpsilonConfigPanel::packetRates() const
{
    std::map<uint8_t, int> packetRates;
    for (QComboBox *combo : packet_rate_combos_)
    {
        if (!combo || !combo->currentData().isValid())
        {
            continue;
        }
        const auto packetId = static_cast<uint8_t>(combo->property("epsilonPacketId").toUInt());
        packetRates[packetId] = combo->currentData().toInt();
    }
    return packetRates;
}

void EpsilonConfigPanel::setRtcmDevicePortIndex(int portIndex)
{
    if (!rtcm_device_port_combo_)
    {
        return;
    }
    if (portIndex < 2 || portIndex > 5)
    {
        portIndex = 2;
    }
    const int index = rtcm_device_port_combo_->findData(portIndex);
    if (index >= 0)
    {
        const QSignalBlocker blocker(rtcm_device_port_combo_);
        rtcm_device_port_combo_->setCurrentIndex(index);
    }
}

int EpsilonConfigPanel::rtcmDevicePortIndex() const
{
    if (!rtcm_device_port_combo_ ||
        !rtcm_device_port_combo_->currentData().isValid())
    {
        return 2;
    }
    const int portIndex = rtcm_device_port_combo_->currentData().toInt();
    return (portIndex >= 2 && portIndex <= 5) ? portIndex : 2;
}

void EpsilonConfigPanel::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event && (event->type() == QEvent::ApplicationPaletteChange ||
                  event->type() == QEvent::PaletteChange))
    {
        applyAppearance();
    }
}

void EpsilonConfigPanel::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    arrangePacketFields(event && event->size().width() >= kTwoColumnMinimumWidth);
}

void EpsilonConfigPanel::arrangePacketFields(bool twoColumns)
{
    if (!packet_grid_ ||
        (packet_layout_initialized_ && twoColumns == two_column_layout_))
    {
        return;
    }

    for (QLabel *groupLabel : packet_group_labels_)
    {
        packet_grid_->removeWidget(groupLabel);
    }
    for (QWidget *field : packet_rate_fields_)
    {
        packet_grid_->removeWidget(field);
    }
    for (int column = 0; column < kPacketWideGridColumnCount; ++column)
    {
        packet_grid_->setColumnMinimumWidth(column, 0);
        packet_grid_->setColumnStretch(column, 0);
    }

    two_column_layout_ = twoColumns;
    packet_layout_initialized_ = true;
    if (twoColumns)
    {
        packet_grid_->setColumnMinimumWidth(2, 24);
        packet_grid_->setColumnStretch(2, 1);
    }
    else
    {
        packet_grid_->setColumnStretch(0, 1);
    }

    int groupFieldCounts[kPacketRateGroupCount] = {};
    for (int groupId : packet_rate_group_ids_)
    {
        if (groupId >= 0 && groupId < kPacketRateGroupCount)
        {
            ++groupFieldCounts[groupId];
        }
    }

    auto addField = [this](int index,
                           int row,
                           int gridColumn,
                           int visualColumn,
                           int groupFieldRow,
                           int groupFieldColumn) {
        QWidget *field = packet_rate_fields_.at(index);
        field->setProperty("epsilonPacketGridColumn", visualColumn);
        field->setProperty("epsilonPacketGroupFieldRow", groupFieldRow);
        field->setProperty("epsilonPacketGroupFieldColumn", groupFieldColumn);
        packet_rate_labels_.at(index)->setProperty("epsilonPacketGridColumn", visualColumn);
        packet_rate_labels_.at(index)->setProperty("epsilonPacketGroupFieldRow", groupFieldRow);
        packet_rate_labels_.at(index)->setProperty("epsilonPacketGroupFieldColumn", groupFieldColumn);
        packet_rate_combos_.at(index)->setProperty("epsilonPacketGridColumn", visualColumn);
        packet_rate_combos_.at(index)->setProperty("epsilonPacketGroupFieldRow", groupFieldRow);
        packet_rate_combos_.at(index)->setProperty("epsilonPacketGroupFieldColumn", groupFieldColumn);
        packet_grid_->addWidget(field, row, gridColumn, Qt::AlignLeft | Qt::AlignVCenter);
    };

    if (twoColumns)
    {
        int gridRow = 0;
        const int groupColumns = kPacketOuterGroupColumns;
        for (int groupRow = 0;
             groupRow * groupColumns < packet_group_labels_.size();
             ++groupRow)
        {
            int rowSpan = 0;
            for (int groupColumn = 0; groupColumn < groupColumns; ++groupColumn)
            {
                const int groupIndex = groupRow * groupColumns + groupColumn;
                if (groupIndex >= packet_group_labels_.size() ||
                    groupIndex >= kPacketRateGroupCount ||
                    groupFieldCounts[groupIndex] <= 0)
                {
                    continue;
                }
                const int gridColumn = groupColumn == 0 ? 0 : 3;
                packet_grid_->addWidget(packet_group_labels_.at(groupIndex), gridRow, gridColumn, 1,
                                        kPacketInnerFieldColumns,
                                        Qt::AlignLeft | Qt::AlignVCenter);

                int localFieldIndex = 0;
                for (int index = 0; index < packet_rate_fields_.size(); ++index)
                {
                    if (packet_rate_group_ids_.at(index) != groupIndex)
                    {
                        continue;
                    }
                    const int fieldRowInGroup = localFieldIndex / kPacketInnerFieldColumns;
                    const int fieldColumnInGroup = localFieldIndex % kPacketInnerFieldColumns;
                    addField(index,
                             gridRow + 1 + fieldRowInGroup,
                             gridColumn + fieldColumnInGroup,
                             groupColumn * kPacketInnerFieldColumns + fieldColumnInGroup,
                             fieldRowInGroup,
                             fieldColumnInGroup);
                    ++localFieldIndex;
                }
                rowSpan = std::max(
                    rowSpan,
                    1 + (groupFieldCounts[groupIndex] + kPacketInnerFieldColumns - 1) /
                            kPacketInnerFieldColumns);
            }
            gridRow += rowSpan;
        }
    }
    else
    {
        int gridRow = 0;
        for (int groupIndex = 0; groupIndex < packet_group_labels_.size(); ++groupIndex)
        {
            if (groupIndex >= kPacketRateGroupCount || groupFieldCounts[groupIndex] <= 0)
            {
                continue;
            }
            packet_grid_->addWidget(packet_group_labels_.at(groupIndex), gridRow, 0,
                                    Qt::AlignLeft | Qt::AlignVCenter);
            ++gridRow;

            int localFieldIndex = 0;
            for (int index = 0; index < packet_rate_fields_.size(); ++index)
            {
                if (packet_rate_group_ids_.at(index) != groupIndex)
                {
                    continue;
                }
                addField(index, gridRow, 0, 0, localFieldIndex, 0);
                ++localFieldIndex;
                ++gridRow;
            }
        }
    }
    updatePacketLabelWidths();
    packet_grid_->invalidate();
    packet_grid_->activate();
    QTimer::singleShot(0, this, [this]() {
        for (QWidget *widget = this; widget; widget = widget->parentWidget())
        {
            if (widget->layout())
            {
                widget->layout()->invalidate();
                widget->layout()->activate();
            }
            widget->updateGeometry();
            if (widget->objectName() == QStringLiteral("epsilonConfigScrollArea"))
            {
                break;
            }
        }
    });
}

void EpsilonConfigPanel::updatePacketLabelWidths()
{
    QVector<int> columnLabelWidths(kPacketVisualColumnCount, 0);
    for (QLabel *label : packet_rate_labels_)
    {
        if (!label)
        {
            continue;
        }
        int visualColumn = label->property("epsilonPacketGridColumn").toInt();
        if (visualColumn < 0 || visualColumn >= kPacketVisualColumnCount)
        {
            visualColumn = 0;
        }
        columnLabelWidths[visualColumn] = std::max(
            columnLabelWidths.at(visualColumn),
            label->fontMetrics().horizontalAdvance(label->text()) + 4);
    }
    for (QLabel *label : packet_rate_labels_)
    {
        if (!label)
        {
            continue;
        }
        int visualColumn = label->property("epsilonPacketGridColumn").toInt();
        if (visualColumn < 0 || visualColumn >= kPacketVisualColumnCount)
        {
            visualColumn = 0;
        }
        const int fallbackWidth = label->fontMetrics().horizontalAdvance(label->text()) + 4;
        const int labelWidth = std::max(columnLabelWidths.at(visualColumn), fallbackWidth);
        label->setMinimumWidth(labelWidth);
        label->setMaximumWidth(labelWidth);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
}

void EpsilonConfigPanel::updateLivePacketRateTexts()
{
    const auto &options = VaporView::Ground::DeviceRates::epsilonPacketConfigOptions();
    for (int i = 0; i < live_packet_rate_labels_.size() &&
                    i < static_cast<int>(options.size()); ++i)
    {
        QLabel *label = live_packet_rate_labels_.at(i);
        QLabel *value = live_packet_rate_values_.at(i);
        if (!label || !value)
        {
            continue;
        }
        const auto &option = options.at(i);
        const QString labelText = VaporView::Ground::DeviceRates::epsilonPacketDialogRowLabel(
            option, is_english_);
        label->setText(labelText);
        label->setToolTip(labelText);
        label->setAccessibleName(labelText);
        const QString valueText = !live_epsilon_data_.valid
            ? QStringLiteral("--")
            : livePacketRateText(livePacketRateForId(live_epsilon_data_, option.packet_id), is_english_);
        value->setText(valueText);
        value->setToolTip(valueText);
        value->setAccessibleName(QStringLiteral("%1 %2").arg(labelText, valueText));
    }
}

void EpsilonConfigPanel::applyAppearance()
{
    const QString style = QStringLiteral(
        "QFrame#epsilonSectionCard { background-color: @vv-window; border: none; }"
        "QFrame[epsilonConfigCard=\"true\"] { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 12px; }"
        "QFrame[epsilonConfigCard=\"true\"] > QWidget#sectionTitleBar { background-color: @vv-surface-raised; border-top-left-radius: 11px; border-top-right-radius: 11px; }"
        "QWidget[epsilonConfigCardBody=\"true\"] { background-color: @vv-surface-raised; border-bottom-left-radius: 11px; border-bottom-right-radius: 11px; }"
        "QLabel[epsilonSummaryName=\"true\"] { color: @vv-text-secondary; font-weight: 400; }"
        "QLabel[epsilonSummaryValue=\"true\"], QLabel[epsilonSettingName=\"true\"] { color: @vv-text-strong; font-weight: 600; }"
        "QLabel[epsilonLivePacketRateLabel=\"true\"] { color: @vv-text-secondary; font-weight: 400; }"
        "QLabel[epsilonLivePacketRateValue=\"true\"] { color: @vv-text-strong; font-weight: 600; }"
        "QLabel[epsilonPacketGroupHeader=\"true\"] { color: @vv-text-strong; font-weight: 600; padding-top: 2px; }"
        "QLabel[epsilonSecondaryText=\"true\"] { color: @vv-text-secondary; font-weight: 400; }"
        "QPushButton[epsilonSecondaryAction=\"true\"] { background-color: @vv-surface-alt; border: 1px solid @vv-border; color: @vv-text; }"
        "QPushButton[epsilonSecondaryAction=\"true\"]:hover { background-color: @vv-primary-subtle; border-color: @vv-primary; color: @vv-primary; }"
        "QPushButton[epsilonSecondaryAction=\"true\"]:focus { border-color: @vv-focus; }"
        "QPushButton[epsilonSecondaryAction=\"true\"]:disabled { background-color: @vv-surface-alt; border-color: @vv-border; color: @vv-text-muted; }"
        "QPushButton#epsilonRecommendedConfigButton { min-height: 28px; max-height: 28px; padding-top: 0px; padding-bottom: 0px; }"
        "QWidget#epsilonActionsContainer { background-color: @vv-window; border: none; }"
        "QWidget#epsilonSummaryFields, QWidget#epsilonLivePacketRateGrid, QWidget#epsilonOutputTitleActions, QWidget#epsilonPacketGrid { background-color: transparent; border: none; }"
        "QComboBox[epsilonRtcmDevicePortControl=\"true\"] { background-color: @vv-surface; }");
    const QString resolvedStyle = VaporView::applyAppThemeTokens(
        style, VaporView::isDarkThemeEnabled());
    if (styleSheet() != resolvedStyle)
    {
        setStyleSheet(resolvedStyle);
    }
}

void EpsilonConfigPanel::updateSummaryTexts()
{
    availability_value_label_->setText(
        is_available_
            ? (is_english_ ? QStringLiteral("Available") : QStringLiteral("可用"))
            : (is_english_ ? QStringLiteral("Unavailable") : QStringLiteral("不可用")));
    profile_value_label_->setText(
        is_english_ ? QStringLiteral("Per-packet") : QStringLiteral("逐项设置"));
    packet_count_value_label_->setText(
        is_english_ ? QStringLiteral("11 packet outputs") : QStringLiteral("11 项报文"));
    availability_value_label_->setAccessibleName(
        QStringLiteral("%1 %2").arg(availability_name_label_->text(), availability_value_label_->text()));
    profile_value_label_->setAccessibleName(
        QStringLiteral("%1 %2").arg(profile_name_label_->text(), profile_value_label_->text()));
    packet_count_value_label_->setAccessibleName(
        QStringLiteral("%1 %2").arg(packet_count_name_label_->text(), packet_count_value_label_->text()));
}

void EpsilonConfigPanel::updateTexts()
{
    const auto &options = VaporView::Ground::DeviceRates::epsilonPacketConfigOptions();
    summary_title_label_->setText(is_english_ ? QStringLiteral("Configuration Summary") : QStringLiteral("配置摘要"));
    live_packet_rate_title_label_->setText(is_english_ ? QStringLiteral("Live Packet Rates") : QStringLiteral("实时数据包频率"));
    output_title_label_->setText(is_english_ ? QStringLiteral("Packet Communication Rates") : QStringLiteral("报文通信频率"));
    device_settings_title_label_->setText(is_english_ ? QStringLiteral("Device Settings") : QStringLiteral("设备设置"));
    for (QLabel *title : {summary_title_label_, live_packet_rate_title_label_, output_title_label_, device_settings_title_label_})
    {
        title->setAccessibleName(title->text());
    }
    setAccessibleName(is_english_ ? QStringLiteral("EPSILON Configuration") : QStringLiteral("EPSILON 配置"));
    availability_name_label_->setText(is_english_ ? QStringLiteral("Configuration") : QStringLiteral("配置操作"));
    profile_name_label_->setText(is_english_ ? QStringLiteral("Packet Rates") : QStringLiteral("频率配置"));
    packet_count_name_label_->setText(is_english_ ? QStringLiteral("Packet Items") : QStringLiteral("报文项数"));
    for (QLabel *label : {availability_name_label_, profile_name_label_, packet_count_name_label_})
    {
        label->setAccessibleName(label->text());
    }
    const QString hintText = is_english_
        ? QStringLiteral("Packet rates are saved for future connect/reconfigure operations. Save applies the profile immediately when an EPSILON port is selected.")
        : QStringLiteral("包频率会用于后续连接和重配；已选择 EPSILON 串口时，保存后会立即应用。");
    hint_label_->setText(hintText);
    hint_label_->setToolTip(hintText);
    hint_label_->setAccessibleName(is_english_ ? QStringLiteral("EPSILON configuration hint") : QStringLiteral("EPSILON 配置提示"));
    rtcm_name_label_->setText(is_english_ ? QStringLiteral("RTCM Input") : QStringLiteral("RTCM 输入"));
    rtcm_description_label_->setText(
        is_english_ ? QStringLiteral("Configure an EPSILON communication port as the RTCM input.")
                    : QStringLiteral("配置 EPSILON 通信串口为 RTCM 输入口。"));
    if (rtcm_device_port_combo_)
    {
        const QSignalBlocker blocker(rtcm_device_port_combo_);
        for (int i = 0; i < rtcm_device_port_combo_->count(); ++i)
        {
            const int portIndex = rtcm_device_port_combo_->itemData(i).toInt();
            rtcm_device_port_combo_->setItemText(i, rtcmDevicePortText(portIndex, is_english_));
        }
        rtcm_device_port_combo_->setAccessibleName(
            is_english_ ? QStringLiteral("EPSILON RTCM input port")
                        : QStringLiteral("EPSILON RTCM 输入口"));
        rtcm_device_port_combo_->setToolTip(
            is_english_
                ? QStringLiteral("Select the EPSILON communication port that receives RTCM corrections. COMM2 is the default.")
                : QStringLiteral("选择 EPSILON 设备端接收 RTCM 差分数据的通信串口，默认 COMM2。"));
    }
    reconfigure_name_label_->setText(is_english_ ? QStringLiteral("Apply Saved Configuration") : QStringLiteral("应用已保存配置"));
    reconfigure_description_label_->setText(
        is_english_
            ? QStringLiteral("Read the saved EPSILON packet-rate and output settings from this computer and send them to the device again. Unsaved page changes are not stored.")
            : QStringLiteral("从本机已保存的 EPSILON 包频率和输出配置读取，并重新下发到设备；不会保存当前页面未提交的修改。"));
    for (QLabel *label : {rtcm_name_label_, rtcm_description_label_,
                          reconfigure_name_label_, reconfigure_description_label_})
    {
        label->setAccessibleName(label->text());
    }

    for (int i = 0; i < packet_rate_labels_.size() && i < static_cast<int>(options.size()); ++i)
    {
        QLabel *label = packet_rate_labels_.at(i);
        if (!label)
        {
            continue;
        }
        label->setText(VaporView::Ground::DeviceRates::epsilonPacketDialogRowLabel(options.at(i), is_english_));
        label->setToolTip(label->text());
        label->setAccessibleName(label->text());
    }
    updatePacketLabelWidths();
    for (int groupIndex = 0; groupIndex < packet_group_labels_.size(); ++groupIndex)
    {
        QLabel *groupLabel = packet_group_labels_.at(groupIndex);
        if (!groupLabel)
        {
            continue;
        }
        const QString title = packetRateGroupTitle(static_cast<PacketRateGroup>(groupIndex), is_english_);
        groupLabel->setText(title);
        groupLabel->setToolTip(title);
        groupLabel->setAccessibleName(title);
    }
    for (QComboBox *combo : packet_rate_combos_)
    {
        if (!combo)
        {
            continue;
        }
        const QSignalBlocker blocker(combo);
        for (int i = 0; i < combo->count(); ++i)
        {
            combo->setItemText(
                i,
                VaporView::Ground::DeviceRates::epsilonPacketRateDisplayText(
                    combo->itemData(i).toInt(), is_english_));
        }
    }

    recommended_button_->setText(is_english_ ? QStringLiteral("Recommended") : QStringLiteral("恢复推荐"));
    recommended_button_->setToolTip(is_english_ ? QStringLiteral("Use the recommended default packet rates") : QStringLiteral("恢复推荐默认包频率"));
    save_button_->setText(is_english_ ? QStringLiteral("Save + Apply") : QStringLiteral("保存并应用"));
    save_button_->setToolTip(is_english_ ? QStringLiteral("Save the packet-rate profile and apply it now when possible") : QStringLiteral("保存包频率配置，并在可用时立即应用"));
    rtcm_port_button_->setText(is_english_ ? QStringLiteral("RTCM Port") : QStringLiteral("配置RTCM串口"));
    rtcm_port_button_->setToolTip(is_english_ ? QStringLiteral("Configure an EPSILON communication port as RTCM input") : QStringLiteral("配置 EPSILON 通信串口为 RTCM 输入口"));
    reconfigure_button_->setText(is_english_ ? QStringLiteral("Apply Saved Configuration") : QStringLiteral("应用已保存配置"));
    reconfigure_button_->setToolTip(
        is_english_
            ? QStringLiteral("Read the saved EPSILON output configuration from this computer and send it to the device again")
            : QStringLiteral("读取本机已保存的 EPSILON 输出配置并重新下发到设备"));
    VaporView::Ground::MainSupport::fitButtonMinimumWidth(recommended_button_, 100);
    VaporView::Ground::MainSupport::fitButtonMinimumWidth(save_button_, 118);
    VaporView::Ground::MainSupport::fitButtonMinimumWidth(rtcm_port_button_, 128);
    VaporView::Ground::MainSupport::fitButtonMinimumWidth(reconfigure_button_, 128);
    for (QPushButton *button : {recommended_button_, save_button_, rtcm_port_button_, reconfigure_button_})
    {
        if (button)
        {
            button->setAccessibleName(button->text());
        }
    }
    updateSummaryTexts();
    updateLivePacketRateTexts();
}

} // namespace VaporView::Ground::Navigation
