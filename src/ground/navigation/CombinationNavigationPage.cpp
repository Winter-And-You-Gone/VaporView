#include "ground/navigation/CombinationNavigationPage.h"

#include "ground/navigation/EpsilonConfigPanel.h"
#include "shared/theme/AppTheme.h"

#include <QButtonGroup>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <array>
#include <utility>

namespace VaporView::Ground::Navigation
{
namespace
{
constexpr int kPageHorizontalInset = 18;
constexpr int kPageVerticalInset = 4;
constexpr int kSectionGap = 12;

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
    label->setProperty("combinationNavigationSectionTitle", true);
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return label;
}

QFrame *createCard(QWidget *parent, const QString& objectName, QVBoxLayout **layoutOut)
{
    auto *card = new QFrame(parent);
    card->setObjectName(objectName);
    card->setProperty("combinationNavigationCard", true);
    prepareStyledBackground(card);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(12);
    if (layoutOut)
    {
        *layoutOut = layout;
    }
    return card;
}

}

CombinationNavigationPage::CombinationNavigationPage(QWidget *differentialPage, QWidget *parent)
    : QWidget(parent)
    , differential_page_(differentialPage)
{
    Q_ASSERT(differential_page_ != nullptr);
    setObjectName(QStringLiteral("combinationNavigationPage"));
    prepareStyledBackground(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *navigationRow = new QWidget(this);
    navigationRow->setObjectName(QStringLiteral("combinationNavigationNavigationRow"));
    prepareStyledBackground(navigationRow);
    auto *navigationRowLayout = new QHBoxLayout(navigationRow);
    navigationRowLayout->setContentsMargins(kPageHorizontalInset,
                                            kPageVerticalInset,
                                            kPageHorizontalInset,
                                            kPageVerticalInset);
    navigationRowLayout->setSpacing(0);

    auto *navigationBar = new QFrame(navigationRow);
    navigationBar->setObjectName(QStringLiteral("combinationNavigationNavigationBar"));
    prepareStyledBackground(navigationBar);
    auto *navigationLayout = new QHBoxLayout(navigationBar);
    navigationLayout->setContentsMargins(2, 2, 2, 2);
    navigationLayout->setSpacing(2);

    section_group_ = new QButtonGroup(this);
    section_group_->setExclusive(true);
    auto createSectionButton = [this, navigationBar, navigationLayout](
                                   const QString& objectName,
                                   Section section) {
        auto *button = new QPushButton(navigationBar);
        button->setObjectName(objectName);
        button->setCheckable(true);
        button->setAutoDefault(false);
        button->setDefault(false);
        button->setFocusPolicy(Qt::TabFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setMinimumWidth(92);
        button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        section_group_->addButton(button, static_cast<int>(section));
        navigationLayout->addWidget(button);
        return button;
    };

    status_button_ = createSectionButton(
        QStringLiteral("combinationNavigationStatusButton"), Section::Status);
    epsilon_button_ = createSectionButton(
        QStringLiteral("combinationNavigationEpsilonButton"), Section::Epsilon);
    differential_button_ = createSectionButton(
        QStringLiteral("combinationNavigationDifferentialButton"), Section::Differential);
    navigationRowLayout->addWidget(navigationBar, 0, Qt::AlignLeft | Qt::AlignVCenter);
    navigationRowLayout->addStretch(1);
    rootLayout->addWidget(navigationRow, 0);

    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("combinationNavigationStack"));
    prepareStyledBackground(stack_);
    stack_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    status_page_ = createStatusPage();
    epsilon_page_ = new QWidget(stack_);
    epsilon_page_->setObjectName(QStringLiteral("combinationNavigationEpsilonPage"));
    prepareStyledBackground(epsilon_page_);
    auto *epsilonPageLayout = new QVBoxLayout(epsilon_page_);
    epsilonPageLayout->setContentsMargins(0, 0, 0, 0);
    epsilonPageLayout->setSpacing(0);
    auto *epsilonScrollArea = new QScrollArea(epsilon_page_);
    epsilonScrollArea->setObjectName(QStringLiteral("epsilonConfigScrollArea"));
    epsilonScrollArea->setWidgetResizable(true);
    epsilonScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    epsilonScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    epsilonScrollArea->setFrameShape(QFrame::NoFrame);
    epsilonScrollArea->setMinimumWidth(0);
    epsilonScrollArea->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto *epsilonContent = new QWidget(epsilonScrollArea);
    prepareStyledBackground(epsilonContent);
    auto *epsilonContentLayout = new QVBoxLayout(epsilonContent);
    epsilonContentLayout->setContentsMargins(kPageHorizontalInset,
                                             kPageVerticalInset,
                                             kPageHorizontalInset,
                                             kPageVerticalInset);
    epsilonContentLayout->setSpacing(kSectionGap);
    epsilon_config_panel_ = new EpsilonConfigPanel(epsilonContent);
    epsilonContentLayout->addWidget(epsilon_config_panel_, 0);
    epsilonContentLayout->addStretch(1);
    epsilonScrollArea->setWidget(epsilonContent);
    epsilonPageLayout->addWidget(epsilonScrollArea, 1);
    stack_->addWidget(status_page_);
    stack_->addWidget(epsilon_page_);
    stack_->addWidget(differential_page_);
    rootLayout->addWidget(stack_, 1);

    connect(section_group_, &QButtonGroup::idClicked, this, [this](int id) {
        setCurrentSection(static_cast<Section>(id));
    });

    QWidget::setTabOrder(status_button_, epsilon_button_);
    QWidget::setTabOrder(epsilon_button_, differential_button_);

    status_refresh_timer_ = new QTimer(this);
    status_refresh_timer_->setInterval(1000);
    status_refresh_timer_->setTimerType(Qt::CoarseTimer);
    connect(status_refresh_timer_, &QTimer::timeout, this, &CombinationNavigationPage::refreshStatus);
    status_refresh_timer_->start();

    setCurrentSection(Section::Status);
    updateTexts();
    applyAppearance();
    setStatusSnapshot(StatusSnapshot{});
}

CombinationNavigationPage::Section CombinationNavigationPage::currentSection() const
{
    return stack_ ? static_cast<Section>(stack_->currentIndex()) : Section::Status;
}

QWidget *CombinationNavigationPage::differentialPage() const
{
    return differential_page_.data();
}

EpsilonConfigPanel *CombinationNavigationPage::epsilonConfigPanel() const
{
    return epsilon_config_panel_;
}

void CombinationNavigationPage::setCurrentSection(Section section)
{
    const int index = static_cast<int>(section);
    if (!stack_ || index < 0 || index >= stack_->count())
    {
        return;
    }

    const bool changed = stack_->currentIndex() != index;
    stack_->setCurrentIndex(index);
    if (QPushButton *button = qobject_cast<QPushButton *>(section_group_->button(index)))
    {
        const QSignalBlocker blocker(button);
        button->setChecked(true);
    }
    if (section == Section::Status || section == Section::Epsilon)
    {
        refreshStatus();
    }
    if (changed)
    {
        emit currentSectionChanged(section);
    }
}

void CombinationNavigationPage::showStatusPage()
{
    setCurrentSection(Section::Status);
}

void CombinationNavigationPage::showDifferentialPage()
{
    setCurrentSection(Section::Differential);
}

void CombinationNavigationPage::setEnglish(bool english)
{
    if (is_english_ == english)
    {
        updateTexts();
        setStatusSnapshot(status_snapshot_);
        return;
    }
    is_english_ = english;
    updateTexts();
    setStatusSnapshot(status_snapshot_);
}

void CombinationNavigationPage::setStatusProvider(StatusProvider provider)
{
    status_provider_ = std::move(provider);
}

void CombinationNavigationPage::setStatusSnapshot(const StatusSnapshot& snapshot)
{
    status_snapshot_ = snapshot;

    applyStatusLabel(
        epsilon_status_.value,
        snapshot.epsilonOnline
            ? (is_english_ ? QStringLiteral("● Online") : QStringLiteral("● 在线"))
            : (is_english_ ? QStringLiteral("○ Offline") : QStringLiteral("○ 离线")),
        snapshot.epsilonOnline ? QStringLiteral("healthy") : QStringLiteral("inactive"));
    applyStatusLabel(
        gnss_status_.value,
        snapshot.navigationDataAvailable
            ? (is_english_ ? QStringLiteral("● Data available") : QStringLiteral("● 有数据"))
            : (is_english_ ? QStringLiteral("○ No data") : QStringLiteral("○ 暂无数据")),
        snapshot.navigationDataAvailable ? QStringLiteral("healthy") : QStringLiteral("inactive"));
    positioning_mode_.value->setText(
        snapshot.navigationDataAvailable && !snapshot.gnssFixText.trimmed().isEmpty()
            ? snapshot.gnssFixText.trimmed()
            : unavailableText());
    applyStatusLabel(
        rtk_status_.value,
        snapshot.rtkServiceRunning
            ? (is_english_ ? QStringLiteral("● Running") : QStringLiteral("● 运行中"))
            : (is_english_ ? QStringLiteral("○ Stopped") : QStringLiteral("○ 未启动")),
        snapshot.rtkServiceRunning ? QStringLiteral("active") : QStringLiteral("inactive"));
    applyStatusLabel(ntrip_status_.value, unavailableText(), QStringLiteral("inactive"));
    applyStatusLabel(rtcm_status_.value, unavailableText(), QStringLiteral("inactive"));

    const QLocale locale;
    longitude_.value->setText(snapshot.positionAvailable
        ? QStringLiteral("%1°").arg(locale.toString(snapshot.longitudeDeg, 'f', 8))
        : unavailableText());
    latitude_.value->setText(snapshot.positionAvailable
        ? QStringLiteral("%1°").arg(locale.toString(snapshot.latitudeDeg, 'f', 8))
        : unavailableText());
    height_.value->setText(snapshot.positionAvailable
        ? QStringLiteral("%1 m").arg(locale.toString(snapshot.heightM, 'f', 3))
        : unavailableText());
    roll_.value->setText(snapshot.attitudeAvailable
        ? QStringLiteral("%1°").arg(locale.toString(snapshot.rollDeg, 'f', 2))
        : unavailableText());
    pitch_.value->setText(snapshot.attitudeAvailable
        ? QStringLiteral("%1°").arg(locale.toString(snapshot.pitchDeg, 'f', 2))
        : unavailableText());
    heading_.value->setText(snapshot.attitudeAvailable
        ? QStringLiteral("%1°").arg(locale.toString(snapshot.headingDeg, 'f', 2))
        : unavailableText());
}

void CombinationNavigationPage::refreshStatus()
{
    if (status_provider_)
    {
        setStatusSnapshot(status_provider_());
    }
}

void CombinationNavigationPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (!event)
    {
        return;
    }
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::PaletteChange)
    {
        QTimer::singleShot(0, this, [this]() { applyAppearance(); });
    }
}

