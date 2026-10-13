#include "ground/navigation/CombinationNavigationPage.h"

#include "ground/navigation/EpsilonConfigPanel.h"
#include "ground/rtk/RtkConfigDialog.h"
#include "ground/widgets/SegmentedSwitchMotion.h"
#include "shared/theme/AppTheme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QVariantAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyleOptionButton>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <utility>

namespace VaporView::Ground::Navigation
{
namespace
{
constexpr int kPageHorizontalInset = 18;
constexpr int kPageVerticalInset = 4;
constexpr int kSectionGap = 12;
constexpr int kNavigationBarHeight = 36;
constexpr int kNavigationRowHeight = kNavigationBarHeight + (kPageVerticalInset * 2);

void prepareStackPageForShow(QStackedWidget *stack, QWidget *page)
{
    if (!stack || !page)
    {
        return;
    }

    const QRect targetGeometry = stack->contentsRect();
    if (targetGeometry.isEmpty())
    {
        return;
    }

    if (page->geometry() != targetGeometry)
    {
        page->setGeometry(targetGeometry);
    }
    if (QLayout *pageLayout = page->layout())
    {
        pageLayout->invalidate();
        pageLayout->activate();
    }
    page->updateGeometry();
}

void settleStackPageForShow(QStackedWidget *stack, QWidget *page)
{
    prepareStackPageForShow(stack, page);
    // Settle child size hints before their parents consume them. A parent-first
    // pass leaves the RTK card row with another layout request after painting resumes.
    const auto children = page->findChildren<QObject *>();
    for (auto child = children.crbegin(); child != children.crend(); ++child)
    {
        QCoreApplication::sendPostedEvents(*child, QEvent::LayoutRequest);
    }
    QCoreApplication::sendPostedEvents(page, QEvent::LayoutRequest);
    prepareStackPageForShow(stack, page);
}

void prepareStyledBackground(QWidget *widget)
{
    if (!widget)
    {
        return;
    }
    widget->setAttribute(Qt::WA_StyledBackground, true);
    widget->setAutoFillBackground(true);
}

class CombinationNavigationSectionButton final : public QPushButton
{
public:
    using QPushButton::QPushButton;

    QSize sizeHint() const override
    {
        QSize hint = QPushButton::sizeHint();
        if (QWidget *track = parentWidget())
        {
            for (auto *button : track->findChildren<QPushButton *>(QString(), Qt::FindDirectChildrenOnly))
                hint.setWidth(std::max(hint.width(), button->fontMetrics().horizontalAdvance(button->text()) + 22));
        }
        return hint;
    }

    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QStyleOptionButton option;
        initStyleOption(&option);
        option.state &= ~QStyle::State_HasFocus;
        const QString buttonText = option.text;
        option.text.clear();

        style()->drawControl(QStyle::CE_PushButton, &option, &painter, this);
        if (buttonText.isEmpty())
        {
            return;
        }

        const QFontMetricsF metrics(painter.font());
        const QRectF textBounds = metrics.tightBoundingRect(buttonText);
        const QRectF buttonRect(rect());
        const QPointF baseline(buttonRect.center().x() - textBounds.center().x(),
                               buttonRect.center().y() - textBounds.center().y());
        const bool dark = VaporView::isDarkThemeEnabled();
        auto textColor = !isEnabled()
            ? VaporView::appThemeColor(VaporView::AppThemeColor::TextDisabled, dark)
            : VaporView::appThemeColor(
                isChecked() ? VaporView::AppThemeColor::Primary : VaporView::AppThemeColor::White,
                dark);
        if (isEnabled() && property("combinationNavigationSelectionProgress").isValid())
        {
            const qreal progress = property("combinationNavigationSelectionProgress").toReal();
            const auto inactive = VaporView::appThemeColor(VaporView::AppThemeColor::White, dark);
            const auto active = VaporView::appThemeColor(VaporView::AppThemeColor::Primary, dark);
            textColor.setRgbF(inactive.redF() + (active.redF() - inactive.redF()) * progress,
                             inactive.greenF() + (active.greenF() - inactive.greenF()) * progress,
                             inactive.blueF() + (active.blueF() - inactive.blueF()) * progress);
        }
        painter.setPen(textColor);
        painter.drawText(baseline, buttonText);

