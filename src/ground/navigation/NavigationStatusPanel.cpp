#include "ground/navigation/NavigationStatusPanel.h"

#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"

#include <QBoxLayout>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QResizeEvent>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace VaporView::Ground::Navigation
{
namespace
{
constexpr int kCardGap = 12;
constexpr int kCompactWidth = 700;

void prepareStyledBackground(QWidget *widget)
{
    if (!widget)
    {
        return;
    }
    widget->setAttribute(Qt::WA_StyledBackground, true);
    widget->setAutoFillBackground(true);
}

QLabel *createSectionTitle(QWidget *parent, const QString& objectName)
{
    auto *label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setProperty("navigationStatusSectionTitle", true);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return label;
}

bool usablePosition(const NavigationStatusSnapshot& snapshot)
{
    return snapshot.positionAvailable &&
        std::isfinite(snapshot.longitudeDeg) &&
        std::isfinite(snapshot.latitudeDeg) &&
        std::isfinite(snapshot.heightM) &&
        snapshot.longitudeDeg >= -180.0 && snapshot.longitudeDeg <= 180.0 &&
        snapshot.latitudeDeg >= -90.0 && snapshot.latitudeDeg <= 90.0;
}

bool usableSpeed(const NavigationStatusSnapshot& snapshot)
{
    return snapshot.speedAvailable &&
        std::isfinite(snapshot.speedMps) &&
        snapshot.speedMps >= 0.0;
}

bool usableAttitude(const NavigationStatusSnapshot& snapshot)
{
    return snapshot.attitudeAvailable &&
        std::isfinite(snapshot.rollDeg) &&
        std::isfinite(snapshot.pitchDeg) &&
        std::isfinite(snapshot.headingDeg);
}

} // namespace

NavigationStatusPanel::NavigationStatusPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("navigationStatusPanel"));
    prepareStyledBackground(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(kCardGap);

    QVBoxLayout *summaryLayout = nullptr;
    summary_card_ = createCard(
        this,
        QStringLiteral("navigationStatusSummaryCard"),
        QStringLiteral("navigationStatusSummaryTitle"),
        &summary_title_,
        &summaryLayout);
    auto *summaryGrid = new QGridLayout();
    summaryGrid->setContentsMargins(0, 0, 0, 0);
    summaryGrid->setHorizontalSpacing(24);
    summaryGrid->setVerticalSpacing(10);
    summaryGrid->setColumnStretch(1, 1);
    summaryGrid->setColumnStretch(3, 1);
    epsilon_status_ = addField(
        summaryGrid, 0, 0, summary_card_, QStringLiteral("navigationStatusEpsilonValue"));
    gnss_status_ = addField(
        summaryGrid, 0, 2, summary_card_, QStringLiteral("navigationStatusGnssValue"));
    ins_status_ = addField(
        summaryGrid, 1, 0, summary_card_, QStringLiteral("navigationStatusInsValue"));
    positioning_mode_ = addField(
        summaryGrid, 1, 2, summary_card_, QStringLiteral("navigationStatusFixValue"));
    data_freshness_ = addField(
        summaryGrid, 2, 0, summary_card_, QStringLiteral("navigationStatusFreshnessValue"));
    rtk_status_ = addField(
        summaryGrid, 2, 2, summary_card_, QStringLiteral("navigationStatusRtkServiceValue"));
    summaryLayout->addLayout(summaryGrid);
    rootLayout->addWidget(summary_card_);

    details_row_ = new QWidget(this);
    details_row_->setObjectName(QStringLiteral("navigationStatusDetailsRow"));
    prepareStyledBackground(details_row_);
    details_layout_ = new QBoxLayout(QBoxLayout::LeftToRight, details_row_);
    details_layout_->setObjectName(QStringLiteral("navigationStatusDetailsLayout"));
    details_layout_->setContentsMargins(0, 0, 0, 0);
    details_layout_->setSpacing(kCardGap);

    QVBoxLayout *positionLayout = nullptr;
    position_card_ = createCard(
        details_row_,
        QStringLiteral("navigationStatusPositionCard"),
        QStringLiteral("navigationStatusPositionTitle"),
        &position_title_,
        &positionLayout);
    auto *positionGrid = new QGridLayout();
    positionGrid->setContentsMargins(0, 0, 0, 0);
    positionGrid->setHorizontalSpacing(20);
    positionGrid->setVerticalSpacing(9);
    positionGrid->setColumnStretch(1, 1);
    longitude_ = addField(
        positionGrid, 0, 0, position_card_, QStringLiteral("navigationStatusLongitudeValue"));
    latitude_ = addField(
        positionGrid, 1, 0, position_card_, QStringLiteral("navigationStatusLatitudeValue"));
    height_ = addField(
        positionGrid, 2, 0, position_card_, QStringLiteral("navigationStatusHeightValue"));
    speed_ = addField(
        positionGrid, 3, 0, position_card_, QStringLiteral("navigationStatusSpeedValue"));
    positionLayout->addLayout(positionGrid);
    details_layout_->addWidget(position_card_, 1);

    QVBoxLayout *attitudeLayout = nullptr;
    attitude_card_ = createCard(
        details_row_,
        QStringLiteral("navigationStatusAttitudeCard"),
        QStringLiteral("navigationStatusAttitudeTitle"),
        &attitude_title_,
        &attitudeLayout);
    auto *attitudeGrid = new QGridLayout();
    attitudeGrid->setContentsMargins(0, 0, 0, 0);
    attitudeGrid->setHorizontalSpacing(20);
    attitudeGrid->setVerticalSpacing(9);
    attitudeGrid->setColumnStretch(1, 1);
    roll_ = addField(
        attitudeGrid, 0, 0, attitude_card_, QStringLiteral("navigationStatusRollValue"));
    pitch_ = addField(
        attitudeGrid, 1, 0, attitude_card_, QStringLiteral("navigationStatusPitchValue"));
    heading_ = addField(
        attitudeGrid, 2, 0, attitude_card_, QStringLiteral("navigationStatusHeadingValue"));
    attitudeLayout->addLayout(attitudeGrid);
    details_layout_->addWidget(attitude_card_, 1);
    rootLayout->addWidget(details_row_);

    quality_row_ = new QWidget(this);
    quality_row_->setObjectName(QStringLiteral("navigationStatusQualityRow"));
    prepareStyledBackground(quality_row_);
    quality_layout_ = new QBoxLayout(QBoxLayout::LeftToRight, quality_row_);
    quality_layout_->setObjectName(QStringLiteral("navigationStatusQualityLayout"));
    quality_layout_->setContentsMargins(0, 0, 0, 0);
    quality_layout_->setSpacing(kCardGap);

    QVBoxLayout *gnssLayout = nullptr;
    gnss_card_ = createCard(
        quality_row_,
        QStringLiteral("navigationStatusGnssCard"),
        QStringLiteral("navigationStatusGnssTitle"),
        &gnss_title_,
        &gnssLayout);
    auto *gnssGrid = new QGridLayout();
    gnssGrid->setContentsMargins(0, 0, 0, 0);
    gnssGrid->setHorizontalSpacing(20);
    gnssGrid->setVerticalSpacing(9);
    gnssGrid->setColumnStretch(1, 1);
    gnss_fix_ = addField(
        gnssGrid, 0, 0, gnss_card_, QStringLiteral("navigationStatusGnssFixValue"));
    satellites_ = addField(
        gnssGrid, 1, 0, gnss_card_, QStringLiteral("navigationStatusSatellitesValue"));
    horizontal_accuracy_ = addField(
        gnssGrid, 2, 0, gnss_card_, QStringLiteral("navigationStatusHorizontalAccuracyValue"));
    gnssLayout->addLayout(gnssGrid);
    quality_layout_->addWidget(gnss_card_, 1);

    QVBoxLayout *differentialLayout = nullptr;
    differential_card_ = createCard(
        quality_row_,
        QStringLiteral("navigationStatusDifferentialCard"),
        QStringLiteral("navigationStatusDifferentialTitle"),
        &differential_title_,
        &differentialLayout);
    auto *differentialGrid = new QGridLayout();
    differentialGrid->setContentsMargins(0, 0, 0, 0);
    differentialGrid->setHorizontalSpacing(20);
    differentialGrid->setVerticalSpacing(9);
    differentialGrid->setColumnStretch(1, 1);
    ntrip_status_ = addField(
        differentialGrid, 0, 0, differential_card_, QStringLiteral("navigationStatusNtripValue"));
    rtcm_status_ = addField(
        differentialGrid, 1, 0, differential_card_, QStringLiteral("navigationStatusRtcmValue"));
    differential_age_ = addField(
        differentialGrid, 2, 0, differential_card_, QStringLiteral("navigationStatusDifferentialAgeValue"));
    differentialLayout->addLayout(differentialGrid);
    quality_layout_->addWidget(differential_card_, 1);
    rootLayout->addWidget(quality_row_);

    empty_state_ = new QFrame(this);
    empty_state_->setObjectName(QStringLiteral("navigationStatusEmptyState"));
    empty_state_->setProperty("navigationStatusEmptyState", true);
    empty_state_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto *emptyStateLayout = new QVBoxLayout(empty_state_);
    emptyStateLayout->setContentsMargins(16, 14, 16, 14);
    emptyStateLayout->setSpacing(4);
    empty_state_title_ = new QLabel(empty_state_);
    empty_state_title_->setObjectName(QStringLiteral("navigationStatusEmptyStateTitle"));
    empty_state_title_->setProperty("navigationStatusEmptyStateTitle", true);
    empty_state_detail_ = new QLabel(empty_state_);
    empty_state_detail_->setObjectName(QStringLiteral("navigationStatusEmptyStateDetail"));
    empty_state_detail_->setProperty("navigationStatusEmptyStateDetail", true);
    empty_state_detail_->setWordWrap(true);
    emptyStateLayout->addWidget(empty_state_title_);
    emptyStateLayout->addWidget(empty_state_detail_);
    rootLayout->addWidget(empty_state_);
    rootLayout->addStretch(1);

    updateTexts();
    applyAppearance();
    setSnapshot(NavigationStatusSnapshot{});
}