QWidget *CombinationNavigationPage::createStatusPage()
{
    auto *page = new QWidget(stack_);
    page->setObjectName(QStringLiteral("combinationNavigationStatusPage"));
    prepareStyledBackground(page);
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(kPageHorizontalInset,
                                   kPageVerticalInset,
                                   kPageHorizontalInset,
                                   kPageVerticalInset);
    pageLayout->setSpacing(kSectionGap);

    QVBoxLayout *navigationCardLayout = nullptr;
    QFrame *navigationCard = createCard(
        page, QStringLiteral("combinationNavigationStatusCard"), &navigationCardLayout);
    navigation_status_title_ = createSectionTitle(
        navigationCard, QStringLiteral("combinationNavigationStatusTitle"));
    navigationCardLayout->addWidget(navigation_status_title_);
    auto *navigationGrid = new QGridLayout();
    navigationGrid->setContentsMargins(0, 0, 0, 0);
    navigationGrid->setHorizontalSpacing(28);
    navigationGrid->setVerticalSpacing(10);
    navigationGrid->setColumnStretch(0, 0);
    navigationGrid->setColumnStretch(1, 1);
    epsilon_status_ = addField(navigationGrid, 0, navigationCard, QStringLiteral("combinationNavigationEpsilonStatusValue"));
    gnss_status_ = addField(navigationGrid, 1, navigationCard, QStringLiteral("combinationNavigationGnssStatusValue"));
    positioning_mode_ = addField(navigationGrid, 2, navigationCard, QStringLiteral("combinationNavigationPositioningModeValue"));
    rtk_status_ = addField(navigationGrid, 3, navigationCard, QStringLiteral("combinationNavigationRtkServiceStatusValue"));
    ntrip_status_ = addField(navigationGrid, 4, navigationCard, QStringLiteral("combinationNavigationNtripStatusValue"));
    rtcm_status_ = addField(navigationGrid, 5, navigationCard, QStringLiteral("combinationNavigationRtcmStatusValue"));
    navigationCardLayout->addLayout(navigationGrid);
    pageLayout->addWidget(navigationCard, 0);

    auto *detailsRow = new QWidget(page);
    detailsRow->setObjectName(QStringLiteral("combinationNavigationDetailsRow"));
    prepareStyledBackground(detailsRow);
    auto *detailsLayout = new QHBoxLayout(detailsRow);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(kSectionGap);

    QVBoxLayout *positionCardLayout = nullptr;
    QFrame *positionCard = createCard(
        detailsRow, QStringLiteral("combinationNavigationPositionCard"), &positionCardLayout);
    position_title_ = createSectionTitle(
        positionCard, QStringLiteral("combinationNavigationPositionTitle"));
    positionCardLayout->addWidget(position_title_);
    auto *positionGrid = new QGridLayout();
    positionGrid->setContentsMargins(0, 0, 0, 0);
    positionGrid->setHorizontalSpacing(24);
    positionGrid->setVerticalSpacing(10);
    positionGrid->setColumnStretch(1, 1);
    longitude_ = addField(positionGrid, 0, positionCard, QStringLiteral("combinationNavigationLongitudeValue"));
    latitude_ = addField(positionGrid, 1, positionCard, QStringLiteral("combinationNavigationLatitudeValue"));
    height_ = addField(positionGrid, 2, positionCard, QStringLiteral("combinationNavigationHeightValue"));
    positionCardLayout->addLayout(positionGrid);
    detailsLayout->addWidget(positionCard, 1);

    QVBoxLayout *attitudeCardLayout = nullptr;
    QFrame *attitudeCard = createCard(
        detailsRow, QStringLiteral("combinationNavigationAttitudeCard"), &attitudeCardLayout);
    attitude_title_ = createSectionTitle(
        attitudeCard, QStringLiteral("combinationNavigationAttitudeTitle"));
    attitudeCardLayout->addWidget(attitude_title_);
    auto *attitudeGrid = new QGridLayout();
    attitudeGrid->setContentsMargins(0, 0, 0, 0);
    attitudeGrid->setHorizontalSpacing(24);
    attitudeGrid->setVerticalSpacing(10);
    attitudeGrid->setColumnStretch(1, 1);
    roll_ = addField(attitudeGrid, 0, attitudeCard, QStringLiteral("combinationNavigationRollValue"));
    pitch_ = addField(attitudeGrid, 1, attitudeCard, QStringLiteral("combinationNavigationPitchValue"));
    heading_ = addField(attitudeGrid, 2, attitudeCard, QStringLiteral("combinationNavigationHeadingValue"));
    attitudeCardLayout->addLayout(attitudeGrid);
    detailsLayout->addWidget(attitudeCard, 1);

    pageLayout->addWidget(detailsRow, 0);
    pageLayout->addStretch(1);
    return page;
}

