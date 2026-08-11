#include "ground/navigation/EpsilonConfigPanel.h"

#include "ground/devices/DeviceRatePolicy.h"
#include "ground/main/GroundMainWindowSupport.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpacerItem>
#include <QVBoxLayout>

#include <algorithm>

namespace VaporView::Ground::Navigation
{
namespace
{

constexpr int kPacketColumnCount = 2;
constexpr int kPacketGroupGapColumn = 2;
constexpr int kPacketRightLabelColumn = 3;
constexpr int kPacketTrailingColumn = 5;

QComboBox *createPacketRateCombo(QWidget *parent, int width)
{
    auto *combo = new QComboBox(parent);
    combo->setFixedHeight(VaporView::Ground::MainSupport::kMainPageInputHeight);
    combo->setFixedWidth(width);
    combo->setMaxVisibleItems(15);
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

} // namespace

EpsilonConfigPanel::EpsilonConfigPanel(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("epsilonSectionCard"));
    setMinimumWidth(520);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    configureTopLevelCard(this);

    auto *panelLayout = new QVBoxLayout(this);
    panelLayout->setContentsMargins(1, 0, 1, 1);
    panelLayout->setSpacing(0);

    auto *titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("sectionTitleBar"));
    titleBar->setFixedHeight(VaporView::Ground::MainSupport::kMainPageTitleBarHeight);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(8, 2, 8, 2);
    titleLayout->setSpacing(8);
    QWidget *titleCluster = nullptr;
    title_label_ = VaporView::Ground::MainSupport::createSectionTitleCluster(
        titleBar, QStringLiteral("sliders-vertical"),
        VaporView::Ground::MainSupport::kMainPageButtonHeight, &titleCluster);
    titleLayout->addWidget(titleCluster, 0, Qt::AlignVCenter | Qt::AlignLeft);
    titleLayout->addStretch(1);
    panelLayout->addWidget(titleBar);

    auto *body = new QWidget(this);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(8, 8, 8, 8);
    bodyLayout->setSpacing(7);

    hint_label_ = new QLabel(body);
    hint_label_->setObjectName(QStringLiteral("epsilonConfigHint"));
    hint_label_->setWordWrap(true);
    hint_label_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    bodyLayout->addWidget(hint_label_);

    custom_packet_check_ = VaporView::Ground::MainSupport::createTitleBarFeedbackCheckBox(body);
    custom_packet_check_->setObjectName(QStringLiteral("epsilonPacketCustomCheck"));
    custom_packet_check_->setFocusPolicy(Qt::TabFocus);
    custom_packet_check_->setAccessibleName(QStringLiteral("EPSILON custom packet-rate profile"));
    bodyLayout->addWidget(custom_packet_check_);

    auto *packetGridWidget = new QWidget(body);
    auto *packetGrid = new QGridLayout(packetGridWidget);
    packetGrid->setContentsMargins(0, 0, 0, 0);
    packetGrid->setHorizontalSpacing(8);
    packetGrid->setVerticalSpacing(4);

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
    comboWidth = std::clamp(comboWidth + 50, 126, 160);