void NavigationStatusPanel::setEnglish(bool english)
{
    is_english_ = english;
    updateTexts();
    setSnapshot(snapshot_);
}

void NavigationStatusPanel::setSnapshot(const NavigationStatusSnapshot& snapshot)
{
    snapshot_ = snapshot;
    const QLocale locale;
    const bool realtimeDataFresh = snapshot.epsilonDataFresh;
    const bool positionUsable = realtimeDataFresh && usablePosition(snapshot);
    const bool speedUsable = realtimeDataFresh && usableSpeed(snapshot);
    const bool attitudeUsable = realtimeDataFresh && usableAttitude(snapshot);
    const QString fixText = snapshot.gnssFixText.trimmed();
    const bool navigationDataUsable = realtimeDataFresh && snapshot.navigationDataAvailable;
    const bool fixUsable = navigationDataUsable && !fixText.isEmpty();
    const bool satellitesUsable = realtimeDataFresh && snapshot.gnssQualityAvailable &&
        snapshot.satelliteCount >= 0;
    const bool accuracyUsable = realtimeDataFresh && snapshot.gnssQualityAvailable &&
        std::isfinite(snapshot.horizontalAccuracyM) && snapshot.horizontalAccuracyM >= 0.0;
    const QString ntripText = snapshot.ntripStatusText.trimmed();
    const QString rtcmText = snapshot.rtcmStatusText.trimmed();
    const bool differentialAgeUsable = snapshot.differentialAvailable &&
        std::isfinite(snapshot.differentialAgeS) && snapshot.differentialAgeS >= 0.0;
    const bool differentialUsable = snapshot.differentialAvailable &&
        (!ntripText.isEmpty() || !rtcmText.isEmpty() || differentialAgeUsable);

    applyStatusLabel(
        epsilon_status_.value,
        (snapshot.epsilonOnline && realtimeDataFresh)
            ? (is_english_ ? QStringLiteral("● Online") : QStringLiteral("● 在线"))
            : (is_english_ ? QStringLiteral("○ Offline") : QStringLiteral("○ 离线")),
        (snapshot.epsilonOnline && realtimeDataFresh) ? QStringLiteral("healthy") : QStringLiteral("inactive"));
    applyStatusLabel(
        gnss_status_.value,
        navigationDataUsable
            ? (is_english_ ? QStringLiteral("● Available") : QStringLiteral("● 可用"))
            : unavailableText(),
        navigationDataUsable ? QStringLiteral("healthy") : QStringLiteral("inactive"));
    applyStatusLabel(
        ins_status_.value,
        attitudeUsable
            ? (is_english_ ? QStringLiteral("● Available") : QStringLiteral("● 可用"))
            : unavailableText(),
        attitudeUsable ? QStringLiteral("healthy") : QStringLiteral("inactive"));
    applyValueLabel(positioning_mode_.value, fixUsable ? fixText : unavailableText());

    QString freshnessText = unavailableText();
    if (snapshot.epsilonDataFresh)
    {
        freshnessText = is_english_ ? QStringLiteral("● Fresh") : QStringLiteral("● 新鲜");
        if (snapshot.epsilonDataAgeMs >= 0)
        {
            freshnessText += QStringLiteral(" (%1 ms)").arg(snapshot.epsilonDataAgeMs);
        }
    }
    applyStatusLabel(
        data_freshness_.value,
        freshnessText,
        snapshot.epsilonDataFresh ? QStringLiteral("healthy") : QStringLiteral("inactive"));
    applyStatusLabel(
        rtk_status_.value,
        snapshot.rtkServiceRunning
            ? (is_english_ ? QStringLiteral("● Running") : QStringLiteral("● 运行中"))
            : (is_english_ ? QStringLiteral("○ Stopped") : QStringLiteral("○ 未启动")),
        snapshot.rtkServiceRunning ? QStringLiteral("active") : QStringLiteral("inactive"));

    applyValueLabel(longitude_.value, positionUsable
        ? QStringLiteral("%1°").arg(locale.toString(snapshot.longitudeDeg, 'f', 8))
        : unavailableText());
    applyValueLabel(latitude_.value, positionUsable
        ? QStringLiteral("%1°").arg(locale.toString(snapshot.latitudeDeg, 'f', 8))
        : unavailableText());
    applyValueLabel(height_.value, positionUsable
        ? QStringLiteral("%1 m").arg(locale.toString(snapshot.heightM, 'f', 3))
        : unavailableText());
    applyValueLabel(speed_.value, speedUsable
        ? QStringLiteral("%1 m/s").arg(locale.toString(snapshot.speedMps, 'f', 3))
        : unavailableText());

    applyValueLabel(roll_.value, attitudeUsable
        ? QStringLiteral("%1°").arg(locale.toString(snapshot.rollDeg, 'f', 2))
        : unavailableText());
    applyValueLabel(pitch_.value, attitudeUsable
        ? QStringLiteral("%1°").arg(locale.toString(snapshot.pitchDeg, 'f', 2))
        : unavailableText());
    applyValueLabel(heading_.value, attitudeUsable
        ? QStringLiteral("%1°").arg(locale.toString(snapshot.headingDeg, 'f', 2))
        : unavailableText());

    applyValueLabel(gnss_fix_.value, fixUsable ? fixText : unavailableText());
    applyValueLabel(satellites_.value, satellitesUsable
        ? locale.toString(snapshot.satelliteCount)
        : unavailableText());
    applyValueLabel(horizontal_accuracy_.value, accuracyUsable
        ? QStringLiteral("%1 m").arg(locale.toString(snapshot.horizontalAccuracyM, 'f', 3))
        : unavailableText());

    applyValueLabel(ntrip_status_.value,
                    snapshot.differentialAvailable && !ntripText.isEmpty()
                        ? ntripText : unavailableText());
    applyValueLabel(rtcm_status_.value,
                    snapshot.differentialAvailable && !rtcmText.isEmpty()
                        ? rtcmText : unavailableText());
    applyValueLabel(differential_age_.value, differentialAgeUsable
        ? QStringLiteral("%1 s").arg(locale.toString(snapshot.differentialAgeS, 'f', 1))
        : unavailableText());

    const bool anyFieldDataUsable = positionUsable || speedUsable || attitudeUsable ||
        fixUsable || satellitesUsable || accuracyUsable || differentialUsable;
    empty_state_->setVisible(!anyFieldDataUsable);
    scheduleShadowUpdate();
}

