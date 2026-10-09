#include "ground/navigation/EpsilonConfigPanel.h"
#include "ground/navigation/CombinationNavigationPage.h"
#include "EpsilonRawSatellite.h"

#include "ground/devices/DeviceRatePolicy.h"
#include "ground/main/GroundMainWindowSupport.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileDialog>
#include <QFile>
#include <QSaveFile>
#include <QMessageBox>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QLayout>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <set>

namespace VaporView::Ground::Navigation
{
namespace
{

constexpr int kTwoColumnMinimumWidth = 980;
constexpr int kPacketWideGridColumnCount = 5;
constexpr int kPacketOuterGroupColumns = 2;
constexpr int kPacketInnerFieldColumns = 2;
constexpr int kPacketVisualColumnCount = kPacketOuterGroupColumns * kPacketInnerFieldColumns;
constexpr int kLivePacketVisualColumnCount = 4;

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
    case 0x77:
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

// A hidden parameter page must not impose its long form height on communication.
class EpsilonPages final : public QStackedWidget
{
public:
    using QStackedWidget::QStackedWidget;
    QSize sizeHint() const override
    {
        return currentWidget() ? currentWidget()->sizeHint() : QSize();
    }
    QSize minimumSizeHint() const override
    {
        return currentWidget() ? currentWidget()->minimumSizeHint() : QSize();
    }
};

class EpsilonSettingsTrack final : public QFrame
{
public:
    using QFrame::QFrame;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QFrame::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(VaporView::appThemeColor(VaporView::AppThemeColor::Surface,
                                                 VaporView::isDarkThemeEnabled()));
        for (auto *button : findChildren<QPushButton *>(QString(), Qt::FindDirectChildrenOnly))
        {
            if (!button->isChecked()) continue;
            const QRectF bounds = QRectF(button->geometry()).adjusted(0.5, 0.5, -0.5, -0.5);
            painter.drawRoundedRect(bounds, bounds.height() / 2.0, bounds.height() / 2.0);
        }
    }
};

class EpsilonParameterCheckBox final : public QCheckBox
{
public:
    explicit EpsilonParameterCheckBox(QWidget *parent) : QCheckBox(parent)
    {
        setProperty("epsilonThemedIndicator", true);
        setStyleSheet(QStringLiteral("QCheckBox::indicator { width: 18px; height: 18px; }"
            "QCheckBox::indicator, QCheckBox::indicator:checked, QCheckBox::indicator:indeterminate, "
            "QCheckBox::indicator:hover { background: transparent; border: none; image: none; }"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QCheckBox::paintEvent(event);
        QStyleOptionButton option;
        initStyleOption(&option);
        const QRect rect = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this);
        const bool dark = VaporView::isDarkThemeEnabled();
        QColor color = VaporView::appThemeColor(isChecked() ? VaporView::AppThemeColor::Primary
                                                          : VaporView::AppThemeColor::TextSecondary, dark);
        if (!isEnabled()) color.setAlphaF(0.6f);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(rect.topLeft());
        painter.scale(rect.width() / 20.0, rect.height() / 20.0);
        painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(VaporView::appThemeColor(isChecked() ? VaporView::AppThemeColor::PrimarySubtle
                                                           : VaporView::AppThemeColor::Surface, dark));
        painter.drawRoundedRect(QRectF(1, 1, 18, 18), 3, 3);
        if (checkState() == Qt::PartiallyChecked)
            painter.drawLine(QPointF(5, 10), QPointF(15, 10));
        else if (isChecked())
        {
            QPainterPath check;
            check.moveTo(5, 10);
            check.lineTo(8, 13);
            check.lineTo(15, 6);
            painter.drawPath(check);
        }
    }
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
    case Ppk::kMsgRawSatellite:
        return data.raw_satellite_epoch_rate_hz;
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

    auto *tabs = new QFrame(this);
    settings_navigation_tabs_ = tabs;
    tabs->setObjectName(QStringLiteral("epsilonSettingsTabs"));
    tabs->setAttribute(Qt::WA_StyledBackground, true);
    tabs->setAutoFillBackground(true);
    tabs->setFixedHeight(36);
    tabs->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *tabsLayout = new QHBoxLayout(tabs);
    tabsLayout->setContentsMargins(2, 2, 2, 2);
    tabsLayout->setSpacing(0);
    auto *track = new EpsilonSettingsTrack(tabs);
    settings_navigation_track_ = track;
    track->setObjectName(QStringLiteral("epsilonSettingsTabTrack"));
    track->setAttribute(Qt::WA_StyledBackground, true);
    track->setAutoFillBackground(true);
    tabsLayout->addWidget(track);
    auto *trackLayout = new QHBoxLayout(track);
    trackLayout->setContentsMargins(2, 2, 2, 2);
    trackLayout->setSpacing(0);
    auto *tabGroup = new QButtonGroup(this);
    for (int i = 0; i < 5; ++i)
    {
        auto *button = createNavigationSectionButton(track);
        button->setObjectName(QStringLiteral("epsilonSettingsTab_%1").arg(i));
        connect(button, &QPushButton::toggled, track, [track]() { track->update(); });
        tabGroup->addButton(button, i);
        trackLayout->addWidget(button, 1);
        page_buttons_.append(button);
    }
    page_buttons_.first()->setChecked(true);
    advanced_features_button_ = createNavigationSectionButton(track);
    advanced_features_button_->setObjectName(QStringLiteral("epsilonAdvancedFeaturesButton"));
    advanced_features_button_->setCheckable(true);
    advanced_features_button_->setAccessibleName(QStringLiteral("高级功能"));
    connect(advanced_features_button_, &QPushButton::toggled, track, [track]() { track->update(); });
    connect(advanced_features_button_, &QPushButton::toggled, this, &EpsilonConfigPanel::setAdvancedFeaturesExpanded);
    trackLayout->addWidget(advanced_features_button_, 1);
    tabs->setMaximumWidth(720);
    panelLayout->addWidget(tabs, 0, Qt::AlignHCenter);
    pages_ = new EpsilonPages(this);
    pages_->setObjectName(QStringLiteral("epsilonSettingsPages"));
    pages_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    communication_page_ = new QWidget(pages_);
    communication_page_->setObjectName(QStringLiteral("epsilonCommunicationPage"));
    auto *communicationLayout = new QVBoxLayout(communication_page_);
    communicationLayout->setContentsMargins(0, 0, 0, 0);
    communicationLayout->setSpacing(12);
    pages_->addWidget(communication_page_);
    panelLayout->addWidget(pages_);

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

    communicationLayout->addWidget(summaryCard.card);

    const SectionCard livePacketRateCard = createSectionCard(
        this, QStringLiteral("epsilonLivePacketRateCard"), QStringLiteral("activity"));
    live_packet_rate_title_label_ = livePacketRateCard.title;
    live_packet_rate_title_label_->setObjectName(QStringLiteral("epsilonLivePacketRateCardTitle"));
    auto *livePacketRateGridWidget = new QWidget(livePacketRateCard.card);
    livePacketRateGridWidget->setObjectName(QStringLiteral("epsilonLivePacketRateGrid"));
    livePacketRateGridWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    live_packet_rate_grid_ = new QGridLayout(livePacketRateGridWidget);
    live_packet_rate_grid_->setContentsMargins(0, 0, 0, 0);
    live_packet_rate_grid_->setHorizontalSpacing(20);
    live_packet_rate_grid_->setVerticalSpacing(8);
    for (int column = 0; column < kLivePacketVisualColumnCount; ++column)
    {
        live_packet_rate_grid_->setColumnStretch(column, 1);
    }
    for (const auto &option : VaporView::Ground::DeviceRates::epsilonPacketConfigOptions())
    {
        const QString packetId = QStringLiteral("%1").arg(
            option.packet_id, 2, 16, QLatin1Char('0')).toUpper();
        const int itemIndex = live_packet_rate_fields_.size();
        const int row = itemIndex / kLivePacketVisualColumnCount;
        const int visualColumn = itemIndex % kLivePacketVisualColumnCount;
        auto *field = new QWidget(livePacketRateGridWidget);
        field->setObjectName(QStringLiteral("epsilonLivePacketRateField_%1").arg(packetId));
        field->setProperty("epsilonLivePacketRateField", true);
        field->setProperty("epsilonPacketId", static_cast<uint>(option.packet_id));
        field->setProperty("epsilonLivePacketGridRow", row);
        field->setProperty("epsilonLivePacketGridColumn", visualColumn);
        field->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *fieldLayout = new QHBoxLayout(field);
        fieldLayout->setContentsMargins(0, 0, 0, 0);
        fieldLayout->setSpacing(6);
        auto *label = new QLabel(field);
        label->setObjectName(QStringLiteral("epsilonLivePacketRateLabel_%1").arg(packetId));
        label->setProperty("epsilonLivePacketRateLabel", true);
        label->setProperty("epsilonPacketId", static_cast<uint>(option.packet_id));
        label->setProperty("epsilonLivePacketGridRow", row);
        label->setProperty("epsilonLivePacketGridColumn", visualColumn);
        label->setWordWrap(false);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        label->setFocusPolicy(Qt::NoFocus);
        auto *value = new QLabel(field);
        value->setObjectName(QStringLiteral("epsilonLivePacketRateValue_%1").arg(packetId));
        value->setProperty("epsilonLivePacketRateValue", true);
        value->setProperty("epsilonPacketId", static_cast<uint>(option.packet_id));
        value->setProperty("epsilonLivePacketGridRow", row);
        value->setProperty("epsilonLivePacketGridColumn", visualColumn);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        value->setFocusPolicy(Qt::NoFocus);
        fieldLayout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignVCenter);
        fieldLayout->addStretch(1);
        fieldLayout->addWidget(value, 0, Qt::AlignRight | Qt::AlignVCenter);
        live_packet_rate_grid_->addWidget(field, row, visualColumn);
        live_packet_rate_fields_.append(field);
        live_packet_rate_labels_.append(label);
        live_packet_rate_values_.append(value);
    }
    livePacketRateCard.body_layout->addWidget(livePacketRateGridWidget);
    communicationLayout->addWidget(livePacketRateCard.card);

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
    // The wide grid must not prevent resizing past the single-column breakpoint.
    packetGridWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
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

    communicationLayout->addWidget(outputCard.card);

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
    communicationLayout->addWidget(deviceSettingsCard.card);

