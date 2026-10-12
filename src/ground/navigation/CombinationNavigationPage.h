#ifndef VAPORVIEW_COMBINATION_NAVIGATION_PAGE_H_
#define VAPORVIEW_COMBINATION_NAVIGATION_PAGE_H_

#include "ground/navigation/NavigationStatusPanel.h"

#include <QPointer>
#include <QRectF>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QEvent;
class QFrame;
class QVariantAnimation;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace VaporView::Ground::Navigation
{

class EpsilonConfigPanel;

QPushButton *createNavigationSectionButton(QWidget *parent);

class CombinationNavigationPage final : public QWidget
{
    Q_OBJECT

public:
    enum class Section
    {
        Status = 0,
        Epsilon = 1,
        Differential = 2,
    };
    Q_ENUM(Section)

    using StatusSnapshot = NavigationStatusSnapshot;

    using StatusProvider = std::function<StatusSnapshot()>;

    explicit CombinationNavigationPage(QWidget *differentialPage, QWidget *parent = nullptr);

    Section currentSection() const;
    QWidget *differentialPage() const;
    EpsilonConfigPanel *epsilonConfigPanel() const;
    void setCurrentSection(Section section);
    void showStatusPage();
    void showDifferentialPage();
    void setEnglish(bool english);
    void setStatusProvider(StatusProvider provider);
    void setStatusSnapshot(const StatusSnapshot& snapshot);
    void refreshStatus();

signals:
    void currentSectionChanged(VaporView::Ground::Navigation::CombinationNavigationPage::Section section);

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createStatusPage();
    void updateTexts();
    void applyAppearance();
    void syncNavigationSelectionThumb(bool animated);
    void updateNavigationSelectionFrame(qreal progress);
    void activateNavigationPage(Section section, QPushButton *selection);

    bool is_english_ = false;
    QButtonGroup *section_group_ = nullptr;
    QPushButton *status_button_ = nullptr;
    QPushButton *epsilon_button_ = nullptr;
    QPushButton *differential_button_ = nullptr;
    QWidget *navigation_track_ = nullptr;
    QFrame *navigation_selection_thumb_ = nullptr;
    QVariantAnimation *navigation_selection_animation_ = nullptr;
    QRectF navigation_thumb_rect_;
    QRectF navigation_thumb_start_rect_;
    QRectF navigation_thumb_target_rect_;
    qreal navigation_thumb_scale_x_ = 1.0;
    qreal navigation_thumb_scale_y_ = 1.0;
    qreal navigation_thumb_start_scale_x_ = 1.0;
    qreal navigation_thumb_start_scale_y_ = 1.0;
    QStackedWidget *stack_ = nullptr;
    QWidget *status_page_ = nullptr;
    NavigationStatusPanel *status_panel_ = nullptr;
    QWidget *epsilon_page_ = nullptr;
    QPointer<QWidget> differential_page_;
    QTimer *status_refresh_timer_ = nullptr;
    StatusProvider status_provider_;
    StatusSnapshot status_snapshot_;

    EpsilonConfigPanel *epsilon_config_panel_ = nullptr;
};

} // namespace VaporView::Ground::Navigation

#endif