CombinationNavigationPage::FieldWidgets CombinationNavigationPage::addField(
    QGridLayout *layout,
    int row,
    QWidget *parent,
    const QString& valueObjectName)
{
    FieldWidgets field;
    field.name = new QLabel(parent);
    field.name->setObjectName(QStringLiteral("combinationNavigationFieldName"));
    field.name->setProperty("combinationNavigationFieldName", true);
    field.name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    field.name->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(field.name, row, 0, Qt::AlignLeft | Qt::AlignVCenter);

    field.value = new QLabel(QStringLiteral("--"), parent);
    field.value->setObjectName(valueObjectName);
    field.value->setProperty("combinationNavigationValue", true);
    field.value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    field.value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(field.value, row, 1, Qt::AlignRight | Qt::AlignVCenter);
    return field;
}

void CombinationNavigationPage::updateTexts()
{
    const QString pageName = is_english_
        ? QStringLiteral("Combination Navigation")
        : QStringLiteral("组合导航");
    setAccessibleName(pageName);

    const std::array<std::pair<QPushButton *, QString>, 3> sectionButtons{{
        {status_button_, is_english_ ? QStringLiteral("Status") : QStringLiteral("状态")},
        {epsilon_button_, QStringLiteral("EPSILON")},
        {differential_button_, is_english_ ? QStringLiteral("Differential Positioning")
                                           : QStringLiteral("差分定位")},
    }};
    for (const auto& [button, text] : sectionButtons)
    {
        button->setText(text);
        button->setToolTip(text);
        button->setAccessibleName(text);
    }

    navigation_status_title_->setText(is_english_ ? QStringLiteral("Navigation Status")
                                                   : QStringLiteral("导航状态"));
    position_title_->setText(is_english_ ? QStringLiteral("Position") : QStringLiteral("位置"));
    attitude_title_->setText(is_english_ ? QStringLiteral("Attitude") : QStringLiteral("姿态"));
    if (epsilon_config_panel_)
    {
        epsilon_config_panel_->setEnglish(is_english_);
    }

    epsilon_status_.name->setText(QStringLiteral("EPSILON"));
    gnss_status_.name->setText(QStringLiteral("GNSS"));
    positioning_mode_.name->setText(is_english_ ? QStringLiteral("Positioning Mode")
                                                : QStringLiteral("定位模式"));
    rtk_status_.name->setText(is_english_ ? QStringLiteral("RTK Service")
                                          : QStringLiteral("RTK 服务"));
    ntrip_status_.name->setText(QStringLiteral("NTRIP"));
    rtcm_status_.name->setText(QStringLiteral("RTCM"));
    longitude_.name->setText(is_english_ ? QStringLiteral("Longitude") : QStringLiteral("经度"));
    latitude_.name->setText(is_english_ ? QStringLiteral("Latitude") : QStringLiteral("纬度"));
    height_.name->setText(is_english_ ? QStringLiteral("Height") : QStringLiteral("高度"));
    roll_.name->setText(QStringLiteral("Roll"));
    pitch_.name->setText(QStringLiteral("Pitch"));
    heading_.name->setText(QStringLiteral("Heading"));

    const std::array<FieldWidgets *, 12> fields{{
        &epsilon_status_, &gnss_status_, &positioning_mode_, &rtk_status_, &ntrip_status_, &rtcm_status_,
        &longitude_, &latitude_, &height_, &roll_, &pitch_, &heading_,
    }};
    for (FieldWidgets *field : fields)
    {
        const QString accessibleName = QStringLiteral("%1 %2").arg(pageName, field->name->text());
        field->name->setAccessibleName(accessibleName);
        field->value->setAccessibleName(accessibleName);
    }
}