    const SectionCard settingsActions = createSectionCard(this, QStringLiteral("epsilonParameterActionsCard"), QStringLiteral("sliders-vertical"));
    settings_actions_card_ = settingsActions.card;
    settings_actions_card_->setProperty("epsilonParameterCard", true);
    settings_actions_title_ = settingsActions.title;
    settings_actions_host_ = settingsActions.title_bar;
    settings_actions_body_ = settingsActions.card->findChild<QWidget *>(QStringLiteral("epsilonParameterActionsCardBody"));
    device_info_label_ = new QLabel(settingsActions.card);
    device_info_label_->setObjectName(QStringLiteral("epsilonDeviceInfo"));
    device_info_label_->setWordWrap(true);
    device_info_label_->setProperty("epsilonSecondaryText", true);
    device_info_label_->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    settingsActions.body_layout->addWidget(device_info_label_);
    createSettingsPages();
    createAdvancedSettingsPage();
    auto *maintenancePage = new QWidget(pages_);
    maintenancePage->setObjectName(QStringLiteral("epsilonMaintenancePage"));
    auto *maintenanceLayout = new QVBoxLayout(maintenancePage);
    maintenanceLayout->setContentsMargins(0, 10, 0, 12);
    auto maintenanceCard = createSectionCard(maintenancePage, QStringLiteral("epsilonMaintenanceCard"), QStringLiteral("sliders-vertical"));
    maintenanceCard.card->setProperty("epsilonParameterCard", true);
    maintenance_title_ = maintenanceCard.title;
    const QStringList maintenanceZh = {QStringLiteral("调平"), QStringLiteral("加表静态零偏"), QStringLiteral("陀螺静态零偏"), QStringLiteral("磁力计 2D 校准"), QStringLiteral("磁力计 3D 校准")};
    for (int i = 0; i < maintenanceZh.size(); ++i)
    {
        auto *button = createActionButton(maintenanceCard.card);
        button->setObjectName(QStringLiteral("epsilonMaintenanceButton_%1").arg(i));
        button->setProperty("epsilonMaintenanceAction", i);
        button->setText(maintenanceZh[i]);
        button->setToolTip(QStringLiteral("请确认设备静止、安装条件满足，并等待校准完成"));
        maintenanceCard.body_layout->addWidget(button, 0, Qt::AlignLeft);
        maintenance_buttons_.append(button);
        connect(button, &QPushButton::clicked, this, [this, i]() {
            const QString condition = i >= 3
                ? (i == 3 ? (is_english_ ? QStringLiteral("Avoid magnetic interference. Rotate slowly through one full turn in a level plane.") : QStringLiteral("避开磁干扰，在水平面缓慢旋转一周。"))
                          : (is_english_ ? QStringLiteral("Avoid magnetic interference. Move through spatial figure-eight orientations.") : QStringLiteral("避开磁干扰，以空间 ∞ 字改变设备姿态。")))
                : i == 2
                ? (is_english_ ? QStringLiteral("The device must be stationary.") : QStringLiteral("设备必须保持静止。"))
                : (is_english_ ? QStringLiteral("The device must be level and stationary.") : QStringLiteral("设备必须保持水平并静止。"));
            const QString explanation = i >= 3
                ? (is_english_ ? QStringLiteral("ACK only starts calibration. Wait for device progress or fit results and navigation recovery. Save and restart may be required; verify the physical result. Continue?")
                               : QStringLiteral("ACK 仅表示开始校准。请等待设备进度或拟合结果及导航恢复；完成后可能需要保存和重启，并实机核对效果。是否继续？"))
                : is_english_
                ? QStringLiteral("The command will be sent and saved, then the device must be restarted. ACK does not prove calibration completion or its physical effect. Verify the result on the device before repeating. Continue?")
                : QStringLiteral("将发送并保存该操作，之后需要重启设备。ACK 不能证明校准已完成或实际效果，请实机核对后再重复操作。是否继续？");
            if (QMessageBox::question(this, is_english_ ? QStringLiteral("Confirm maintenance conditions") : QStringLiteral("确认维护前置条件"),
                condition + QStringLiteral("\n") + explanation, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
                emit maintenanceRequested(static_cast<VaporView::EpsilonMaintenanceAction>(i));
        });
    }
    maintenance_progress_ = new QProgressBar(maintenanceCard.card);
    maintenance_progress_->setObjectName(QStringLiteral("epsilonMaintenanceProgress"));
    maintenance_progress_->hide();
    maintenanceCard.body_layout->addWidget(maintenance_progress_);
    maintenance_cancel_button_ = createActionButton(maintenanceCard.card);
    maintenance_cancel_button_->setObjectName(QStringLiteral("epsilonMaintenanceCancelButton"));
    maintenance_cancel_button_->hide();
    maintenanceCard.body_layout->addWidget(maintenance_cancel_button_, 0, Qt::AlignLeft);
    connect(maintenance_cancel_button_, &QPushButton::clicked, this, [this]() {
        maintenance_cancel_button_->setEnabled(false);
        maintenance_cancel_requested_ = true;
        setSettingsStatus(is_english_ ? QStringLiteral("Exit requested; device stop and navigation recovery are not yet confirmed.") : QStringLiteral("已请求退出；设备停止与导航恢复尚未确认。"));
        emit maintenanceCancelRequested();
    });
    maintenanceLayout->addWidget(maintenanceCard.card);
    auto *maintenanceHint = new QLabel(maintenancePage);
    maintenanceHint->setWordWrap(true);
    maintenanceHint->setProperty("epsilonSecondaryText", true);
    maintenanceHint->setObjectName(QStringLiteral("epsilonMaintenanceHint"));
    maintenanceLayout->addWidget(maintenanceHint);
    maintenanceLayout->addStretch(1);
    pages_->addWidget(maintenancePage);
    settings_status_label_ = new QLabel(this);
    settings_status_label_->setObjectName(QStringLiteral("epsilonSettingsStatus"));
    settings_status_label_->setWordWrap(true);
    settings_status_label_->setProperty("epsilonSecondaryText", true);
    settingsActions.body_layout->addWidget(settings_status_label_);
    panelLayout->insertWidget(1, settings_actions_card_);
    settings_actions_card_->hide();

    auto *actionsContainer = new QWidget(this);
    actions_container_ = actionsContainer;
    actionsContainer->setObjectName(QStringLiteral("epsilonActionsContainer"));
    actionsContainer->setAttribute(Qt::WA_StyledBackground, true);
    actionsContainer->setFixedHeight(VaporView::Ground::MainSupport::kMainPageButtonHeight);
    auto *actionsLayout = new QHBoxLayout(actionsContainer);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);
    save_button_ = createActionButton(actionsContainer);
    save_button_->setObjectName(QStringLiteral("epsilonSaveButton"));
    settings_read_button_ = createActionButton(actionsContainer);
    settings_read_button_->setObjectName(QStringLiteral("epsilonSettingsReadButton"));
    device_restart_button_ = createActionButton(actionsContainer);
    device_restart_button_->setObjectName(QStringLiteral("epsilonDeviceRestartButton"));
    device_restart_button_->setProperty("epsilonSecondaryAction", true);
    actionsLayout->addWidget(settings_read_button_);
    actionsLayout->addWidget(device_restart_button_);
    actionsLayout->addStretch(1);
    actionsLayout->addWidget(save_button_, 0, Qt::AlignRight | Qt::AlignVCenter);
    auto *snapshotTools = new QWidget(settings_actions_body_);
    auto *snapshotLayout = new QHBoxLayout(snapshotTools);
    snapshotLayout->setContentsMargins(0, 0, 0, 0);
    settings_export_button_ = createActionButton(snapshotTools);
    settings_import_button_ = createActionButton(snapshotTools);
    settings_export_button_->setObjectName(QStringLiteral("epsilonSettingsExportButton"));
    settings_import_button_->setObjectName(QStringLiteral("epsilonSettingsImportButton"));
    snapshotLayout->addWidget(settings_export_button_);
    snapshotLayout->addWidget(settings_import_button_);
    snapshotLayout->addStretch(1);
    qobject_cast<QVBoxLayout *>(settings_actions_body_->layout())->addWidget(snapshotTools);
    connect(settings_export_button_, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(this, is_english_ ? QStringLiteral("Export device snapshot") : QStringLiteral("导出设备快照"), QString(), QStringLiteral("JSON (*.json)"));
        if (path.isEmpty()) return;
        QSaveFile file(path);
        const QByteArray snapshot = exportSettingsJson();
        if (snapshot.isEmpty() || !file.open(QIODevice::WriteOnly) ||
            file.write(snapshot) != snapshot.size() || !file.commit())
            setSettingsError(is_english_ ? QStringLiteral("Snapshot export failed") : QStringLiteral("快照导出失败"));
    });
    connect(settings_import_button_, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, is_english_ ? QStringLiteral("Import snapshot differences") : QStringLiteral("导入快照差异"), QString(), QStringLiteral("JSON (*.json)"));
        if (path.isEmpty()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
        {
            setSettingsError(is_english_ ? QStringLiteral("Snapshot could not be opened") : QStringLiteral("无法打开快照文件"));
            return;
        }
        VaporView::EpsilonSettingsOperation changes;
        QString error;
        if (!previewSettingsImport(file.read(1024 * 1024 + 1), changes, error)) { setSettingsError(error); return; }
        QStringList lines;
        for (const auto& change : changes.values)
        {
            const auto *descriptor = VaporView::epsilonParameterDescriptor(change.first);
            const auto field = std::find_if(settings_fields_.begin(), settings_fields_.end(), [&](const auto& entry) { return entry.name == change.first; });
            lines.append(QStringLiteral("%1: %2 → %3 %4")
                .arg(QString::fromStdString(is_english_ ? descriptor->label_en : descriptor->label_zh))
                .arg(field->original, 0, 'g', 12).arg(change.second, 0, 'g', 12).arg(QString::fromStdString(descriptor->unit)));
        }
        if (QMessageBox::question(this, is_english_ ? QStringLiteral("Review differences") : QStringLiteral("确认差异"),
            lines.join(QLatin1Char('\n')) + (is_english_ ? QStringLiteral("\nFill editors only; Save + Apply sends changes to the device.") : QStringLiteral("\n仅填入编辑器；保存并应用后才下发设备。"))) == QMessageBox::Yes)
            applyImportedSettings(changes);
    });
    panelLayout->addWidget(actionsContainer);