const NavigationStatusSnapshot& NavigationStatusPanel::snapshot() const
{
    return snapshot_;
}

void NavigationStatusPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event &&
        (event->type() == QEvent::ApplicationPaletteChange ||
         event->type() == QEvent::PaletteChange))
    {
        QTimer::singleShot(0, this, [this]() { applyAppearance(); });
    }
}

void NavigationStatusPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

QFrame *NavigationStatusPanel::createCard(
    QWidget *parent,
    const QString& objectName,
    const QString& titleObjectName,
    QLabel **titleOut,
    QVBoxLayout **layoutOut)
{
    auto *card = new QFrame(parent);
    card->setObjectName(objectName);
    card->setProperty("navigationStatusCard", true);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    VaporView::configureTopLevelCard(card);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(11);
    QLabel *title = createSectionTitle(card, titleObjectName);
    layout->addWidget(title);
    if (titleOut)
    {
        *titleOut = title;
    }
    if (layoutOut)
    {
        *layoutOut = layout;
    }
    return card;
}

NavigationStatusPanel::FieldWidgets NavigationStatusPanel::addField(
    QGridLayout *layout,
    int row,
    int column,
    QWidget *parent,
    const QString& valueObjectName)
{
    FieldWidgets field;
    field.name = new QLabel(parent);
    field.name->setProperty("navigationStatusFieldName", true);
    field.name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    field.name->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(field.name, row, column, Qt::AlignLeft | Qt::AlignVCenter);

    field.value = new QLabel(QStringLiteral("--"), parent);
    field.value->setObjectName(valueObjectName);
    field.value->setProperty("navigationStatusValue", true);
    field.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    field.value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(field.value, row, column + 1, Qt::AlignRight | Qt::AlignVCenter);
    return field;
}