        if (isEnabled() && keyboard_focus_indicator_visible_)
        {
            painter.setPen(QPen(VaporView::appThemeColor(VaporView::AppThemeColor::Focus, dark), 1.0));
            painter.setBrush(Qt::NoBrush);
            const QRectF focusRect = buttonRect.adjusted(1.5, 1.5, -1.5, -1.5);
            painter.drawRoundedRect(focusRect, 12.5, 12.5);
        }
    }

    void focusInEvent(QFocusEvent *event) override
    {
        QPushButton::focusInEvent(event);
        const Qt::FocusReason reason = event ? event->reason() : Qt::OtherFocusReason;
        setKeyboardFocusIndicatorVisible(reason == Qt::TabFocusReason ||
                                         reason == Qt::BacktabFocusReason ||
                                         reason == Qt::ShortcutFocusReason);
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        QPushButton::focusOutEvent(event);
        setKeyboardFocusIndicatorVisible(false);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (QWidget *focusWidget = QApplication::focusWidget();
            focusWidget && focusWidget != this && focusWidget->window() == window())
        {
            focusWidget->clearFocus();
        }
        clearTrackKeyboardFocusIndicators();
        setKeyboardFocusIndicatorVisible(false);
        QPushButton::mousePressEvent(event);
        clearTrackKeyboardFocusIndicators();
        setKeyboardFocusIndicatorVisible(false);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QPushButton::mouseReleaseEvent(event);
        clearTrackKeyboardFocusIndicators();
        setKeyboardFocusIndicatorVisible(false);
    }

private:
    void clearTrackKeyboardFocusIndicators()
    {
        if (QWidget *track = parentWidget())
        {
            const auto children = track->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
            for (QWidget *child : children)
            {
                if (auto *button = dynamic_cast<CombinationNavigationSectionButton *>(child))
                {
                    button->setKeyboardFocusIndicatorVisible(false);
                }
            }
        }
    }

    void setKeyboardFocusIndicatorVisible(bool visible)
    {
        setProperty("combinationNavigationKeyboardFocus", visible);
        if (keyboard_focus_indicator_visible_ == visible)
        {
            return;
        }
        keyboard_focus_indicator_visible_ = visible;
        update();
    }

    bool keyboard_focus_indicator_visible_ = false;
};

class CombinationNavigationSelectionThumb final : public QFrame
{
public:
    using QFrame::QFrame;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (bounds.width() <= 0.0 || bounds.height() <= 0.0)
        {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(VaporView::appThemeColor(
            VaporView::AppThemeColor::Surface, VaporView::isDarkThemeEnabled()));
        painter.drawRoundedRect(bounds, bounds.height() / 2.0, bounds.height() / 2.0);
    }
};

} // namespace

