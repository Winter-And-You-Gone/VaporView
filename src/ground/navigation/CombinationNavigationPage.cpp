#include "ground/navigation/CombinationNavigationPage.h"

#include "ground/navigation/EpsilonConfigPanel.h"
#include "shared/theme/AppTheme.h"

#include <QButtonGroup>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
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

} // namespace

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
    rootLayout->addWidget(navigationRow);

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
    epsilonContentLayout->addWidget(epsilon_config_panel_);
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
    connect(status_refresh_timer_, &QTimer::timeout,
            this, &CombinationNavigationPage::refreshStatus);
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
    if (status_panel_)
    {
        status_panel_->setSnapshot(snapshot);
    }
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
    if (event &&
        (event->type() == QEvent::ApplicationPaletteChange ||
         event->type() == QEvent::PaletteChange))
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
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    auto *scrollArea = new QScrollArea(page);
    scrollArea->setObjectName(QStringLiteral("navigationStatusScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setMinimumWidth(0);
    scrollArea->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

    auto *content = new QWidget(scrollArea);
    content->setObjectName(QStringLiteral("navigationStatusContent"));
    prepareStyledBackground(content);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(kPageHorizontalInset,
                                      kPageVerticalInset,
                                      kPageHorizontalInset,
                                      kPageVerticalInset);
    contentLayout->setSpacing(0);
    status_panel_ = new NavigationStatusPanel(content);
    contentLayout->addWidget(status_panel_);
    contentLayout->addStretch(1);
    scrollArea->setWidget(content);
    pageLayout->addWidget(scrollArea, 1);
    return page;
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
    if (status_panel_)
    {
        status_panel_->setEnglish(is_english_);
    }
    if (epsilon_config_panel_)
    {
        epsilon_config_panel_->setEnglish(is_english_);
    }
}

void CombinationNavigationPage::applyAppearance()
{
    const QString style = QStringLiteral(
        "QWidget#combinationNavigationPage, QWidget#combinationNavigationNavigationRow, "
        "QWidget#combinationNavigationStatusPage, QWidget#navigationStatusContent, "
        "QWidget#combinationNavigationEpsilonPage, QStackedWidget#combinationNavigationStack, "
        "QScrollArea#navigationStatusScrollArea, QScrollArea#navigationStatusScrollArea > QWidget > QWidget { "
        "background-color: @vv-surface; border: none; }"
        "QFrame#combinationNavigationNavigationBar { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
        "QPushButton#combinationNavigationStatusButton, QPushButton#combinationNavigationEpsilonButton, "
        "QPushButton#combinationNavigationDifferentialButton { background-color: transparent; border: 1px solid transparent; border-radius: 6px; color: @vv-text; font-weight: 500; padding: 6px 12px; outline: none; }"
        "QPushButton#combinationNavigationStatusButton:checked, QPushButton#combinationNavigationEpsilonButton:checked, "
        "QPushButton#combinationNavigationDifferentialButton:checked { background-color: @vv-surface; color: @vv-primary; font-weight: 600; }"
        "QPushButton#combinationNavigationStatusButton:!checked:hover, QPushButton#combinationNavigationEpsilonButton:!checked:hover, "
        "QPushButton#combinationNavigationDifferentialButton:!checked:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "QPushButton#combinationNavigationStatusButton:pressed, QPushButton#combinationNavigationEpsilonButton:pressed, "
        "QPushButton#combinationNavigationDifferentialButton:pressed { background-color: @vv-primary-subtle-pressed; }"
        "QPushButton#combinationNavigationStatusButton:focus, QPushButton#combinationNavigationEpsilonButton:focus, "
        "QPushButton#combinationNavigationDifferentialButton:focus { border-color: @vv-focus; }");
    const QString resolvedStyle =
        VaporView::applyAppThemeTokens(style, VaporView::isDarkThemeEnabled());
    if (styleSheet() != resolvedStyle)
    {
        setStyleSheet(resolvedStyle);
    }
}

} // namespace VaporView::Ground::Navigation