void NavigationStatusPanel::updateTexts()
{
    const QString pageName = is_english_
        ? QStringLiteral("Combination Navigation Status")
        : QStringLiteral("组合导航状态");
    setAccessibleName(pageName);

    summary_title_->setText(is_english_ ? QStringLiteral("Status Overview") : QStringLiteral("状态总览"));
    position_title_->setText(is_english_ ? QStringLiteral("Position") : QStringLiteral("位置"));
    attitude_title_->setText(is_english_ ? QStringLiteral("Attitude") : QStringLiteral("姿态"));
    gnss_title_->setText(is_english_ ? QStringLiteral("GNSS Quality") : QStringLiteral("GNSS 质量"));
    differential_title_->setText(is_english_ ? QStringLiteral("Differential Data") : QStringLiteral("差分数据"));
    empty_state_title_->setText(is_english_
        ? QStringLiteral("No confirmed field-level navigation data")
        : QStringLiteral("暂无可确认的字段级导航数据"));
    empty_state_detail_->setText(is_english_
        ? QStringLiteral("Position, attitude, and GNSS quality stay at -- until their validity and freshness are confirmed.")
        : QStringLiteral("字段有效性与新鲜度确认前，位置、姿态与 GNSS 质量均保持为 --。"));

    epsilon_status_.name->setText(QStringLiteral("EPSILON"));
    gnss_status_.name->setText(QStringLiteral("GNSS"));
    ins_status_.name->setText(QStringLiteral("INS"));
    positioning_mode_.name->setText(is_english_ ? QStringLiteral("Fix") : QStringLiteral("定位"));
    data_freshness_.name->setText(is_english_ ? QStringLiteral("Data") : QStringLiteral("数据"));
    rtk_status_.name->setText(is_english_ ? QStringLiteral("RTK Service") : QStringLiteral("RTK 服务"));
    longitude_.name->setText(is_english_ ? QStringLiteral("Longitude") : QStringLiteral("经度"));
    latitude_.name->setText(is_english_ ? QStringLiteral("Latitude") : QStringLiteral("纬度"));
    height_.name->setText(is_english_ ? QStringLiteral("Height") : QStringLiteral("高度"));
    speed_.name->setText(is_english_ ? QStringLiteral("Speed") : QStringLiteral("速度"));
    roll_.name->setText(QStringLiteral("Roll"));
    pitch_.name->setText(QStringLiteral("Pitch"));
    heading_.name->setText(QStringLiteral("Heading"));
    gnss_fix_.name->setText(is_english_ ? QStringLiteral("Fix") : QStringLiteral("定位类型"));
    satellites_.name->setText(is_english_ ? QStringLiteral("Satellites") : QStringLiteral("卫星数"));
    horizontal_accuracy_.name->setText(is_english_ ? QStringLiteral("H. Accuracy") : QStringLiteral("水平精度"));
    ntrip_status_.name->setText(QStringLiteral("NTRIP"));
    rtcm_status_.name->setText(QStringLiteral("RTCM"));
    differential_age_.name->setText(is_english_ ? QStringLiteral("Age") : QStringLiteral("差分龄期"));

    const std::array<FieldWidgets *, 19> fields{{
        &epsilon_status_, &gnss_status_, &ins_status_, &positioning_mode_,
        &data_freshness_, &rtk_status_, &longitude_, &latitude_, &height_, &speed_,
        &roll_, &pitch_, &heading_, &gnss_fix_, &satellites_, &horizontal_accuracy_,
        &ntrip_status_, &rtcm_status_, &differential_age_,
    }};
    for (FieldWidgets *field : fields)
    {
        const QString accessibleName = QStringLiteral("%1 %2").arg(pageName, field->name->text());
        field->name->setAccessibleName(accessibleName);
        field->value->setAccessibleName(accessibleName);
    }
}