    int packetIndex = 0;
    for (const auto &option : VaporView::Ground::DeviceRates::epsilonPacketConfigOptions())
    {
        const int row = packetIndex / kPacketColumnCount;
        const int side = packetIndex % kPacketColumnCount;
        const int labelColumn = side == 0 ? 0 : kPacketRightLabelColumn;
        const int comboColumn = labelColumn + 1;

        auto *label = new QLabel(packetGridWidget);
        label->setObjectName(QStringLiteral("epsilonPacketRateLabel_%1").arg(option.packet_id, 2, 16, QLatin1Char('0')));
        label->setProperty("epsilonPacketId", static_cast<uint>(option.packet_id));
        label->setProperty("epsilonPacketGridColumn", side);
        label->setWordWrap(false);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        label->setFocusPolicy(Qt::NoFocus);
        packetGrid->addWidget(label, row, labelColumn, Qt::AlignLeft | Qt::AlignVCenter);

        auto *combo = createPacketRateCombo(packetGridWidget, comboWidth);
        combo->setObjectName(QStringLiteral("epsilonPacketRateCombo_%1").arg(option.packet_id, 2, 16, QLatin1Char('0')));
        combo->setProperty("epsilonPacketId", static_cast<uint>(option.packet_id));
        combo->setProperty("epsilonPacketGridColumn", side);
        combo->setFocusPolicy(Qt::TabFocus);
        for (int rateHz : option.supported_rates_hz)
        {
            combo->addItem(
                VaporView::Ground::DeviceRates::epsilonPacketRateDisplayText(rateHz, is_english_),
                rateHz);
        }
        combo->setAccessibleName(QStringLiteral("EPSILON packet %1 rate").arg(option.packet_id, 2, 16, QLatin1Char('0')));
        packetGrid->addWidget(combo, row, comboColumn, Qt::AlignLeft | Qt::AlignVCenter);

        packet_rate_labels_.append(label);
        packet_rate_combos_.append(combo);
        ++packetIndex;
    }
    packetGrid->setColumnMinimumWidth(kPacketGroupGapColumn, 20);
    packetGrid->addItem(new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum),
                        0, kPacketTrailingColumn);
    packetGrid->setColumnStretch(kPacketTrailingColumn, 1);

    auto *buttonPanel = new QWidget(packetGridWidget);
    buttonPanel->setObjectName(QStringLiteral("epsilonPacketActionPanel"));
    buttonPanel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Minimum);
    auto *buttonLayout = new QGridLayout(buttonPanel);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setHorizontalSpacing(4);
    buttonLayout->setVerticalSpacing(4);

    recommended_button_ = createActionButton(buttonPanel);
    recommended_button_->setObjectName(QStringLiteral("epsilonRecommendedConfigButton"));
    grouped_button_ = createActionButton(buttonPanel);
    grouped_button_->setObjectName(QStringLiteral("epsilonGroupedConfigButton"));
    save_button_ = createActionButton(buttonPanel);
    save_button_->setObjectName(QStringLiteral("epsilonSaveButton"));
    rtcm_port_button_ = createActionButton(buttonPanel);
    rtcm_port_button_->setObjectName(QStringLiteral("epsilonRtcmPortButton"));
    reconfigure_button_ = createActionButton(buttonPanel);
    reconfigure_button_->setObjectName(QStringLiteral("epsilonReconfigureButton"));
    rtk_config_button_ = createActionButton(buttonPanel);
    rtk_config_button_->setObjectName(QStringLiteral("epsilonRtkConfigButton"));

    buttonLayout->addWidget(recommended_button_, 0, 0);
    buttonLayout->addWidget(grouped_button_, 0, 1);
    buttonLayout->addWidget(save_button_, 1, 0);
    buttonLayout->addWidget(rtcm_port_button_, 1, 1);
    buttonLayout->addWidget(reconfigure_button_, 2, 0);
    buttonLayout->addWidget(rtk_config_button_, 2, 1);
    packetGrid->addWidget(buttonPanel, 0, kPacketTrailingColumn, 4, 1,
                          Qt::AlignLeft | Qt::AlignTop);
    bodyLayout->addWidget(packetGridWidget);
    panelLayout->addWidget(body);

    connect(recommended_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::recommendedProfileRequested);
    connect(grouped_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::groupedProfileRequested);
    connect(save_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::saveRequested);
    connect(rtcm_port_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::rtcmPortRequested);
    connect(reconfigure_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::reconfigureRequested);
    connect(rtk_config_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::rtkConfigRequested);

    QWidget::setTabOrder(custom_packet_check_, packet_rate_combos_.value(0));
    for (int i = 0; i + 1 < packet_rate_combos_.size(); ++i)
    {
        QWidget::setTabOrder(packet_rate_combos_.at(i), packet_rate_combos_.at(i + 1));
    }
    QWidget::setTabOrder(packet_rate_combos_.last(), recommended_button_);
    QWidget::setTabOrder(recommended_button_, grouped_button_);
    QWidget::setTabOrder(grouped_button_, save_button_);
    QWidget::setTabOrder(save_button_, rtcm_port_button_);
    QWidget::setTabOrder(rtcm_port_button_, reconfigure_button_);
    QWidget::setTabOrder(reconfigure_button_, rtk_config_button_);

    updateTexts();
}

