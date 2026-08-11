#ifndef VAPORVIEW_COMBINATION_NAVIGATION_PAGE_H_
#define VAPORVIEW_COMBINATION_NAVIGATION_PAGE_H_

#include "ground/navigation/NavigationStatusPanel.h"

#include <QPointer>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QEvent;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace VaporView::Ground::Navigation
{

class EpsilonConfigPanel;

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

private:
    QWidget *createStatusPage();
    void updateTexts();
    void applyAppearance();

    bool is_english_ = false;
    QButtonGroup *section_group_ = nullptr;
    QPushButton *status_button_ = nullptr;
    QPushButton *epsilon_button_ = nullptr;
    QPushButton *differential_button_ = nullptr;
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