void NavigationStatusPanel::applyAppearance()
{
    const QString style = QStringLiteral(
        "QWidget#navigationStatusPanel, QWidget#navigationStatusDetailsRow, "
        "QWidget#navigationStatusQualityRow { background-color: @vv-surface; border: none; }"
        "QFrame[navigationStatusCard=\"true\"] { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 12px; }"
        "QLabel[navigationStatusSectionTitle=\"true\"] { color: @vv-text-strong; font-weight: 600; }"
        "QLabel[navigationStatusFieldName=\"true\"] { color: @vv-text-secondary; font-weight: 500; }"
        "QLabel[navigationStatusValue=\"true\"] { color: @vv-text-strong; font-weight: 600; }"
        "QFrame[navigationStatusEmptyState=\"true\"] { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
        "QLabel[navigationStatusEmptyStateTitle=\"true\"] { color: @vv-text-strong; font-weight: 600; }"
        "QLabel[navigationStatusEmptyStateDetail=\"true\"] { color: @vv-text-muted; font-weight: 400; }"
        "QLabel[navigationStatusKind=\"healthy\"] { color: @vv-hd-ok; }"
        "QLabel[navigationStatusKind=\"active\"] { color: @vv-primary; }"
        "QLabel[navigationStatusKind=\"inactive\"] { color: @vv-text-muted; }");
    const QString resolvedStyle =
        VaporView::applyAppThemeTokens(style, VaporView::isDarkThemeEnabled());
    if (styleSheet() != resolvedStyle)
    {
        setStyleSheet(resolvedStyle);
    }
    scheduleShadowUpdate();
}