    connect(recommended_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::recommendedProfileRequested);
    connect(save_button_, &QPushButton::clicked, this, [this]() {
        if (pages_->currentIndex() == 0)
            emit saveRequested();
        else if (isDgnssPage())
        {
            const auto operation = editedDgnss();
            std::string error;
            if (!VaporView::validateEpsilonDgnss(operation, error)) setSettingsError(QString::fromStdString(error));
            else if (!operation.values.empty() && !settings_pending_) emit dgnssApplyRequested(operation);
        }
        else
        {
            const auto operation = editedSettings();
            if (!operation.values.empty() && !settings_pending_)
                emit settingsApplyRequested(operation);
        }
    });
    connect(settings_read_button_, &QPushButton::clicked, this, [this]() {
        if (isDgnssPage()) emit dgnssReadRequested();
        else emit settingsReadRequested(currentSettingsGroup());
    });
    connect(device_restart_button_, &QPushButton::clicked, this, &EpsilonConfigPanel::deviceRestartRequested);
    connect(tabGroup, &QButtonGroup::idClicked, this, [this](int index) {
        pages_->setCurrentIndex(index);
        auto *rootLayout = qobject_cast<QVBoxLayout *>(layout());
        rootLayout->removeWidget(actions_container_);
        auto *headerLayout = qobject_cast<QHBoxLayout *>(settings_actions_host_->layout());
        headerLayout->removeWidget(actions_container_);
        settings_actions_body_->layout()->removeWidget(actions_container_);
        if (index == 0)
            rootLayout->addWidget(actions_container_);
        else
            headerLayout->addWidget(actions_container_, 0, Qt::AlignRight | Qt::AlignVCenter);
        settings_actions_card_->setVisible(index != 0);
        arrangeSettingsFields(width() >= 760);
        pages_->updateGeometry();
        updateSettingsTexts();
        updateSettingsControls();
    });
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
    invalidateSettings();
    setAdvancedFeaturesExpanded(false);
}

void EpsilonConfigPanel::setEnglish(bool english)
{
    is_english_ = english;
    updateTexts();
}

QVector<QPushButton *> EpsilonConfigPanel::takeSettingsNavigationButtons(QWidget *newParent)
{
    if (!newParent || !settings_navigation_track_)
    {
        return {};
    }

    auto *trackLayout = qobject_cast<QHBoxLayout *>(settings_navigation_track_->layout());
    if (!trackLayout)
    {
        return {};
    }

    QVector<QPushButton *> buttons = page_buttons_;
    if (advanced_features_button_)
    {
        buttons.append(advanced_features_button_);
    }
    for (QPushButton *button : buttons)
    {
        if (!button)
        {
            continue;
        }
        trackLayout->removeWidget(button);
        button->setParent(newParent);
    }
    if (settings_navigation_tabs_)
    {
        settings_navigation_tabs_->hide();
    }
    return buttons;
}

void EpsilonConfigPanel::setSettingsNavigationVisible(bool visible)
{
    settings_navigation_visible_ = visible;
    updateSettingsNavigationVisibility();
}

void EpsilonConfigPanel::updateSettingsNavigationVisibility()
{
    for (int index = 0; index < page_buttons_.size(); ++index)
    {
        page_buttons_[index]->setVisible(settings_navigation_visible_ &&
                                          (index < 3 || advanced_features_expanded_));
    }
    if (advanced_features_button_)
    {
        advanced_features_button_->setVisible(settings_navigation_visible_);
    }
}

void EpsilonConfigPanel::setAdvancedFeaturesExpanded(bool expanded)
{
    advanced_features_expanded_ = expanded;
    if (advanced_features_button_)
    {
        const QString label = is_english_ ? QStringLiteral("Advanced features") : QStringLiteral("高级功能");
        advanced_features_button_->setText(expanded
            ? (is_english_ ? QStringLiteral("Hide advanced") : QStringLiteral("收起高级功能"))
            : label);
        advanced_features_button_->setAccessibleName(label);
        advanced_features_button_->setToolTip(expanded
            ? (is_english_ ? QStringLiteral("Hide advanced settings and calibration") : QStringLiteral("收起高级设置和校准维护"))
            : (is_english_ ? QStringLiteral("Show advanced settings and calibration") : QStringLiteral("展开高级设置和校准维护")));
    }
    updateSettingsNavigationVisibility();
    if (!expanded && pages_ && pages_->currentIndex() >= 3 && !page_buttons_.isEmpty())
        page_buttons_.first()->click();
    if (pages_) pages_->updateGeometry();
    updateSettingsTexts();
    updateSettingsControls();
}

void EpsilonConfigPanel::createSettingsPages()
{
    for (auto group : {VaporView::EpsilonSettingsGroup::Installation,
                       VaporView::EpsilonSettingsGroup::Fusion})
    {
        auto *page = new QWidget(pages_);
        page->setObjectName(group == VaporView::EpsilonSettingsGroup::Installation
            ? QStringLiteral("epsilonInstallationPage") : QStringLiteral("epsilonFusionPage"));
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 10, 0, 12);
        layout->setSpacing(10);
        auto *hint = new QLabel(settings_actions_card_);
        hint->setWordWrap(true);
        hint->setProperty("epsilonSecondaryText", true);
        qobject_cast<QVBoxLayout *>(settings_actions_card_->findChild<QWidget *>(QStringLiteral("epsilonParameterActionsCardBody"))->layout())->addWidget(hint);
        settings_hints_.append(hint);
        auto *fields = new QWidget(page);
        fields->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
        auto *grid = new QGridLayout(fields);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(24);
        grid->setVerticalSpacing(8);
        grid->setAlignment(Qt::AlignTop);
        settings_grids_.append(grid);
        layout->addWidget(fields);
        QVector<SectionCard> cards;
        const int cardCount = group == VaporView::EpsilonSettingsGroup::Installation ? 3 : 4;
        for (int cardIndex = 0; cardIndex < cardCount; ++cardIndex)
        {
            const SectionCard card = createSectionCard(fields,
                QStringLiteral("epsilonParameterCard_%1_%2").arg(static_cast<int>(group)).arg(cardIndex),
                group == VaporView::EpsilonSettingsGroup::Installation ? QStringLiteral("satellite") : QStringLiteral("activity"));
            card.card->setProperty("epsilonParameterCard", true);
            card.card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
            cards.append(card);
            settings_cards_.append(card.card);
            settings_card_titles_.append(card.title);
        }
        if (group == VaporView::EpsilonSettingsGroup::Installation)
        {
            dual_antenna_grid_ = new QGridLayout();
            dual_antenna_grid_->setHorizontalSpacing(24);
            dual_antenna_grid_->setVerticalSpacing(10);
            cards[2].body_layout->addLayout(dual_antenna_grid_);
        }
        int descriptorIndex = 0;
        for (const auto& descriptor : VaporView::epsilonParameterDescriptors(group))
        {
            const int cardIndex = group == VaporView::EpsilonSettingsGroup::Installation
                ? (descriptorIndex < 3 ? 0 : descriptorIndex < 6 ? 1 : 2)
                : (descriptorIndex < 4 ? 0 : descriptorIndex < 7 ? 1 : descriptorIndex < 10 ? 2 : 3);
            ++descriptorIndex;
            auto *row = createParameterField(descriptor, cards[cardIndex].card);
            if (group == VaporView::EpsilonSettingsGroup::Installation && cardIndex == 2)
                dual_antenna_grid_->addWidget(row, descriptorIndex - 7, 0);
            else
                cards[cardIndex].body_layout->addWidget(row);
        }
        layout->addStretch(1);
        pages_->addWidget(page);
    }
    arrangeSettingsFields(width() >= 760);
}

QWidget *EpsilonConfigPanel::createParameterField(const VaporView::EpsilonParameterDescriptor& descriptor, QWidget *parent)
{
    auto *row = new QWidget(parent);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    row->setObjectName(QStringLiteral("epsilonParameterRow_%1").arg(QString::fromStdString(descriptor.name)));
    auto *rowLayout = new QGridLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(3);
    SettingsField field;
    field.row = row;
    field.name = descriptor.name;
    field.group = descriptor.group;
    field.label = new QLabel(row);
    field.label->setWordWrap(true);
    field.label->setProperty("epsilonParameterLabel", true);
    field.label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    rowLayout->setColumnStretch(0, 1);
    rowLayout->addWidget(field.label, 0, 0);
    if (descriptor.kind == VaporView::EpsilonParameterKind::Boolean)
    {
        auto *editor = new EpsilonParameterCheckBox(row);
        field.editor = editor;
        connect(editor, &QCheckBox::toggled, this, [this]() { updateSettingsControls(); });
    }
    else if (descriptor.kind == VaporView::EpsilonParameterKind::Enumeration)
    {
        auto *editor = new QComboBox(row);
        for (const auto& option : descriptor.options)
            editor->addItem(QString::fromStdString(option.label_zh), option.value);
        editor->setMinimumWidth(0);
        editor->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        VaporView::configureComboBoxPopup(editor, VaporView::isDarkThemeEnabled());
        field.editor = editor;
        connect(editor, &QComboBox::currentIndexChanged, this, [this]() { updateSettingsControls(); });
    }
    else
    {
        auto *editor = new QDoubleSpinBox(row);
        editor->setDecimals(6);
        editor->setRange(descriptor.has_range ? descriptor.minimum : -1e9,
                 descriptor.has_range ? descriptor.maximum : 1e9);
        editor->setSuffix(descriptor.unit.empty() ? QString() : QStringLiteral(" ") + QString::fromStdString(descriptor.unit));
        field.editor = editor;
        connect(editor, &QDoubleSpinBox::valueChanged, this, [this]() { updateSettingsControls(); });
    }
    field.editor->setObjectName(QStringLiteral("epsilonParameter_%1").arg(QString::fromStdString(field.name)));
    field.editor->setProperty("epsilonParameterName", QString::fromStdString(field.name));
    field.editor->setFixedHeight(VaporView::Ground::MainSupport::kMainPageInputHeight);
    field.editor->setFocusPolicy(Qt::TabFocus);
    field.editor->setMinimumWidth(descriptor.kind == VaporView::EpsilonParameterKind::Enumeration ? 190 : 0);
    field.editor->setMaximumWidth(descriptor.kind == VaporView::EpsilonParameterKind::Enumeration ? 240 : 170);
    rowLayout->addWidget(field.editor, 0, 1);
    field.state = new QLabel(row);
    field.state->setWordWrap(true);
    field.state->setProperty("epsilonSecondaryText", true);
    rowLayout->addWidget(field.state, 1, 0, 1, 2);
    settings_fields_.append(field);
    return row;
}

