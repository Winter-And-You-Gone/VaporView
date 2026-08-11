#ifndef VAPORVIEW_EPSILON_CONFIG_PANEL_H_
#define VAPORVIEW_EPSILON_CONFIG_PANEL_H_

#include <QFrame>
#include <QVector>

#include <cstdint>
#include <map>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

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

private:
    void updateTexts();

    bool is_english_ = false;
    QLabel *title_label_ = nullptr;
    QLabel *hint_label_ = nullptr;
    QCheckBox *custom_packet_check_ = nullptr;
    QVector<QLabel *> packet_rate_labels_;
    QVector<QComboBox *> packet_rate_combos_;
    QPushButton *recommended_button_ = nullptr;
    QPushButton *grouped_button_ = nullptr;
    QPushButton *save_button_ = nullptr;
    QPushButton *rtcm_port_button_ = nullptr;
    QPushButton *reconfigure_button_ = nullptr;
    QPushButton *rtk_config_button_ = nullptr;
};

} // namespace VaporView::Ground::Navigation

#endif