void NavigationStatusPanel::applyStatusLabel(
    QLabel *label,
    const QString& text,
    const QString& kind)
{
    if (!label)
    {
        return;
    }
    const bool propertyChanged = label->property("navigationStatusKind").toString() != kind;
    label->setText(text);
    if (!propertyChanged)
    {
        return;
    }
    label->setProperty("navigationStatusKind", kind);
    if (label->style())
    {
        label->style()->unpolish(label);
        label->style()->polish(label);
    }
    label->update();
}

void NavigationStatusPanel::applyValueLabel(QLabel *label, const QString& text)
{
    applyStatusLabel(label, text, QStringLiteral("normal"));
}

void NavigationStatusPanel::updateResponsiveLayout()
{
    const QBoxLayout::Direction direction =
        width() < kCompactWidth ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
    if (details_layout_->direction() != direction)
    {
        details_layout_->setDirection(direction);
    }
    if (quality_layout_->direction() != direction)
    {
        quality_layout_->setDirection(direction);
    }
}

void NavigationStatusPanel::scheduleShadowUpdate()
{
    QTimer::singleShot(0, this, [this]() {
        VaporView::updateTopLevelCardShadows(this);
    });
}

QString NavigationStatusPanel::unavailableText() const
{
    return QStringLiteral("--");
}

} // namespace VaporView::Ground::Navigation