void EpsilonConfigPanel::arrangeSettingsFields(bool twoColumns)
{
    int cardOffset = 0;
    for (int groupIndex = 0; groupIndex < settings_grids_.size(); ++groupIndex)
    {
        auto *grid = settings_grids_[groupIndex];
        const int count = groupIndex == 0 ? 3 : 4;
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, twoColumns ? 1 : 0);
        for (int i = 0; i < count; ++i)
        {
            auto *card = settings_cards_[cardOffset + i];
            grid->removeWidget(card);
            if (twoColumns && groupIndex == 0 && i == 2)
                grid->addWidget(card, 1, 0, 1, 2, Qt::AlignTop);
            else
                grid->addWidget(card, twoColumns ? i / 2 : i, twoColumns ? i % 2 : 0, Qt::AlignTop);
            card->setProperty("epsilonSettingsFieldColumn", twoColumns ? i % 2 : 0);
        }
        cardOffset += count;
    }
    if (actions_container_ && pages_->currentIndex() != 0)
    {
        auto *header = qobject_cast<QHBoxLayout *>(settings_actions_host_->layout());
        auto *body = qobject_cast<QVBoxLayout *>(settings_actions_body_->layout());
        header->removeWidget(actions_container_);
        body->removeWidget(actions_container_);
        if (width() >= 760)
            header->addWidget(actions_container_, 0, Qt::AlignRight | Qt::AlignVCenter);
        else
            body->insertWidget(0, actions_container_);
    }
    if (dual_antenna_grid_)
    {
        dual_antenna_grid_->setColumnStretch(0, 1);
        dual_antenna_grid_->setColumnStretch(1, twoColumns ? 1 : 0);
        int index = 0;
        for (auto& field : settings_fields_)
        {
            if (field.name.rfind("GNSS_L_ANT2_ANT1_", 0) != 0 &&
                field.name != "GNSS_ANTS_HEADING_BIAS" && field.name != "GNSS_L_ANTS_BASE_LINE") continue;
            dual_antenna_grid_->removeWidget(field.row);
            dual_antenna_grid_->addWidget(field.row, twoColumns && index >= 3 ? index - 3 : index,
                                         twoColumns && index >= 3 ? 1 : 0, Qt::AlignTop);
            ++index;
        }
    }
    if (pages_) pages_->updateGeometry();
}

void EpsilonConfigPanel::createAdvancedSettingsPage()
{
    auto *page = new QWidget(pages_);
    page->setObjectName(QStringLiteral("epsilonAdvancedPage"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 10, 0, 12);
    layout->setSpacing(10);
    auto *selectorRow = new QWidget(page);
    auto *selectorLayout = new QHBoxLayout(selectorRow);
    selectorLayout->setContentsMargins(0, 0, 0, 0);
    auto *selectorLabel = new QLabel(selectorRow);
    selectorLabel->setObjectName(QStringLiteral("epsilonAdvancedGroupLabel"));
    advanced_group_label_ = selectorLabel;
    advanced_group_combo_ = new QComboBox(selectorRow);
    advanced_group_combo_->setObjectName(QStringLiteral("epsilonAdvancedGroupCombo"));
    advanced_group_combo_->setMinimumWidth(220);
    VaporView::configureComboBoxPopup(advanced_group_combo_, VaporView::isDarkThemeEnabled());
    advanced_groups_ = {VaporView::EpsilonSettingsGroup::Communication, VaporView::EpsilonSettingsGroup::Filters,
                        VaporView::EpsilonSettingsGroup::Sensors, VaporView::EpsilonSettingsGroup::InitialState,
                        VaporView::EpsilonSettingsGroup::ReferencePoint, VaporView::EpsilonSettingsGroup::ExternalAids};
    for (auto group : advanced_groups_)
        advanced_group_combo_->addItem(QString::number(static_cast<int>(group)), static_cast<int>(group));
    selectorLayout->addWidget(selectorLabel);
    selectorLayout->addWidget(advanced_group_combo_);
    selectorLayout->addStretch(1);
    layout->addWidget(selectorRow);
    advanced_hint_ = new QLabel(page);
    advanced_hint_->setWordWrap(true);
    advanced_hint_->setProperty("epsilonSecondaryText", true);
    layout->addWidget(advanced_hint_);
    advanced_pages_ = new EpsilonPages(page);
    advanced_pages_->setObjectName(QStringLiteral("epsilonAdvancedGroupPages"));
    layout->addWidget(advanced_pages_, 1);
    for (auto group : advanced_groups_)
    {
        auto *groupPage = new QWidget(advanced_pages_);
        auto *groupLayout = new QVBoxLayout(groupPage);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        const auto descriptors = VaporView::epsilonParameterDescriptors(group);
        auto card = createSectionCard(groupPage, QStringLiteral("epsilonAdvancedCard_%1").arg(static_cast<int>(group)), QStringLiteral("sliders-vertical"));
        card.card->setProperty("epsilonParameterCard", true);
        advanced_titles_.append(card.title);
        for (const auto& descriptor : descriptors)
            card.body_layout->addWidget(createParameterField(descriptor, card.card));
        groupLayout->addWidget(card.card);
        groupLayout->addStretch(1);
        advanced_pages_->addWidget(groupPage);
    }
    advanced_group_combo_->addItem(QStringLiteral("D4G"));
    auto *dgnssPage = new QWidget(advanced_pages_);
    auto *dgnssLayout = new QVBoxLayout(dgnssPage);
    dgnssLayout->setContentsMargins(0, 0, 0, 0);
    auto dgnssCard = createSectionCard(dgnssPage, QStringLiteral("epsilonDgnssCard"), QStringLiteral("sliders-vertical"));
    dgnssCard.card->setProperty("epsilonParameterCard", true);
    dgnss_title_ = dgnssCard.title;
    for (const auto& descriptor : VaporView::epsilonDgnssDescriptors())
    {
        DgnssField field;
        field.name = descriptor.name;
        auto *row = new QWidget(dgnssCard.card);
        auto *rowLayout = new QGridLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        field.label = new QLabel(row);
        field.label->setWordWrap(true);
        field.label->setProperty("epsilonParameterLabel", true);
        field.editor = new QLineEdit(row);
        field.editor->setObjectName(QStringLiteral("epsilonDgnssField_%1").arg(QString::fromStdString(field.name)));
        field.editor->setMinimumHeight(36);
        field.editor->setMaximumWidth(240);
        field.editor->setMinimumWidth(120);
        field.editor->setPlaceholderText(QStringLiteral("--"));
        if (descriptor.secret) field.editor->setEchoMode(QLineEdit::Password);
        field.state = new QLabel(row);
        field.state->setWordWrap(true);
        field.state->setProperty("epsilonSecondaryText", true);
        rowLayout->addWidget(field.label, 0, 0);
        rowLayout->addWidget(field.editor, 0, 1);
        rowLayout->addWidget(field.state, 1, 0, 1, 2);
        rowLayout->setColumnStretch(0, 1);
        dgnssCard.body_layout->addWidget(row);
        connect(field.editor, &QLineEdit::textChanged, this, [this]() { updateSettingsControls(); });
        dgnss_fields_.append(field);
    }
    dgnssLayout->addWidget(dgnssCard.card);
    dgnssLayout->addStretch(1);
    advanced_pages_->addWidget(dgnssPage);
    connect(advanced_group_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        advanced_pages_->setCurrentIndex(index);
        updateSettingsTexts();
        updateSettingsControls();
    });
    pages_->addWidget(page);
}

VaporView::EpsilonSettingsGroup EpsilonConfigPanel::currentSettingsGroup() const
{
    if (isDgnssPage()) return VaporView::EpsilonSettingsGroup::Communication;
    if (pages_->currentIndex() == 3 && advanced_group_combo_)
        return static_cast<VaporView::EpsilonSettingsGroup>(advanced_group_combo_->currentData().toInt());
    return pages_->currentIndex() == 2 ? VaporView::EpsilonSettingsGroup::Fusion
                                    : VaporView::EpsilonSettingsGroup::Installation;
}

bool EpsilonConfigPanel::isDgnssPage() const
{
    return pages_->currentIndex() == 3 && advanced_group_combo_ && advanced_group_combo_->currentIndex() == advanced_groups_.size();
}

VaporView::EpsilonDgnssOperation EpsilonConfigPanel::editedDgnss() const
{
    VaporView::EpsilonDgnssOperation operation;
    for (const auto& field : dgnss_fields_)
    {
        const auto *descriptor = VaporView::epsilonDgnssDescriptor(field.name);
        if (field.read && !field.unsupported && descriptor && descriptor->writable && field.editor->text() != field.original)
            operation.values.emplace(field.name, field.editor->text().toStdString());
    }
    return operation;
}

void EpsilonConfigPanel::setDgnssSnapshot(const VaporView::EpsilonDgnssSnapshot& snapshot, bool partial)
{
    settings_saved_ = snapshot.saved;
    settings_verified_ = snapshot.readback_verified;
    settings_restart_required_ = settings_restart_required_ || snapshot.restart_required;
    settings_status_custom_ = false;
    settings_status_label_->setProperty("epsilonSettingsError", false);
    for (auto& field : dgnss_fields_)
    {
        const auto value = snapshot.values.find(field.name);
        const bool unsupported = std::find(snapshot.unsupported.begin(), snapshot.unsupported.end(), field.name) != snapshot.unsupported.end();
        if (partial && value == snapshot.values.end() && !unsupported) continue;
        field.read = value != snapshot.values.end() && !unsupported;
        field.unsupported = unsupported;
        field.original = field.read ? QString::fromStdString(value->second) : QString();
        const QSignalBlocker blocker(field.editor);
        field.editor->setText(field.original);
    }
    updateSettingsTexts();
    updateSettingsControls();
}

VaporView::EpsilonSettingsOperation EpsilonConfigPanel::editedSettings() const
{
    VaporView::EpsilonSettingsOperation operation;
    operation.group = currentSettingsGroup();
    for (const auto& field : settings_fields_)
    {
        const auto *descriptor = VaporView::epsilonParameterDescriptor(field.name);
        if (field.group != operation.group || !field.read || !field.value_supported || field.unsupported ||
            !descriptor || !descriptor->writable)
            continue;
        double value = field.original;
        if (auto *spinEditor = qobject_cast<QDoubleSpinBox *>(field.editor))
            value = spinEditor->value();
        else if (auto *checkEditor = qobject_cast<QCheckBox *>(field.editor))
            value = checkEditor->isChecked() ? 1 : 0;
        else if (auto *comboEditor = qobject_cast<QComboBox *>(field.editor))
        {
            if (!comboEditor->currentData().isValid())
                continue;
            value = comboEditor->currentData().toDouble();
        }
        if (!VaporView::epsilonSettingsValuesEqual(value, field.original))
            operation.values.emplace(field.name, value);
    }
    return operation;
}