QPushButton *createNavigationSectionButton(QWidget *parent)
{
    auto *button = new CombinationNavigationSectionButton(parent);
    button->setCheckable(true);
    button->setAutoDefault(false);
    button->setDefault(false);
    button->setFlat(true);
    button->setFocusPolicy(Qt::TabFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumWidth(92);
    button->setMinimumHeight(0);
    button->setContentsMargins(0, 0, 0, 0);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return button;
}

CombinationNavigationPage::CombinationNavigationPage(QWidget *differentialPage, QWidget *parent)
    : QWidget(parent)
    , differential_page_(differentialPage)
{
    Q_ASSERT(differential_page_ != nullptr);
    setObjectName(QStringLiteral("combinationNavigationPage"));
    prepareStyledBackground(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *rootLayout = new QGridLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *navigationRow = new QWidget(this);
    navigationRow->setObjectName(QStringLiteral("combinationNavigationNavigationRow"));
    prepareStyledBackground(navigationRow);
    navigationRow->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *navigationRowLayout = new QHBoxLayout(navigationRow);
    navigationRowLayout->setContentsMargins(kPageHorizontalInset,
                                            kPageVerticalInset,
                                            kPageHorizontalInset,
                                            kPageVerticalInset);
    navigationRowLayout->setSpacing(0);

    auto *navigationBar = new QFrame(navigationRow);
    navigationBar->setObjectName(QStringLiteral("combinationNavigationNavigationBar"));
    prepareStyledBackground(navigationBar);
    navigationBar->setFixedHeight(kNavigationBarHeight);
    navigationBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *navigationBarLayout = new QHBoxLayout(navigationBar);
    navigationBarLayout->setContentsMargins(2, 2, 2, 2);
    navigationBarLayout->setSpacing(0);

    auto *navigationTrack = new QFrame(navigationBar);
    navigationTrack->setObjectName(QStringLiteral("combinationNavigationNavigationTrack"));
    prepareStyledBackground(navigationTrack);
    navigationTrack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    navigation_track_ = navigationTrack;
    navigationTrack->installEventFilter(this);
    navigationBarLayout->addWidget(navigationTrack);

    auto *navigationLayout = new QHBoxLayout(navigationTrack);
    navigationLayout->setContentsMargins(2, 2, 2, 2);
    navigationLayout->setSpacing(0);

    navigation_selection_thumb_ = new CombinationNavigationSelectionThumb(navigationTrack);
    navigation_selection_thumb_->setObjectName(
        QStringLiteral("combinationNavigationNavigationSelectionThumb"));
    navigation_selection_thumb_->setFrameShape(QFrame::NoFrame);
    navigation_selection_thumb_->setAttribute(Qt::WA_TransparentForMouseEvents);
    navigation_selection_thumb_->setAttribute(Qt::WA_TranslucentBackground);
    navigation_selection_thumb_->setAutoFillBackground(false);
    navigation_selection_animation_ = new QVariantAnimation(this);
    navigation_selection_animation_->setObjectName(
        QStringLiteral("combinationNavigationNavigationSelectionAnimation"));
    navigation_selection_animation_->setDuration(Widgets::kSegmentedSwitchAnimationDurationMs);
    navigation_selection_animation_->setEasingCurve(QEasingCurve::Linear);
    connect(navigation_selection_animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        updateNavigationSelectionFrame(value.toReal());
    });
    connect(navigation_selection_animation_, &QVariantAnimation::finished, this, [this]() {
        syncNavigationSelectionThumb(false);
    });

    section_group_ = new QButtonGroup(this);
    section_group_->setExclusive(true);
    auto createSectionButton = [this, navigationTrack, navigationLayout](
                                   const QString& objectName,
                                   Section section) {
        auto *button = createNavigationSectionButton(navigationTrack);
        button->setObjectName(objectName);
        button->installEventFilter(this);
        section_group_->addButton(button, static_cast<int>(section));
        navigationLayout->addWidget(button, 1);
        return button;
    };

    status_button_ = createSectionButton(
        QStringLiteral("combinationNavigationStatusButton"), Section::Status);
    differential_button_ = createSectionButton(
        QStringLiteral("combinationNavigationDifferentialButton"), Section::Differential);
    navigation_selection_thumb_->lower();
    status_button_->raise();
    differential_button_->raise();
    navigationRowLayout->addWidget(navigationBar, 0, Qt::AlignHCenter | Qt::AlignVCenter);

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
    epsilonScrollArea->viewport()->setObjectName(QStringLiteral("epsilonConfigViewport"));
    prepareStyledBackground(epsilonScrollArea->viewport());
    epsilonScrollArea->setWidgetResizable(true);
    epsilonScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Reserve the rail across all sub-pages so switching pages never changes
    // the available card width for a frame while the scrollbar range settles.
    epsilonScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    epsilonScrollArea->setFrameShape(QFrame::NoFrame);
    epsilonScrollArea->setMinimumWidth(0);
    epsilonScrollArea->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    auto *epsilonContent = new QWidget(epsilonScrollArea);
    epsilonContent->setObjectName(QStringLiteral("epsilonConfigContent"));
    prepareStyledBackground(epsilonContent);
    auto *epsilonContentLayout = new QVBoxLayout(epsilonContent);
    epsilonContentLayout->setContentsMargins(kPageHorizontalInset,
                                             kNavigationRowHeight + kPageVerticalInset,
                                             kPageHorizontalInset,
                                             kPageVerticalInset);
    epsilonContentLayout->setSpacing(kSectionGap);
    epsilon_config_panel_ = new EpsilonConfigPanel(epsilonContent);
    const QVector<QPushButton *> epsilonNavigationButtons =
        epsilon_config_panel_->takeSettingsNavigationButtons(navigationTrack);
    int settingsButtonId = 3;
    for (QPushButton *button : epsilonNavigationButtons)
    {
        if (!button)
        {
            continue;
        }
        button->setProperty("epsilonUnifiedNavigationButton", true);
        button->installEventFilter(this);
        if (!epsilon_button_)
            epsilon_button_ = button;
        navigationLayout->addWidget(button, 1);
        if (button->isCheckable())
        {
            section_group_->addButton(button, settingsButtonId++);
        }
        button->raise();
    }
    // Keep the shared capsule structurally stable across all three content pages.
    // The EPSILON entries, including the merged advanced page, remain visible.
    epsilon_config_panel_->setSettingsNavigationVisible(true);
    epsilonContentLayout->addWidget(epsilon_config_panel_);
    epsilonContentLayout->addStretch(1);
    epsilonScrollArea->setWidget(epsilonContent);
    epsilonPageLayout->addWidget(epsilonScrollArea, 1);

    stack_->addWidget(status_page_);
    stack_->addWidget(epsilon_page_);
    stack_->addWidget(differential_page_);
    if (auto *rtkPage = qobject_cast<RtkConfigDialog *>(differential_page_.data()))
    {
        rtkPage->setCombinationNavigationTopInset(kNavigationRowHeight);
    }
    rootLayout->addWidget(stack_, 0, 0);
    rootLayout->addWidget(navigationRow, 0, 0, Qt::AlignHCenter | Qt::AlignTop);
    navigationRow->raise();

    connect(section_group_, &QButtonGroup::idClicked, this, [this](int id) {
        if (id < 3)
            setCurrentSection(static_cast<Section>(id));
        else
            activateNavigationPage(Section::Epsilon, qobject_cast<QPushButton *>(section_group_->button(id)));
    });
    connect(section_group_, &QButtonGroup::buttonToggled, this, [this](QAbstractButton *, bool checked) {
        if (checked) syncNavigationSelectionThumb(true);
    });
    QWidget::setTabOrder(status_button_, differential_button_);
    QWidget::setTabOrder(differential_button_, epsilon_button_);

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
    QTimer::singleShot(0, this, [this]() { syncNavigationSelectionThumb(false); });
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
    if (section == Section::Epsilon)
    {
        epsilon_button_->click();
        return;
    }
    activateNavigationPage(section, qobject_cast<QPushButton *>(section_group_->button(static_cast<int>(section))));
}

void CombinationNavigationPage::activateNavigationPage(Section section, QPushButton *selection)
{
    const int index = static_cast<int>(section);
    if (!stack_ || index < 0 || index >= stack_->count())
    {
        return;
    }

    const bool changed = stack_->currentIndex() != index;
    const bool freezeUpdatesForSwitch =
        changed && section == Section::Differential && updatesEnabled();
    if (freezeUpdatesForSwitch)
    {
        setUpdatesEnabled(false);
    }
    if (changed)
    {
        prepareStackPageForShow(stack_, stack_->widget(index));
    }
    stack_->setCurrentIndex(index);
    if (epsilon_config_panel_)
    {
        // The unified capsule always exposes the same six primary entries;
        // changing the content page must not resize or collapse the capsule.
        epsilon_config_panel_->setSettingsNavigationVisible(true);
        if (section != Section::Epsilon)
        {
            epsilon_config_panel_->clearSettingsNavigationSelection();
        }
        navigation_track_->layout()->invalidate();
        navigation_track_->layout()->activate();
        navigation_track_->updateGeometry();
    }
    if (freezeUpdatesForSwitch)
    {
        settleStackPageForShow(stack_, stack_->widget(index));
    }
    if (selection)
    {
        const QSignalBlocker blocker(selection);
        selection->setChecked(true);
    }
    syncNavigationSelectionThumb(true);
    if (section == Section::Status || section == Section::Epsilon)
    {
        refreshStatus();
    }
    if (changed)
    {
        emit currentSectionChanged(section);
    }
    if (freezeUpdatesForSwitch)
    {
        setUpdatesEnabled(true);
        update();
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

bool CombinationNavigationPage::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched == navigation_track_ || (section_group_ && watched == section_group_->checkedButton())) && event &&
        (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest ||
         event->type() == QEvent::Show))
    {
        QTimer::singleShot(0, this, [this]() {
            if (navigation_selection_animation_ &&
                navigation_selection_animation_->state() == QAbstractAnimation::Running)
            {
                return;
            }
            syncNavigationSelectionThumb(false);
        });
    }
    return QWidget::eventFilter(watched, event);
}