void EpsilonConfigPanel::setEnglish(bool english)
{
    is_english_ = english;
    updateTexts();
}

void EpsilonConfigPanel::setAvailable(bool available)
{
    setEnabled(available);
}

void EpsilonConfigPanel::setCustomPacketProfileEnabled(bool enabled)
{
    const QSignalBlocker blocker(custom_packet_check_);
    custom_packet_check_->setChecked(enabled);
}

bool EpsilonConfigPanel::customPacketProfileEnabled() const
{
    return custom_packet_check_->isChecked();
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

void EpsilonConfigPanel::updateTexts()
{
    const auto &options = VaporView::Ground::DeviceRates::epsilonPacketConfigOptions();
    title_label_->setText(is_english_ ? QStringLiteral("EPSILON Configuration") : QStringLiteral("EPSILON 配置"));
    title_label_->setAccessibleName(title_label_->text());
    setAccessibleName(title_label_->text());
    hint_label_->setText(
        is_english_
            ? QStringLiteral("Packet rates are saved for future connect/reconfigure operations. Save applies the profile immediately when an EPSILON port is selected.")
            : QStringLiteral("包频率会用于后续连接和重配；已选择 EPSILON 串口时，保存后会立即应用。"));
    hint_label_->setAccessibleName(is_english_ ? QStringLiteral("EPSILON configuration hint") : QStringLiteral("EPSILON 配置提示"));
    custom_packet_check_->setText(
        is_english_ ? QStringLiteral("Use this custom EPSILON packet-rate profile")
                    : QStringLiteral("使用这组自定义 EPSILON 包频率"));
    custom_packet_check_->setAccessibleName(custom_packet_check_->text());

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
    grouped_button_->setText(is_english_ ? QStringLiteral("Grouped") : QStringLiteral("分组模式"));
    grouped_button_->setToolTip(is_english_ ? QStringLiteral("Use the grouped output-rate profile") : QStringLiteral("切换到分组输出频率模式"));
    save_button_->setText(is_english_ ? QStringLiteral("Save + Apply") : QStringLiteral("保存并应用"));
    save_button_->setToolTip(is_english_ ? QStringLiteral("Save the packet-rate profile and apply it now when possible") : QStringLiteral("保存包频率配置，并在可用时立即应用"));
    rtcm_port_button_->setText(is_english_ ? QStringLiteral("RTCM Port") : QStringLiteral("配置RTCM串口"));
    rtcm_port_button_->setToolTip(is_english_ ? QStringLiteral("Configure EPSILON communication port 2 as RTCM input") : QStringLiteral("配置 EPSILON 第二通信串口为 RTCM 输入口"));
    reconfigure_button_->setText(is_english_ ? QStringLiteral("Reconfigure Output") : QStringLiteral("重新配置输出"));
    reconfigure_button_->setToolTip(is_english_ ? QStringLiteral("Apply the current EPSILON output profile") : QStringLiteral("应用当前 EPSILON 输出配置"));
    rtk_config_button_->setText(is_english_ ? QStringLiteral("RTK Config") : QStringLiteral("RTK配置"));
    rtk_config_button_->setToolTip(is_english_ ? QStringLiteral("Open RTK config") : QStringLiteral("打开 RTK 配置"));
    VaporView::Ground::MainSupport::fitButtonMinimumWidth(recommended_button_, 100);
    VaporView::Ground::MainSupport::fitButtonMinimumWidth(grouped_button_, 100);
    VaporView::Ground::MainSupport::fitButtonMinimumWidth(save_button_, 118);
    VaporView::Ground::MainSupport::fitButtonMinimumWidth(rtcm_port_button_, 128);
    VaporView::Ground::MainSupport::fitButtonMinimumWidth(reconfigure_button_, 128);
    VaporView::Ground::MainSupport::fitButtonMinimumWidth(rtk_config_button_, 100);
    for (QPushButton *button : {recommended_button_, grouped_button_, save_button_, rtcm_port_button_, reconfigure_button_, rtk_config_button_})
    {
        if (button)
        {
            button->setAccessibleName(button->text());
        }
    }
}

} // namespace VaporView::Ground::Navigation