void EpsilonConfigPanel::invalidateSettings(bool preserveMaintenanceState)
{
    settings_pending_ = false;
    if (!preserveMaintenanceState)
    {
        settings_restart_required_ = false;
        maintenance_verification_pending_ = false;
        maintenance_running_ = false;
        maintenance_cancel_requested_ = false;
        live_epsilon_data_.device_info_valid = false;
        live_epsilon_data_.hardware_name.clear();
        live_epsilon_data_.firmware_name.clear();
        live_epsilon_data_.serial_number.fill(0);
        updateDeviceInfoTexts();
    }
    for (auto& field : dgnss_fields_)
    {
        field.read = false;
        field.unsupported = false;
        field.original.clear();
        const QSignalBlocker blocker(field.editor);
        field.editor->clear();
    }
    settings_saved_ = false;
    settings_verified_ = false;
    settings_status_custom_ = false;
    settings_status_label_->clear();
    for (auto& field : settings_fields_)
    {
        field.read = false;
        field.unsupported = false;
        field.value_supported = false;
        const QSignalBlocker blocker(field.editor);
        if (auto *spinEditor = qobject_cast<QDoubleSpinBox *>(field.editor))
        {
            spinEditor->setSpecialValueText(QStringLiteral("--"));
            spinEditor->setValue(spinEditor->minimum());
        }
        else if (auto *checkEditor = qobject_cast<QCheckBox *>(field.editor))
        {
            checkEditor->setTristate(true);
            checkEditor->setCheckState(Qt::PartiallyChecked);
        }
        else if (auto *comboEditor = qobject_cast<QComboBox *>(field.editor))
            comboEditor->setCurrentIndex(-1);
    }
    updateSettingsTexts();
    updateSettingsControls();
}

void EpsilonConfigPanel::setSettingsSnapshot(const VaporView::EpsilonSettingsSnapshot& snapshot, bool partial)
{
    for (auto& field : settings_fields_)
    {
        if (field.group != snapshot.group)
            continue;
        const auto value = snapshot.values.find(field.name);
        if (partial && value == snapshot.values.end() &&
            std::find(snapshot.unsupported.begin(), snapshot.unsupported.end(), field.name) == snapshot.unsupported.end())
            continue;
        field.unsupported = std::find(snapshot.unsupported.begin(), snapshot.unsupported.end(), field.name) != snapshot.unsupported.end();
        field.read = value != snapshot.values.end() && std::isfinite(value->second) && !field.unsupported;
        const auto *descriptor = VaporView::epsilonParameterDescriptor(field.name);
        field.value_supported = field.read;
        const QSignalBlocker blocker(field.editor);
        if (auto *comboEditor = qobject_cast<QComboBox *>(field.editor); comboEditor && descriptor)
            while (comboEditor->count() > static_cast<int>(descriptor->options.size()))
                comboEditor->removeItem(comboEditor->count() - 1);
        if (field.read && descriptor)
        {
            if (descriptor->kind == VaporView::EpsilonParameterKind::Real && descriptor->has_range && (value->second < descriptor->minimum || value->second > descriptor->maximum))
                field.value_supported = false;
            if (descriptor->kind == VaporView::EpsilonParameterKind::Boolean && value->second != 0 && value->second != 1)
                field.value_supported = false;
            if (auto *comboEditor = qobject_cast<QComboBox *>(field.editor))
                field.value_supported = field.value_supported && comboEditor->findData(value->second) >= 0;
        }
        if (!field.read)
        {
            if (auto *spinEditor = qobject_cast<QDoubleSpinBox *>(field.editor))
            {
                spinEditor->setSpecialValueText(QStringLiteral("--"));
                spinEditor->setValue(spinEditor->minimum());
            }
            else if (auto *comboEditor = qobject_cast<QComboBox *>(field.editor)) comboEditor->setCurrentIndex(-1);
            else if (auto *checkEditor = qobject_cast<QCheckBox *>(field.editor))
            {
                checkEditor->setTristate(true);
                checkEditor->setCheckState(Qt::PartiallyChecked);
            }
            continue;
        }
        field.original = value->second;
        if (auto *spinEditor = qobject_cast<QDoubleSpinBox *>(field.editor))
        {
            spinEditor->setSpecialValueText(QString());
            const double minimum = descriptor && descriptor->has_range ? descriptor->minimum : -1e9;
            const double maximum = descriptor && descriptor->has_range ? descriptor->maximum : 1e9;
            spinEditor->setRange(std::min(minimum, value->second), std::max(maximum, value->second));
            spinEditor->setValue(value->second);
        }
        else if (auto *checkEditor = qobject_cast<QCheckBox *>(field.editor))
        {
            checkEditor->setTristate(!field.value_supported);
            checkEditor->setCheckState(field.value_supported
                ? (value->second != 0 ? Qt::Checked : Qt::Unchecked) : Qt::PartiallyChecked);
        }
        else if (auto *comboEditor = qobject_cast<QComboBox *>(field.editor))
        {
            if (!field.value_supported)
                comboEditor->addItem(QString::number(value->second, 'g', 12), value->second);
            comboEditor->setCurrentIndex(comboEditor->findData(value->second));
        }
    }
    if (snapshot.saved)
    {
        settings_saved_ = true;
        settings_verified_ = snapshot.readback_verified;
    }
    settings_restart_required_ = settings_restart_required_ || snapshot.restart_required;
    settings_pending_ = false;
    settings_status_custom_ = false;
    settings_status_label_->setProperty("epsilonSettingsError", false);
    settings_status_label_->clear();
    updateSettingsTexts();
    updateSettingsControls();
}

void EpsilonConfigPanel::setSettingsOperationPending(bool pending)
{
    if (maintenance_running_ && !pending) return;
    if (settings_pending_ == pending)
        return;
    settings_pending_ = pending;
    settings_status_custom_ = false;
    updateSettingsTexts();
    updateSettingsControls();
}

void EpsilonConfigPanel::setSettingsAvailable(bool available)
{
    settings_available_ = available;
    updateSettingsControls();
}

void EpsilonConfigPanel::setMaintenanceResult(const VaporView::EpsilonMaintenanceResult& result)
{
    const bool magnetic = result.action == VaporView::EpsilonMaintenanceAction::Magnetic2D || result.action == VaporView::EpsilonMaintenanceAction::Magnetic3D;
    maintenance_running_ = result.status == VaporView::EpsilonMaintenanceStatus::Running ||
        (magnetic && result.status == VaporView::EpsilonMaintenanceStatus::Acknowledged);
    settings_pending_ = maintenance_running_;
    if (!maintenance_running_) maintenance_cancel_requested_ = false;
    maintenance_progress_->setVisible(magnetic && (maintenance_running_ || result.progress_known));
    maintenance_progress_->setRange(0, result.progress_known ? 100 : 0);
    if (result.progress_known) maintenance_progress_->setValue(std::clamp(result.progress_percent, 0, 100));
    settings_restart_required_ = settings_restart_required_ || result.restart_required;
    maintenance_verification_pending_ = maintenance_verification_pending_ || maintenance_running_ || result.restart_required || result.status == VaporView::EpsilonMaintenanceStatus::Completed ||
        result.status == VaporView::EpsilonMaintenanceStatus::Acknowledged ||
        result.status == VaporView::EpsilonMaintenanceStatus::SentUnverified;
    const QStringList statusNames = is_english_
        ? QStringList{QStringLiteral("Acknowledged, completion unverified"), QStringLiteral("Completion reported"), QStringLiteral("Failed"), QStringLiteral("Unsupported"), QStringLiteral("Sent, result unverified")}
        : QStringList{QStringLiteral("已确认接收，完成未验证"), QStringLiteral("已报告完成"), QStringLiteral("失败"), QStringLiteral("不支持"), QStringLiteral("已发送，结果未验证")};
    const int statusIndex = static_cast<int>(result.status);
    const QString status = result.status == VaporView::EpsilonMaintenanceStatus::Running
        ? (is_english_ ? QStringLiteral("Running; navigation collection paused") : QStringLiteral("正在校准；导航采集暂停"))
        : result.status == VaporView::EpsilonMaintenanceStatus::Cancelled
        ? (is_english_ ? QStringLiteral("Exit requested; device stop is unverified") : QStringLiteral("已请求退出，停止状态未验证"))
        : statusNames.value(statusIndex, is_english_ ? QStringLiteral("Unknown") : QStringLiteral("未知"));
    const QString saveStatus = magnetic
        ? (is_english_ ? QStringLiteral("Device auto-save: %1").arg(result.saved ? QStringLiteral("reported") : QStringLiteral("unconfirmed"))
                       : QStringLiteral("设备自动保存：%1").arg(result.saved ? QStringLiteral("已报告") : QStringLiteral("未确认")))
        : (is_english_ ? QStringLiteral("Save ACK: %1").arg(result.saved ? QStringLiteral("received") : QStringLiteral("unconfirmed"))
                       : QStringLiteral("保存 ACK：%1").arg(result.saved ? QStringLiteral("已收到") : QStringLiteral("未确认")));
    const QString nextStep = maintenance_running_
        ? (is_english_ ? QStringLiteral("Wait for device results and navigation recovery; edits and restart are disabled while running.")
                       : QStringLiteral("请等待设备结果及导航恢复；运行期间编辑和重启均已禁用。"))
        : (is_english_ ? QStringLiteral("Restart is available for verification; verify the physical result before repeating.")
                       : QStringLiteral("可重启设备进行核对；再次校准前须实机核对结果。"));
    const QString text = is_english_
        ? QStringLiteral("Maintenance status: %1; %2. %3").arg(status, saveStatus, nextStep)
        : QStringLiteral("维护状态：%1；%2。%3").arg(status, saveStatus, nextStep);
    QString details = text;
    if (result.progress_known) details += QStringLiteral(" %1%").arg(result.progress_percent);
    if (result.fit_error_known) details += (is_english_ ? QStringLiteral(" Fit error: %1.") : QStringLiteral(" 拟合误差：%1。" )).arg(result.fit_error);
    if (!result.algorithm.empty()) details += (is_english_ ? QStringLiteral(" Algorithm: ") : QStringLiteral(" 等级：")) + QString::fromStdString(result.algorithm);
    setSettingsStatus(result.error.empty() ? details : details + QStringLiteral(" ") + QString::fromStdString(result.error));
    updateSettingsControls();
}

void EpsilonConfigPanel::setRestartResult(bool succeeded)
{
    if (succeeded)
    {
        maintenance_verification_pending_ = false;
        settings_restart_required_ = false;
        maintenance_running_ = false;
        maintenance_cancel_requested_ = false;
    }
    updateSettingsControls();
}

QByteArray EpsilonConfigPanel::exportSettingsJson() const
{
    QJsonArray groups;
    for (auto group : VaporView::epsilonSettingsGroups())
    {
        QJsonObject values;
        for (const auto& field : settings_fields_)
            if (field.group == group && field.read && !field.unsupported)
                values.insert(QString::fromStdString(field.name), field.original);
        if (!values.isEmpty()) groups.append(QJsonObject{{QStringLiteral("group"), static_cast<int>(group)}, {QStringLiteral("values"), values}});
    }
    if (groups.isEmpty()) return {};
    return QJsonDocument(QJsonObject{{QStringLiteral("schema_version"), 1}, {QStringLiteral("device"), QStringLiteral("EPSILON")}, {QStringLiteral("groups"), groups}}).toJson();
}

