#ifndef VAPORVIEW_EPSILON_CONFIG_PANEL_H_
#define VAPORVIEW_EPSILON_CONFIG_PANEL_H_

#include <QFrame>
#include <QVector>

#include <cstdint>
#include <map>

class QCheckBox;
class QComboBox;
class QEvent;
class QGridLayout;
class QLabel;
class QPushButton;
class QResizeEvent;
class QWidget;

namespace VaporView::Ground::Navigation
{

class EpsilonConfigPanel final : public QFrame
{
    Q_OBJECT

public:
    explicit EpsilonConfigPanel(QWidget *parent = nullptr);

    void setEnglish(bool english);
    void setAvailable(bool available);
    void setCustomPacketProfileEnabled(bool enabled);
    bool customPacketProfileEnabled() const;
    void setPacketRates(const std::map<uint8_t, int>& packetRates);
    std::map<uint8_t, int> packetRates() const;

signals:
    void recommendedProfileRequested();
    void groupedProfileRequested();
    void saveRequested();
    void rtcmPortRequested();
    void reconfigureRequested();
    void rtkConfigRequested();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void arrangePacketFields(bool twoColumns);
    void applyAppearance();
    void updateSummaryTexts();
    void updateTexts();

    bool is_english_ = false;
    bool is_available_ = true;
    bool packet_layout_initialized_ = false;
    bool two_column_layout_ = true;
    QGridLayout *packet_grid_ = nullptr;
    QLabel *summary_title_label_ = nullptr;
    QLabel *output_title_label_ = nullptr;
    QLabel *device_settings_title_label_ = nullptr;
    QLabel *hint_label_ = nullptr;
    QLabel *availability_name_label_ = nullptr;
    QLabel *availability_value_label_ = nullptr;
    QLabel *profile_name_label_ = nullptr;
    QLabel *profile_value_label_ = nullptr;
    QLabel *packet_count_name_label_ = nullptr;
    QLabel *packet_count_value_label_ = nullptr;
    QLabel *grouped_description_label_ = nullptr;
    QLabel *rtcm_name_label_ = nullptr;
    QLabel *rtcm_description_label_ = nullptr;
    QLabel *reconfigure_name_label_ = nullptr;
    QLabel *reconfigure_description_label_ = nullptr;
    QLabel *rtk_name_label_ = nullptr;
    QLabel *rtk_description_label_ = nullptr;
    QCheckBox *custom_packet_check_ = nullptr;
    QVector<QLabel *> packet_group_labels_;
    QVector<QWidget *> packet_rate_fields_;
    QVector<QLabel *> packet_rate_labels_;
    QVector<QComboBox *> packet_rate_combos_;
    QVector<int> packet_rate_group_ids_;
    QPushButton *recommended_button_ = nullptr;
    QPushButton *grouped_button_ = nullptr;
    QPushButton *save_button_ = nullptr;
    QPushButton *rtcm_port_button_ = nullptr;
    QPushButton *reconfigure_button_ = nullptr;
    QPushButton *rtk_config_button_ = nullptr;
};

} // namespace VaporView::Ground::Navigation

#endif