void CombinationNavigationPage::applyAppearance()
{
    const QString style = QStringLiteral(
        "QWidget#combinationNavigationPage, "
        "QWidget#combinationNavigationNavigationRow, "
        "QWidget#combinationNavigationStatusPage, "
        "QWidget#combinationNavigationEpsilonPage, "
        "QWidget#combinationNavigationDetailsRow, "
        "QStackedWidget#combinationNavigationStack { background-color: @vv-surface; border: none; }"
        "QFrame#combinationNavigationNavigationBar { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
        "QFrame[combinationNavigationCard=\"true\"] { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 8px; }"
        "QPushButton#combinationNavigationStatusButton, "
        "QPushButton#combinationNavigationEpsilonButton, "
        "QPushButton#combinationNavigationDifferentialButton { background-color: transparent; border: 1px solid transparent; border-radius: 6px; color: @vv-text; font-weight: 500; padding: 6px 12px; outline: none; }"
        "QPushButton#combinationNavigationStatusButton:checked, "
        "QPushButton#combinationNavigationEpsilonButton:checked, "
        "QPushButton#combinationNavigationDifferentialButton:checked { background-color: @vv-surface; color: @vv-primary; font-weight: 600; }"
        "QPushButton#combinationNavigationStatusButton:!checked:hover, "
        "QPushButton#combinationNavigationEpsilonButton:!checked:hover, "
        "QPushButton#combinationNavigationDifferentialButton:!checked:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "QPushButton#combinationNavigationStatusButton:pressed, "
        "QPushButton#combinationNavigationEpsilonButton:pressed, "
        "QPushButton#combinationNavigationDifferentialButton:pressed { background-color: @vv-primary-subtle-pressed; }"
        "QPushButton#combinationNavigationStatusButton:focus, "
        "QPushButton#combinationNavigationEpsilonButton:focus, "
        "QPushButton#combinationNavigationDifferentialButton:focus { border-color: @vv-focus; }"
        "QLabel[combinationNavigationSectionTitle=\"true\"] { color: @vv-text-strong; font-weight: 600; }"
        "QLabel[combinationNavigationFieldName=\"true\"] { color: @vv-text-secondary; font-weight: 500; }"
        "QLabel[combinationNavigationValue=\"true\"] { color: @vv-text-strong; font-weight: 600; }"
        "QLabel[combinationNavigationStatusKind=\"healthy\"] { color: @vv-success; }"
        "QLabel[combinationNavigationStatusKind=\"active\"] { color: @vv-primary; }"
        "QLabel[combinationNavigationStatusKind=\"inactive\"] { color: @vv-text-muted; }");
    const QString resolvedStyle =
        VaporView::applyAppThemeTokens(style, VaporView::isDarkThemeEnabled());
    if (styleSheet() != resolvedStyle)
    {
        setStyleSheet(resolvedStyle);
    }
}

void CombinationNavigationPage::applyStatusLabel(
    QLabel *label,
    const QString& text,
    const QString& kind)
{
    if (!label)
    {
        return;
    }
    const bool propertyChanged = label->property("combinationNavigationStatusKind").toString() != kind;
    label->setText(text);
    if (!propertyChanged)
    {
        return;
    }
    label->setProperty("combinationNavigationStatusKind", kind);
    if (label->style())
    {
        label->style()->unpolish(label);
        label->style()->polish(label);
    }
    label->update();
}

QString CombinationNavigationPage::unavailableText() const
{
    return QStringLiteral("--");
}

} // namespace VaporView::Ground::Navigation