bool EpsilonConfigPanel::previewSettingsImport(const QByteArray& json, VaporView::EpsilonSettingsOperation& changes, QString& error) const
{
    changes = {};
    changes.group = currentSettingsGroup();
    error.clear();
    auto fail = [&](const QString& text) { changes.values.clear(); error = text; return false; };
    if (json.size() > 1024 * 1024)
        return fail(is_english_ ? QStringLiteral("Snapshot exceeds 1 MB") : QStringLiteral("快照超过 1 MB"));
    const auto document = QJsonDocument::fromJson(json);
    const auto root = document.object();
    auto integer = [](const QJsonValue& value, int minimum, int maximum) {
        return value.isDouble() && std::isfinite(value.toDouble()) &&
            value.toDouble() >= minimum && value.toDouble() <= maximum &&
            std::floor(value.toDouble()) == value.toDouble();
    };
    if (!document.isObject() || !integer(root.value(QStringLiteral("schema_version")), 1, 1) ||
        root.value(QStringLiteral("device")).toString() != QStringLiteral("EPSILON") || !root.value(QStringLiteral("groups")).isArray())
        return fail(is_english_ ? QStringLiteral("Invalid EPSILON snapshot schema") : QStringLiteral("EPSILON 快照格式无效"));
    std::set<int> seenGroups;
    for (const auto& groupValue : root.value(QStringLiteral("groups")).toArray())
    {
        const auto object = groupValue.toObject();
        const auto groupNumber = object.value(QStringLiteral("group"));
        if (!groupValue.isObject() || !integer(groupNumber, 0, 255))
            return fail(is_english_ ? QStringLiteral("Snapshot group must be an integer") : QStringLiteral("快照分组必须是整数"));
        const int groupId = static_cast<int>(groupNumber.toDouble());
        const auto group = static_cast<VaporView::EpsilonSettingsGroup>(groupId);
        if (!VaporView::isValidEpsilonSettingsGroup(group) || !seenGroups.insert(groupId).second || !object.value(QStringLiteral("values")).isObject())
            return fail(is_english_ ? QStringLiteral("Unknown, repeated, or invalid snapshot group") : QStringLiteral("快照分组未知、重复或无效"));
        const auto values = object.value(QStringLiteral("values")).toObject();
        for (auto it = values.begin(); it != values.end(); ++it)
        {
            const std::string name = it.key().toStdString();
            const auto *descriptor = VaporView::epsilonParameterDescriptor(name);
            if (!descriptor || descriptor->group != group || !it.value().isDouble() || !std::isfinite(it.value().toDouble()))
                return fail(is_english_ ? QStringLiteral("Snapshot contains an unknown, misplaced or invalid parameter") : QStringLiteral("快照包含未知、跨组或无效参数"));
            if (group != changes.group) continue;
            const auto field = std::find_if(settings_fields_.begin(), settings_fields_.end(), [&](const auto& entry) { return entry.name == name && entry.group == changes.group; });
            if (field == settings_fields_.end() || !field->read || !field->value_supported || field->unsupported || !descriptor->writable) continue;
            const double value = it.value().toDouble();
            if (auto *spin = qobject_cast<QDoubleSpinBox *>(field->editor))
                if (value < spin->minimum() || value > spin->maximum())
                    return fail(is_english_ ? QStringLiteral("Imported value exceeds the editor range") : QStringLiteral("导入值超出编辑器范围"));
            if (!VaporView::epsilonSettingsValuesEqual(field->original, value)) changes.values.emplace(name, value);
        }
    }
    if (changes.values.empty())
    { error = is_english_ ? QStringLiteral("No editable differences in the current group; read the target device first") : QStringLiteral("当前分组没有可编辑差异；请先读取目标设备"); return false; }
    std::string validationError;
    if (!VaporView::validateEpsilonSettings(changes, validationError)) return fail(QString::fromStdString(validationError));
    return true;
}

void EpsilonConfigPanel::applyImportedSettings(const VaporView::EpsilonSettingsOperation& changes)
{
    if (settings_pending_ || changes.group != currentSettingsGroup()) return;
    std::string error;
    if (!VaporView::validateEpsilonSettings(changes, error)) return;
    for (const auto& field : settings_fields_)
    {
        const auto value = changes.values.find(field.name);
        if (value == changes.values.end()) continue;
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(field.editor))
            if (value->second < spin->minimum() || value->second > spin->maximum()) return;
    }
    for (auto& field : settings_fields_)
    {
        const auto value = changes.values.find(field.name);
        const auto *descriptor = VaporView::epsilonParameterDescriptor(field.name);
        if (value == changes.values.end() || field.group != changes.group || !field.read || !field.value_supported || field.unsupported || !descriptor || !descriptor->writable) continue;
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(field.editor)) spin->setValue(value->second);
        else if (auto *check = qobject_cast<QCheckBox *>(field.editor)) check->setChecked(value->second != 0);
        else if (auto *combo = qobject_cast<QComboBox *>(field.editor)) combo->setCurrentIndex(combo->findData(value->second));
    }
    updateSettingsControls();
}

void EpsilonConfigPanel::setSettingsStatus(const QString& text)
{
    settings_status_custom_ = true;
    settings_status_label_->setProperty("epsilonSettingsError", false);
    settings_status_label_->setText(text);
    applyAppearance();
}

void EpsilonConfigPanel::setSettingsError(const QString& text)
{
    settings_status_custom_ = true;
    settings_pending_ = false;
    settings_status_label_->setProperty("epsilonSettingsError", true);
    settings_status_label_->setText(text);
    applyAppearance();
    updateSettingsControls();
}

void EpsilonConfigPanel::updateSettingsControls()
{
    if (!save_button_)
        return;
    const bool settingsPage = pages_->currentIndex() != 0 && pages_->currentIndex() != 4;
    const bool busy = settings_pending_ || maintenance_running_;
    settings_read_button_->setVisible(settingsPage);
    device_restart_button_->setVisible(pages_->currentIndex() != 0);
    settings_status_label_->setVisible(pages_->currentIndex() != 0);
    actions_container_->setVisible(true);
    save_button_->setVisible(pages_->currentIndex() != 4);
    settings_export_button_->setVisible(settingsPage && !isDgnssPage());
    settings_import_button_->setVisible(settingsPage && !isDgnssPage());
    settings_export_button_->setEnabled(!busy && !exportSettingsJson().isEmpty());
    settings_import_button_->setEnabled(settings_available_ && !busy);
    settings_read_button_->setEnabled(settings_available_ && !busy);
    device_restart_button_->setEnabled(settings_available_ && !busy &&
        (settings_restart_required_ || (pages_->currentIndex() == 4 && maintenance_verification_pending_)));
    communication_page_->setEnabled(!busy);
    save_button_->setEnabled(!busy && (!settingsPage || (settings_available_ && (isDgnssPage() ? !editedDgnss().values.empty() : !editedSettings().values.empty()))));
    for (auto& field : dgnss_fields_)
    {
        const auto *descriptor = VaporView::epsilonDgnssDescriptor(field.name);
        field.editor->setEnabled(settings_available_ && !busy && field.read && !field.unsupported && descriptor && descriptor->writable);
    }
    maintenance_cancel_button_->setVisible(maintenance_running_);
    maintenance_cancel_button_->setEnabled(maintenance_running_ && !maintenance_cancel_requested_);
    if (!maintenance_running_ && !maintenance_verification_pending_) maintenance_progress_->hide();
    for (auto& field : settings_fields_)
    {
        const auto *descriptor = VaporView::epsilonParameterDescriptor(field.name);
        field.editor->setEnabled(settings_available_ && !busy && field.read && field.value_supported && !field.unsupported && descriptor && descriptor->writable);
    }
    for (auto *button : maintenance_buttons_)
        button->setEnabled(settings_available_ && !busy && !settings_restart_required_ && !maintenance_verification_pending_);
}

