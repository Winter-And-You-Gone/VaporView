#ifndef VAPORVIEW_NAVIGATION_STATUS_PANEL_H_
#define VAPORVIEW_NAVIGATION_STATUS_PANEL_H_

#include <QString>
#include <QWidget>

#include <limits>

class QBoxLayout;
class QEvent;
class QFrame;
class QGridLayout;
class QLabel;
class QResizeEvent;
class QVBoxLayout;

namespace VaporView::Ground::Navigation
{

struct NavigationStatusSnapshot
{
    bool epsilonOnline = false;
    bool epsilonDataFresh = false;
    int epsilonDataAgeMs = -1;

    bool navigationDataAvailable = false;
    QString gnssFixText;
    bool gnssQualityAvailable = false;
    int satelliteCount = -1;
    double horizontalAccuracyM = std::numeric_limits<double>::quiet_NaN();

    bool positionAvailable = false;
    double longitudeDeg = std::numeric_limits<double>::quiet_NaN();
    double latitudeDeg = std::numeric_limits<double>::quiet_NaN();
    double heightM = std::numeric_limits<double>::quiet_NaN();
    bool speedAvailable = false;
    double speedMps = std::numeric_limits<double>::quiet_NaN();

    bool attitudeAvailable = false;
    double rollDeg = std::numeric_limits<double>::quiet_NaN();
    double pitchDeg = std::numeric_limits<double>::quiet_NaN();
    double headingDeg = std::numeric_limits<double>::quiet_NaN();

    bool rtkServiceRunning = false;
    bool differentialAvailable = false;
    QString ntripStatusText;
    QString rtcmStatusText;
    double differentialAgeS = std::numeric_limits<double>::quiet_NaN();
};

class NavigationStatusPanel final : public QWidget
{
public:
    explicit NavigationStatusPanel(QWidget *parent = nullptr);

    void setEnglish(bool english);
    void setSnapshot(const NavigationStatusSnapshot& snapshot);
    const NavigationStatusSnapshot& snapshot() const;

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct FieldWidgets
    {
        QLabel *name = nullptr;
        QLabel *value = nullptr;
    };

    QFrame *createCard(QWidget *parent,
                       const QString& objectName,
                       const QString& titleObjectName,
                       QLabel **titleOut,
                       QVBoxLayout **layoutOut);
    FieldWidgets addField(QGridLayout *layout,
                          int row,
                          int column,
                          QWidget *parent,
                          const QString& valueObjectName);
    void updateTexts();
    void applyAppearance();
    void applyStatusLabel(QLabel *label, const QString& text, const QString& kind);
    void applyValueLabel(QLabel *label, const QString& text);
    void updateResponsiveLayout();
    void scheduleShadowUpdate();
    QString unavailableText() const;

    bool is_english_ = false;
    NavigationStatusSnapshot snapshot_;

    QFrame *summary_card_ = nullptr;
    QFrame *position_card_ = nullptr;
    QFrame *attitude_card_ = nullptr;
    QFrame *gnss_card_ = nullptr;
    QFrame *differential_card_ = nullptr;
    QFrame *empty_state_ = nullptr;
    QWidget *details_row_ = nullptr;
    QWidget *quality_row_ = nullptr;
    QBoxLayout *details_layout_ = nullptr;
    QBoxLayout *quality_layout_ = nullptr;

    QLabel *summary_title_ = nullptr;
    QLabel *position_title_ = nullptr;
    QLabel *attitude_title_ = nullptr;
    QLabel *gnss_title_ = nullptr;
    QLabel *differential_title_ = nullptr;
    QLabel *empty_state_title_ = nullptr;
    QLabel *empty_state_detail_ = nullptr;

    FieldWidgets epsilon_status_;
    FieldWidgets gnss_status_;
    FieldWidgets ins_status_;
    FieldWidgets positioning_mode_;
    FieldWidgets data_freshness_;
    FieldWidgets rtk_status_;

    FieldWidgets longitude_;
    FieldWidgets latitude_;
    FieldWidgets height_;
    FieldWidgets speed_;
    FieldWidgets roll_;
    FieldWidgets pitch_;
    FieldWidgets heading_;
    FieldWidgets gnss_fix_;
    FieldWidgets satellites_;
    FieldWidgets horizontal_accuracy_;
    FieldWidgets ntrip_status_;
    FieldWidgets rtcm_status_;
    FieldWidgets differential_age_;
};

} // namespace VaporView::Ground::Navigation

#endif