void CombinationNavigationPage::syncNavigationSelectionThumb(bool animated)
{
    if (!navigation_selection_thumb_ || !navigation_selection_animation_ || !section_group_ ||
        !stack_)
    {
        return;
    }

    auto *button = qobject_cast<QPushButton *>(section_group_->checkedButton());
    if (!button)
    {
        return;
    }

    const QRect targetGeometry = button->geometry();
    if (targetGeometry.width() <= 0 || targetGeometry.height() <= 0)
    {
        QTimer::singleShot(0, this, [this]() { syncNavigationSelectionThumb(false); });
        return;
    }

    navigation_selection_thumb_->show();
    navigation_selection_thumb_->lower();
    if (!animated || navigation_thumb_rect_.isNull())
    {
        navigation_selection_animation_->stop();
        navigation_thumb_start_rect_ = targetGeometry;
        navigation_thumb_target_rect_ = targetGeometry;
        navigation_thumb_start_scale_x_ = navigation_thumb_start_scale_y_ = 1.0;
        updateNavigationSelectionFrame(1.0);
        return;
    }

    if (navigation_thumb_target_rect_ == targetGeometry &&
        navigation_selection_animation_->state() == QAbstractAnimation::Running)
        return;
    if (navigation_thumb_rect_ == targetGeometry && navigation_thumb_scale_x_ == 1.0 &&
        navigation_thumb_scale_y_ == 1.0)
    {
        return;
    }

    navigation_selection_animation_->stop();
    navigation_thumb_start_rect_ = navigation_thumb_rect_;
    const QRectF currentBounds(navigation_selection_thumb_->geometry());
    navigation_thumb_start_rect_.moveLeft(targetGeometry.left() >= navigation_thumb_rect_.left()
        ? currentBounds.left() : currentBounds.right() - navigation_thumb_rect_.width());
    navigation_thumb_target_rect_ = targetGeometry;
    navigation_thumb_start_scale_x_ = navigation_thumb_scale_x_;
    navigation_thumb_start_scale_y_ = navigation_thumb_scale_y_;
    navigation_selection_animation_->setDuration(qApp->property("vaporViewReducedMotion").toBool()
        ? Widgets::kSegmentedSwitchReducedMotionDurationMs : Widgets::kSegmentedSwitchAnimationDurationMs);
    navigation_selection_animation_->setStartValue(0.0);
    navigation_selection_animation_->setEndValue(1.0);
    navigation_selection_animation_->start();
}