void EpsilonConfigPanel::updateSettingsTexts()
{
    updateDeviceInfoTexts();
    settings_actions_title_->setText(is_english_ ? QStringLiteral("Device Parameters") : QStringLiteral("设备参数"));
    if (maintenance_title_)
        maintenance_title_->setText(is_english_ ? QStringLiteral("Calibration and Maintenance") : QStringLiteral("校准与维护"));
    if (dgnss_title_) dgnss_title_->setText(is_english_ ? QStringLiteral("Built-in 4G (D4G)") : QStringLiteral("内置 4G（D4G）"));
    if (maintenance_cancel_button_) maintenance_cancel_button_->setText(is_english_ ? QStringLiteral("Request Exit") : QStringLiteral("请求退出校准"));
    for (auto& field : dgnss_fields_)
    {
        const auto *descriptor = VaporView::epsilonDgnssDescriptor(field.name);
        field.label->setText(QString::fromStdString(is_english_ ? descriptor->label_en : descriptor->label_zh));
        field.state->setText(field.unsupported ? (is_english_ ? QStringLiteral("Not supported by this device") : QStringLiteral("当前设备不支持"))
            : !descriptor->writable ? (is_english_ ? QStringLiteral("Read only") : QStringLiteral("只读")) : QString());
        field.state->setVisible(field.unsupported || !descriptor->writable);
        field.editor->setToolTip(field.name == "RTCM_TYPE"
            ? (is_english_ ? QStringLiteral("Raw firmware value. The documented Ntrip mapping conflicts with the example; mapping is unverified.") : QStringLiteral("固件原始编号；官方表与示例的 Ntrip 映射矛盾，映射尚待核实。"))
            : !field.read ? (is_english_ ? QStringLiteral("Read this device before editing") : QStringLiteral("先读取当前设备，才能编辑")) : QString());
    }
    if (settings_export_button_) settings_export_button_->setText(is_english_ ? QStringLiteral("Export") : QStringLiteral("导出快照"));
    if (settings_import_button_) settings_import_button_->setText(is_english_ ? QStringLiteral("Import") : QStringLiteral("导入差异"));
    const QStringList cardTitles = is_english_
        ? QStringList{QStringLiteral("Installation Angles"), QStringLiteral("Main Antenna Lever Arm"), QStringLiteral("Dual Antenna Geometry"),
                      QStringLiteral("GNSS Aids"), QStringLiteral("Magnetometer Aids"), QStringLiteral("Stationary Constraints"), QStringLiteral("Startup Tare and Dynamics")}
        : QStringList{QStringLiteral("安装角"), QStringLiteral("主天线杆臂"), QStringLiteral("双天线几何"),
                      QStringLiteral("GNSS 辅助"), QStringLiteral("磁力计辅助"), QStringLiteral("静止约束"), QStringLiteral("启动零偏与动力学")};
    for (int i = 0; i < settings_card_titles_.size(); ++i)
        settings_card_titles_[i]->setText(cardTitles[i]);
    for (int i = 0; i < settings_hints_.size(); ++i)
        settings_hints_[i]->setVisible(pages_->currentIndex() == i + 1);
    if (advanced_group_combo_)
    {
        const QStringList advancedNames = is_english_
            ? QStringList{QStringLiteral("Communication"), QStringLiteral("Filters"), QStringLiteral("Sensors"), QStringLiteral("Initial State"), QStringLiteral("Reference Point"), QStringLiteral("External Aids")}
            : QStringList{QStringLiteral("通信设置"), QStringLiteral("滤波器"), QStringLiteral("传感器"), QStringLiteral("初始状态"), QStringLiteral("参考点"), QStringLiteral("外部辅助")};
        for (int i = 0; i < advanced_group_combo_->count() && i < advancedNames.size(); ++i)
            advanced_group_combo_->setItemText(i, advancedNames[i]);
        advanced_group_combo_->setItemText(advanced_groups_.size(), is_english_ ? QStringLiteral("Built-in 4G (D4G)") : QStringLiteral("内置 4G（D4G）"));
        if (pages_->currentIndex() == 3)
        {
            const QStringList hints = is_english_
                ? QStringList{QStringLiteral("Read before editing. The Main control port is protected; COMM1 settings stay read only."),
                    QStringLiteral("Read before editing. Zero disables a filter; no preset is applied automatically."),
                    QStringLiteral("Read before editing. GPIO1 is reserved for internal GNSS PPS; use options supported by this firmware."),
                    QStringLiteral("Zero restores device defaults. Initial yaw depends on heading/magnetic conditions; roll and pitch depend on gravity alignment. Fusion options are not changed automatically."),
                    QStringLiteral("Reference point uses the IMU frame: X forward, Y right, Z down, in metres. Zero disables mapping. This is separate from antenna lever arms."),
                    QStringLiteral("External aids require an actual input link and adequate data quality. No aiding option is enabled automatically.")}
                : QStringList{QStringLiteral("编辑前先读取。Main 控制端口受保护；COMM1 设置保持只读。"),
                    QStringLiteral("编辑前先读取。滤波参数 0 表示关闭；不会自动套用预设。"),
                    QStringLiteral("编辑前先读取。GPIO1 为内部 GNSS PPS 保留；仅使用当前固件支持的选项。"),
                    QStringLiteral("0 恢复设备默认值。初始航向与航向/磁辅助条件有关，横滚和俯仰与重力对准约束有关；不会自动修改融合开关。"),
                    QStringLiteral("参考点采用 IMU 坐标系：X 向前、Y 向右、Z 向下，单位为米。0 关闭映射；与天线杆臂设置不同。"),
                    QStringLiteral("外部辅助需要真实输入链路及满足要求的数据质量，不会自动开启辅助选项。")};
            advanced_hint_->setText(hints.value(advanced_group_combo_->currentIndex()));
            if (isDgnssPage()) advanced_hint_->setText(is_english_ ? QStringLiteral("D4G model only; read supported keys before editing. Navigation collection pauses during configuration. Credentials stay in memory and are excluded from JSON snapshots.") : QStringLiteral("仅适用于 D4G 型号；先读取支持键再编辑。配置期间导航采集暂停；凭据仅保留在内存，不进入 JSON 快照。"));
        }
        advanced_group_label_->setText(is_english_ ? QStringLiteral("Parameter group") : QStringLiteral("参数分组"));
        const QStringList advancedCardTitles = is_english_
            ? QStringList{QStringLiteral("Communication"), QStringLiteral("Filters"), QStringLiteral("Sensors"), QStringLiteral("Initial State"), QStringLiteral("Reference Point"), QStringLiteral("External Aids")}
            : QStringList{QStringLiteral("通信设置"), QStringLiteral("滤波器"), QStringLiteral("传感器"), QStringLiteral("初始状态"), QStringLiteral("参考点"), QStringLiteral("外部辅助")};
        for (int i = 0; i < advanced_titles_.size(); ++i)
            advanced_titles_[i]->setText(advancedCardTitles[i]);
    }
    const QStringList names = is_english_
        ? QStringList{QStringLiteral("Communication"), QStringLiteral("Installation & Antennas"), QStringLiteral("Navigation Fusion"), QStringLiteral("Advanced"), QStringLiteral("Calibration & Maintenance")}
        : QStringList{QStringLiteral("通信输出"), QStringLiteral("安装与天线"), QStringLiteral("导航融合"), QStringLiteral("高级设置"), QStringLiteral("校准与维护")};
    for (int i = 0; i < page_buttons_.size(); ++i)
    {
        const QStringList shortNames = {QStringLiteral("Output"), QStringLiteral("Installation"), QStringLiteral("Fusion"), QStringLiteral("Advanced"), QStringLiteral("Calibration")};
        page_buttons_[i]->setText(is_english_ ? shortNames[i] : names[i]);
        page_buttons_[i]->setAccessibleName(names[i]);
        page_buttons_[i]->setToolTip(names[i]);
    }
    if (advanced_features_button_)
    {
        const QString label = is_english_ ? QStringLiteral("Advanced features") : QStringLiteral("高级功能");
        advanced_features_button_->setText(advanced_features_expanded_
            ? (is_english_ ? QStringLiteral("Hide advanced") : QStringLiteral("收起高级功能"))
            : label);
        advanced_features_button_->setAccessibleName(label);
        advanced_features_button_->setToolTip(advanced_features_expanded_
            ? (is_english_ ? QStringLiteral("Hide advanced settings and calibration") : QStringLiteral("收起高级设置和校准维护"))
            : (is_english_ ? QStringLiteral("Show advanced settings and calibration") : QStringLiteral("展开高级设置和校准维护")));
    }
    for (auto *button : page_buttons_) button->updateGeometry();
    const QStringList maintenanceNames = is_english_
        ? QStringList{QStringLiteral("Level alignment"), QStringLiteral("Accel bias tare"), QStringLiteral("Gyro bias tare"), QStringLiteral("Magnetometer 2D"), QStringLiteral("Magnetometer 3D")}
        : QStringList{QStringLiteral("调平"), QStringLiteral("加表静态零偏"), QStringLiteral("陀螺静态零偏"), QStringLiteral("磁力计 2D 校准"), QStringLiteral("磁力计 3D 校准")};
    for (int i = 0; i < maintenance_buttons_.size(); ++i)
        maintenance_buttons_[i]->setText(maintenanceNames[i]);
    if (auto *hint = findChild<QLabel *>(QStringLiteral("epsilonMaintenanceHint")))
        hint->setText(is_english_ ? QStringLiteral("Level alignment and accel bias tare require a level, stationary device; gyro tare requires a stationary device. ACK does not confirm completion. Save and restart are required; verify the physical result before another calibration.")
                                  : QStringLiteral("调平和加表静态零偏要求设备水平静止；陀螺零偏要求静止。ACK 不代表完成。操作需要保存和重启；再次校准前须实机核对结果。"));
    auto *track = page_buttons_.first()->parentWidget();
    track->layout()->invalidate();
    auto *tabs = track->parentWidget();
    tabs->layout()->invalidate();
    tabs->updateGeometry();
    layout()->invalidate();
    settings_hints_[0]->setText(is_english_
        ? QStringLiteral("Read device configuration before editing. Reads pause navigation collection and may take tens of seconds. X forward, Y right, Z down; angles in degrees, lever arms in metres. Check installed antenna geometry. Restart after saving.")
        : QStringLiteral("编辑前先读取设备配置；读取期间暂停导航采集，可能需要几十秒。X 向前、Y 向右、Z 向下；角度为度，杆臂为米。请核对实际天线安装几何，保存后重启。"));
    settings_hints_[1]->setText(is_english_
        ? QStringLiteral("Read device configuration before editing. Reads pause navigation collection and may take tens of seconds. Match dynamics and motion constraints to the actual vehicle. Unsupported parameters stay disabled. Restart after saving.")
        : QStringLiteral("编辑前先读取设备配置；读取期间暂停导航采集，可能需要几十秒。动力学与运动约束须匹配实际车辆；不支持的参数禁用，保存后重启。"));
    for (auto& field : settings_fields_)
    {
        const auto *descriptor = VaporView::epsilonParameterDescriptor(field.name);
        if (!descriptor) continue;
        const QString label = QString::fromStdString(is_english_ ? descriptor->label_en : descriptor->label_zh);
        field.label->setText(label);
        field.editor->setAccessibleName(label);
        QString readOnlyReason = is_english_ ? QStringLiteral("Read only") : QStringLiteral("只读");
        if (!descriptor->writable)
        {
            if (field.name == "COMM_BAUD1" || field.name == "COMM_STREAM_TYP1")
                readOnlyReason = is_english_ ? QStringLiteral("Read only: protects the Main control link") : QStringLiteral("只读：保护 Main 控制链路");
            else if (field.name == "GPIO_1_FUNCTION")
                readOnlyReason = is_english_ ? QStringLiteral("Read only: reserved for internal GNSS PPS") : QStringLiteral("只读：内部 GNSS PPS 占用");
            else if (field.name == "GNSS_L_ANTS_BASE_LINE")
                readOnlyReason = is_english_ ? QStringLiteral("Read only: calculated by the device") : QStringLiteral("只读：由设备计算");
            else if (field.name.rfind("USER_DEFINE_HOLD", 0) == 0)
                readOnlyReason = is_english_ ? QStringLiteral("Read only: origin encoding is not verified") : QStringLiteral("只读：原点编码尚未确认");
            else if (field.name == "COMM_STREAM_TYP5")
                readOnlyReason = is_english_ ? QStringLiteral("Read only: device CAN protocol is protected") : QStringLiteral("只读：保护设备 CAN 协议");
        }
        field.state->setText(field.unsupported
            ? (is_english_ ? QStringLiteral("Unsupported by this device / firmware") : QStringLiteral("当前设备或固件不支持"))
            : !field.read ? QString()
            : !field.value_supported ? (is_english_
                ? QStringLiteral("Device value %1 is outside the verified editing range; read only").arg(field.original, 0, 'g', 12)
                : QStringLiteral("设备实际值 %1 不在已确认的编辑范围内，仅供查看").arg(field.original, 0, 'g', 12))
            : !descriptor->writable ? readOnlyReason : QString());
        field.state->setVisible(!field.state->text().isEmpty());
        field.editor->setToolTip(!field.read && !field.unsupported
            ? (is_english_ ? QStringLiteral("Not read — current device value unknown") : QStringLiteral("未读取，设备当前值未知")) : field.state->text());
        if (auto *checkEditor = qobject_cast<QCheckBox *>(field.editor))
            checkEditor->setText(!field.read
                ? (is_english_ ? QStringLiteral("Not read") : QStringLiteral("未读取"))
                : !field.value_supported ? (is_english_ ? QStringLiteral("Unknown") : QStringLiteral("未知"))
                : (is_english_ ? QStringLiteral("Enabled") : QStringLiteral("启用")));
        else if (auto *comboEditor = qobject_cast<QComboBox *>(field.editor))
        {
            const QSignalBlocker blocker(comboEditor);
            for (int i = 0; i < comboEditor->count() && i < static_cast<int>(descriptor->options.size()); ++i)
                comboEditor->setItemText(i, QString::fromStdString(is_english_ ? descriptor->options[i].label_en : descriptor->options[i].label_zh));
            if (comboEditor->count() > static_cast<int>(descriptor->options.size()))
                comboEditor->setItemText(comboEditor->count() - 1, is_english_
                    ? QStringLiteral("Unknown (%1)").arg(field.original, 0, 'g', 12)
                    : QStringLiteral("未知选项（%1）").arg(field.original, 0, 'g', 12));
        }
    }
    if (!settings_read_button_) return;
    settings_read_button_->setText(is_english_ ? QStringLiteral("Read Device") : QStringLiteral("读取设备配置"));
    device_restart_button_->setText(is_english_ ? QStringLiteral("Restart Device") : QStringLiteral("重启设备"));
    settings_read_button_->setAccessibleName(settings_read_button_->text());
    device_restart_button_->setAccessibleName(device_restart_button_->text());
    device_restart_button_->setToolTip(is_english_ ? QStringLiteral("Restarts the device and interrupts navigation output") : QStringLiteral("重启设备，导航输出会中断"));
    if (pages_->currentIndex() != 0)
    {
        int readCount = 0;
        int editableCount = 0;
        int unsupportedCount = 0;
        int unreadCount = 0;
        for (const auto& field : settings_fields_)
        {
            if (field.group != currentSettingsGroup()) continue;
            const auto *descriptor = VaporView::epsilonParameterDescriptor(field.name);
            readCount += field.read ? 1 : 0;
            editableCount += field.read && field.value_supported && descriptor && descriptor->writable ? 1 : 0;
            unsupportedCount += field.unsupported ? 1 : 0;
            unreadCount += !field.read && !field.unsupported ? 1 : 0;
        }
        if (isDgnssPage())
        {
            readCount = editableCount = unsupportedCount = unreadCount = 0;
            for (const auto& field : dgnss_fields_)
            {
                readCount += field.read ? 1 : 0;
                editableCount += field.read && !field.unsupported && VaporView::epsilonDgnssDescriptor(field.name)->writable ? 1 : 0;
                unsupportedCount += field.unsupported ? 1 : 0;
                unreadCount += !field.read && !field.unsupported ? 1 : 0;
            }
        }
        const QString readStatus = readCount > 0
            ? (is_english_
                ? QStringLiteral("Read %1 parameters; %2 editable, %3 unsupported, %4 not read. See field details.")
                    .arg(readCount).arg(editableCount).arg(unsupportedCount).arg(unreadCount)
                : QStringLiteral("已读取 %1 项；%2 项可编辑，%3 项不支持，%4 项未读取。详见字段说明。")
                    .arg(readCount).arg(editableCount).arg(unsupportedCount).arg(unreadCount))
            : (is_english_ ? QStringLiteral("Read current device values to enable editing.") : QStringLiteral("读取设备当前值后才能编辑。"));
        save_button_->setText(is_english_ ? QStringLiteral("Save + Apply") : QStringLiteral("保存并应用"));
        save_button_->setToolTip(is_english_ ? QStringLiteral("Save only changed, successfully read device parameters") : QStringLiteral("仅保存已成功读取且已修改的设备参数"));
        if (!settings_status_custom_)
            settings_status_label_->setText(settings_pending_
                ? (is_english_ ? QStringLiteral("Device configuration in progress; navigation collection is paused. Please wait…") : QStringLiteral("正在处理设备配置，导航采集已暂停，请等待完成…"))
                : settings_saved_
                ? (settings_verified_ ? (is_english_ ? QStringLiteral("Saved ACK received; changed values readback confirmed. Restart required.") : QStringLiteral("已收到保存 ACK；已修改项回读确认一致。待重启设备。"))
                                      : (is_english_ ? QStringLiteral("Saved ACK received; changed values readback is not confirmed. Restart required.") : QStringLiteral("已收到保存 ACK；已修改项尚未回读确认。待重启设备。")))
                : readStatus);
    }
}

