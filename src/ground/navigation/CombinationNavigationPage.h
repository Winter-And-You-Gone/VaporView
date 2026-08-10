#ifndef VAPORVIEW_COMBINATION_NAVIGATION_PAGE_H_
#define VAPORVIEW_COMBINATION_NAVIGATION_PAGE_H_

#include <QPointer>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QEvent;
class QFrame;
class QGridLayout;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

namespace VaporView::Ground::Navigation
{

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

    struct StatusSnapshot
    {
        bool epsilonOnline = false;
        bool navigationDataAvailable = false;
        QString gnssFixText;
        bool rtkServiceRunning = false;
        bool positionAvailable = false;
        double longitudeDeg = 0.0;
        double latitudeDeg = 0.0;
        double heightM = 0.0;
        bool attitudeAvailable = false;
        double rollDeg = 0.0;
        double pitchDeg = 0.0;
        double headingDeg = 0.0;
    };

    using StatusProvider = std::function<StatusSnapshot()>;

    explicit CombinationNavigationPage(QWidget *differentialPage, QWidget *parent = nullptr);

    Section currentSection() const;
    QWidget *differentialPage() const;
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
    struct FieldWidgets
    {
        QLabel *name = nullptr;
        QLabel *value = nullptr;
    };

    QWidget *createStatusPage();
    QWidget *createEpsilonPage();
    FieldWidgets addField(QGridLayout *layout,
                          int row,
                          QWidget *parent,
                          const QString& valueObjectName);
    void updateTexts();
    void applyAppearance();
    void applyStatusLabel(QLabel *label, const QString& text, const QString& kind);
    QString unavailableText() const;

    bool is_english_ = false;
    QButtonGroup *section_group_ = nullptr;
    QPushButton *status_button_ = nullptr;
    QPushButton *epsilon_button_ = nullptr;
    QPushButton *differential_button_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QWidget *status_page_ = nullptr;
    QWidget *epsilon_page_ = nullptr;
    QPointer<QWidget> differential_page_;
    QTimer *status_refresh_timer_ = nullptr;
    StatusProvider status_provider_;
    StatusSnapshot status_snapshot_;

    QLabel *navigation_status_title_ = nullptr;
    QLabel *position_title_ = nullptr;
    QLabel *attitude_title_ = nullptr;
    QLabel *epsilon_page_title_ = nullptr;
    QLabel *epsilon_page_description_ = nullptr;

    FieldWidgets epsilon_status_;
    FieldWidgets gnss_status_;
    FieldWidgets positioning_mode_;
    FieldWidgets rtk_status_;
    FieldWidgets ntrip_status_;
    FieldWidgets rtcm_status_;
    FieldWidgets longitude_;
    FieldWidgets latitude_;
    FieldWidgets height_;
    FieldWidgets roll_;
    FieldWidgets pitch_;
    FieldWidgets heading_;
    FieldWidgets epsilon_page_status_;
};

} // namespace VaporView::Ground::Navigation

#endif