void CombinationNavigationPage::updateNavigationSelectionFrame(qreal progress)
{
    const bool reducedMotion = qApp->property("vaporViewReducedMotion").toBool();
    const auto motion = Widgets::sampleSegmentedSwitchMotion(
        progress, navigation_thumb_start_rect_.left(), navigation_thumb_target_rect_.left(),
        navigation_thumb_start_scale_x_, navigation_thumb_start_scale_y_,
        std::clamp(navigation_thumb_target_rect_.width() * 2.0 * 0.014, 2.0, 3.5), reducedMotion);
    navigation_thumb_scale_x_ = motion.scale_x;
    navigation_thumb_scale_y_ = motion.scale_y;
    navigation_thumb_rect_ = QRectF(motion.position, navigation_thumb_target_rect_.top(),
        navigation_thumb_start_rect_.width() +
            (navigation_thumb_target_rect_.width() - navigation_thumb_start_rect_.width()) * progress,
        navigation_thumb_target_rect_.height());
    QRectF bounds = navigation_thumb_rect_;
    const qreal scaledWidth = bounds.width() * motion.scale_x;
    if (navigation_thumb_target_rect_.left() >= navigation_thumb_start_rect_.left())
        bounds.setWidth(scaledWidth);
    else
        bounds.setLeft(bounds.right() - scaledWidth);
    bounds.setTop(navigation_thumb_rect_.center().y() - navigation_thumb_rect_.height() * motion.scale_y / 2.0);
    bounds.setBottom(navigation_thumb_rect_.center().y() + navigation_thumb_rect_.height() * motion.scale_y / 2.0);
    navigation_selection_thumb_->setGeometry(bounds.toRect());
    for (auto *button : section_group_->buttons())
    {
        const qreal coverage = std::clamp(1.0 - std::abs(
            QRectF(button->geometry()).center().x() - navigation_thumb_rect_.center().x()) /
            std::max<qreal>(1.0, button->width()), 0.0, 1.0);
        button->setProperty("combinationNavigationSelectionProgress", coverage);
        button->update();
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
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setMinimumWidth(0);
    scrollArea->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

    auto *content = new QWidget(scrollArea);
    content->setObjectName(QStringLiteral("navigationStatusContent"));
    prepareStyledBackground(content);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(kPageHorizontalInset,
                                      kNavigationRowHeight + kPageVerticalInset,
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

    const std::array<std::pair<QPushButton *, QString>, 2> sectionButtons{{
        {status_button_, is_english_ ? QStringLiteral("Status") : QStringLiteral("状态")},
        {differential_button_, is_english_ ? QStringLiteral("Differential Positioning")
                                           : QStringLiteral("差分定位")},
    }};
    for (const auto& [button, text] : sectionButtons)
    {
        button->setText(text);
        button->setToolTip(text);
        button->setAccessibleName(text);
    }
    for (const auto& entry : sectionButtons) entry.first->updateGeometry();
    navigation_track_->layout()->invalidate();
    navigation_track_->parentWidget()->layout()->invalidate();
    navigation_track_->parentWidget()->updateGeometry();
    if (status_panel_)
    {
        status_panel_->setEnglish(is_english_);
    }
    if (epsilon_config_panel_)
    {
        epsilon_config_panel_->setEnglish(is_english_);
        navigation_track_->layout()->invalidate();
        navigation_track_->layout()->activate();
        navigation_track_->updateGeometry();
    }
}

void CombinationNavigationPage::applyAppearance()
{
    const bool dark = VaporView::isDarkThemeEnabled();
    const QString trackOutline = VaporView::appThemeColorName(
        dark ? VaporView::AppThemeColor::BorderStrong : VaporView::AppThemeColor::White,
        dark);
    const QString style = QStringLiteral(
        "QWidget#combinationNavigationPage, QWidget#combinationNavigationStatusPage, "
        "QWidget#navigationStatusContent, "
        "QWidget#combinationNavigationEpsilonPage, QWidget#epsilonConfigContent, QWidget#epsilonConfigViewport, "
        "QStackedWidget#combinationNavigationStack, QScrollArea#navigationStatusScrollArea, "
        "QScrollArea#navigationStatusScrollArea > QWidget > QWidget, QScrollArea#epsilonConfigScrollArea, "
        "QScrollArea#epsilonConfigScrollArea > QWidget > QWidget { "
        "background-color: @vv-window; border: none; }"
        "QWidget#combinationNavigationNavigationRow { background-color: transparent; border: none; }"
        "QScrollArea#navigationStatusScrollArea QScrollBar:vertical, "
        "QScrollArea#epsilonConfigScrollArea QScrollBar:vertical, "
        "QScrollArea#rtkConfigScrollArea QScrollBar:vertical { "
        "background-color: @vv-window; width: 8px; border: none; border-radius: 4px; margin: 0px; }"
        "QScrollArea#navigationStatusScrollArea QScrollBar::handle:vertical, "
        "QScrollArea#epsilonConfigScrollArea QScrollBar::handle:vertical, "
        "QScrollArea#rtkConfigScrollArea QScrollBar::handle:vertical { "
        "background-color: @vv-scrollbar-handle; min-height: 30px; border: none; border-radius: 4px; margin: 0px; }"
        "QScrollArea#navigationStatusScrollArea QScrollBar::handle:vertical:hover, "
        "QScrollArea#epsilonConfigScrollArea QScrollBar::handle:vertical:hover, "
        "QScrollArea#rtkConfigScrollArea QScrollBar::handle:vertical:hover { "
        "background-color: @vv-scrollbar-handle-hover; }"
        "QScrollArea#navigationStatusScrollArea QScrollBar::add-page:vertical, "
        "QScrollArea#navigationStatusScrollArea QScrollBar::sub-page:vertical, "
        "QScrollArea#epsilonConfigScrollArea QScrollBar::add-page:vertical, "
        "QScrollArea#epsilonConfigScrollArea QScrollBar::sub-page:vertical, "
        "QScrollArea#rtkConfigScrollArea QScrollBar::add-page:vertical, "
        "QScrollArea#rtkConfigScrollArea QScrollBar::sub-page:vertical { "
        "background-color: @vv-window; border-radius: 4px; }"
        "QScrollArea#navigationStatusScrollArea QScrollBar::add-line:vertical, "
        "QScrollArea#navigationStatusScrollArea QScrollBar::sub-line:vertical, "
        "QScrollArea#epsilonConfigScrollArea QScrollBar::add-line:vertical, "
        "QScrollArea#epsilonConfigScrollArea QScrollBar::sub-line:vertical, "
        "QScrollArea#rtkConfigScrollArea QScrollBar::add-line:vertical, "
        "QScrollArea#rtkConfigScrollArea QScrollBar::sub-line:vertical { "
        "background-color: @vv-window; border: none; height: 0px; }"
        "QFrame#combinationNavigationNavigationBar { background-color: @vv-primary-subtle; border: 1px solid @vv-border-strong; border-radius: 18px; }"
        "QFrame#combinationNavigationNavigationTrack { background-color: @vv-primary; border: 1px solid %1; border-radius: 15px; }"
        "QPushButton#combinationNavigationStatusButton, "
        "QPushButton#combinationNavigationDifferentialButton { background-color: transparent; border: 1px solid transparent; border-radius: 13px; color: @vv-white; font-weight: 600; margin: 0; min-height: 0; padding: 0 10px; outline: none; }"
        "QPushButton#combinationNavigationStatusButton:checked, "
        "QPushButton#combinationNavigationDifferentialButton:checked { background-color: transparent; color: @vv-primary; font-weight: 600; }"
        "QPushButton#combinationNavigationStatusButton:!checked:hover, "
        "QPushButton#combinationNavigationDifferentialButton:!checked:hover { background-color: transparent; color: @vv-white; }"
        "QPushButton#combinationNavigationStatusButton:pressed, "
        "QPushButton#combinationNavigationDifferentialButton:pressed { background-color: transparent; }"
        "QPushButton#combinationNavigationStatusButton:checked:pressed, "
        "QPushButton#combinationNavigationDifferentialButton:checked:pressed { background-color: transparent; }"
        "QFrame#combinationNavigationNavigationTrack QPushButton[epsilonUnifiedNavigationButton=\"true\"] { background-color: transparent; border: 1px solid transparent; border-radius: 13px; color: @vv-white; font-weight: 600; margin: 0; min-height: 0; padding: 0 10px; outline: none; }"
        "QFrame#combinationNavigationNavigationTrack QPushButton[epsilonUnifiedNavigationButton=\"true\"]:checked { background-color: transparent; color: @vv-primary; font-weight: 600; }"
        "QFrame#combinationNavigationNavigationTrack QPushButton[epsilonUnifiedNavigationButton=\"true\"]:!checked:hover { background-color: transparent; color: @vv-white; }"
        "QFrame#combinationNavigationNavigationTrack QPushButton[epsilonUnifiedNavigationButton=\"true\"]:pressed { background-color: transparent; }"
        "QFrame#combinationNavigationNavigationTrack QPushButton[epsilonUnifiedNavigationButton=\"true\"]:checked:pressed { background-color: transparent; }")
            .arg(trackOutline);
    const QString resolvedStyle =
        VaporView::applyAppThemeTokens(style, dark);
    if (styleSheet() != resolvedStyle)
    {
        setStyleSheet(resolvedStyle);
    }
}

} // namespace VaporView::Ground::Navigation