void EpsilonConfigPanel::setAvailable(bool available)
{
    is_available_ = available;
    updateSummaryTexts();
    setEnabled(available);
    updateSettingsControls();
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
    updateDeviceInfoTexts();
}

void EpsilonConfigPanel::updateDeviceInfoTexts()
{
    if (!device_info_label_) return;
    if (!live_epsilon_data_.device_info_valid)
    {
        device_info_label_->setText(is_english_ ? QStringLiteral("Version packet not received") : QStringLiteral("未收到版本报文"));
        device_info_label_->setToolTip(QString());
        return;
    }
    QStringList serial;
    for (uint32_t part : live_epsilon_data_.serial_number)
        serial.append(QStringLiteral("%1").arg(part, 8, 16, QLatin1Char('0')));
    const QString text = (is_english_ ? QStringLiteral("Hardware: %1 [%2]; Firmware: %3 [%4]; SN: %5") : QStringLiteral("硬件：%1 [%2]；固件：%3 [%4]；序列号：%5"))
        .arg(QString::fromStdString(live_epsilon_data_.hardware_name)).arg(live_epsilon_data_.hardware_version)
        .arg(QString::fromStdString(live_epsilon_data_.firmware_name)).arg(live_epsilon_data_.firmware_version)
        .arg(serial.join(QLatin1Char('-')));
    device_info_label_->setText(text);
    device_info_label_->setToolTip(text);
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
    arrangeSettingsFields(event && event->size().width() >= 760);
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
    const int liveColumns = twoColumns ? kLivePacketVisualColumnCount : 2;
    for (int column = 0; column < kLivePacketVisualColumnCount; ++column)
        live_packet_rate_grid_->setColumnStretch(column, column < liveColumns ? 1 : 0);
    for (int i = 0; i < live_packet_rate_fields_.size(); ++i)
    {
        QWidget *field = live_packet_rate_fields_.at(i);
        live_packet_rate_grid_->removeWidget(field);
        const int row = i / liveColumns;
        const int column = i % liveColumns;
        for (QObject *object : {static_cast<QObject *>(field),
                               static_cast<QObject *>(live_packet_rate_labels_.at(i)),
                               static_cast<QObject *>(live_packet_rate_values_.at(i))})
        {
            object->setProperty("epsilonLivePacketGridRow", row);
            object->setProperty("epsilonLivePacketGridColumn", column);
        }
        live_packet_rate_grid_->addWidget(field, row, column);
    }
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
        "QLabel[epsilonParameterLabel=\"true\"] { color: @vv-text; font-weight: 400; }"
        "QFrame[epsilonParameterCard=\"true\"] > QWidget#sectionTitleBar { border-bottom: 1px solid @vv-border; }"
        "QLabel[epsilonLivePacketRateLabel=\"true\"] { color: @vv-text-secondary; font-weight: 400; }"
        "QLabel[epsilonLivePacketRateValue=\"true\"] { color: @vv-text-strong; font-weight: 600; }"
        "QLabel[epsilonPacketGroupHeader=\"true\"] { color: @vv-text-strong; font-weight: 600; padding-top: 2px; }"
        "QLabel[epsilonSecondaryText=\"true\"] { color: @vv-text-secondary; font-weight: 400; }"
        "QPushButton[epsilonSecondaryAction=\"true\"] { background-color: @vv-surface-alt; border: 1px solid @vv-border; color: @vv-text; }"
        "QPushButton[epsilonSecondaryAction=\"true\"]:hover { background-color: @vv-primary-subtle; border-color: @vv-primary; color: @vv-primary; }"
        "QPushButton[epsilonSecondaryAction=\"true\"]:focus { border-color: @vv-focus; }"
        "QPushButton[epsilonSecondaryAction=\"true\"]:disabled { background-color: @vv-surface-alt; border-color: @vv-border; color: @vv-text-muted; }"
        "QFrame#epsilonSettingsTabs { background-color: @vv-primary-subtle; border: 1px solid @vv-border-strong; border-radius: 18px; }"
        "QFrame#epsilonSettingsTabTrack { background-color: @vv-primary; border: 1px solid %1; border-radius: 15px; }"
        "QFrame#epsilonSettingsTabTrack QPushButton { background-color: transparent; color: @vv-white; border: 1px solid transparent; border-radius: 13px; font-weight: 600; margin: 0; min-height: 0; padding: 0 10px; outline: none; }"
        "QFrame#epsilonSettingsTabTrack QPushButton:checked { background-color: transparent; color: @vv-primary; }"
        "QFrame#epsilonSettingsTabTrack QPushButton:!checked:hover { background-color: transparent; color: @vv-white; }"
        "QFrame#epsilonSettingsTabTrack QPushButton:pressed { background-color: transparent; }"
        "QFrame#epsilonSettingsTabTrack QPushButton:checked:pressed { background-color: transparent; }"
        "QLabel[epsilonSettingsError=\"true\"] { color: @vv-danger; }"
        "QPushButton#epsilonRecommendedConfigButton { min-height: 28px; max-height: 28px; padding-top: 0px; padding-bottom: 0px; }"
        "QWidget#epsilonActionsContainer { background-color: transparent; border: none; }"
        "QProgressBar#epsilonMaintenanceProgress { background-color: @vv-surface-alt; color: @vv-text; border: 1px solid @vv-border; border-radius: 4px; min-height: 18px; text-align: center; }"
        "QProgressBar#epsilonMaintenanceProgress::chunk { background-color: @vv-primary; border-radius: 3px; }"
        "QWidget#epsilonSummaryFields, QWidget#epsilonLivePacketRateGrid, QWidget#epsilonOutputTitleActions, QWidget#epsilonPacketGrid { background-color: transparent; border: none; }"
        "QComboBox[epsilonRtcmDevicePortControl=\"true\"] { background-color: @vv-surface; }")
        .arg(VaporView::appThemeColorName(VaporView::isDarkThemeEnabled()
            ? VaporView::AppThemeColor::BorderStrong : VaporView::AppThemeColor::White,
            VaporView::isDarkThemeEnabled()));
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
    updateSettingsTexts();
    updateSettingsControls();
}

} // namespace VaporView::Ground::Navigation
