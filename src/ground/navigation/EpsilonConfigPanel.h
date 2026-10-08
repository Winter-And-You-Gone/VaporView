#ifndef VAPORVIEW_EPSILON_CONFIG_PANEL_H_
#define VAPORVIEW_EPSILON_CONFIG_PANEL_H_

#include <QFrame>
#include <QVector>

#include "data_types.h"
#include "EpsilonSettings.h"

#include <cstdint>
#include <map>

class QComboBox;
class QEvent;
class QGridLayout;
class QLabel;
class QPushButton;
class QResizeEvent;
class QWidget;
class QStackedWidget;

namespace VaporView::Ground::Navigation
{

class EpsilonConfigPanel final : public QFrame
{
    Q_OBJECT

public:
    explicit EpsilonConfigPanel(QWidget *parent = nullptr);

    void setEnglish(bool english);
    void setAvailable(bool available);
    void setPacketRates(const std::map<uint8_t, int>& packetRates);
    void setLivePacketRates(const VaporView::EpsilonData& epsilonData);
    std::map<uint8_t, int> packetRates() const;
    void setRtcmDevicePortIndex(int portIndex);
    int rtcmDevicePortIndex() const;
    VaporView::EpsilonSettingsGroup currentSettingsGroup() const;
    void setSettingsSnapshot(const VaporView::EpsilonSettingsSnapshot& snapshot, bool partial = false);
    void invalidateSettings();
    void setSettingsOperationPending(bool pending);
    void setSettingsAvailable(bool available);
    void setSettingsStatus(const QString& text);
    void setSettingsError(const QString& text);

signals:
    void recommendedProfileRequested();
    void saveRequested();
    void rtcmPortRequested();
    void reconfigureRequested();
    void settingsReadRequested(VaporView::EpsilonSettingsGroup group);
    void settingsApplyRequested(const VaporView::EpsilonSettingsOperation& operation);
    void deviceRestartRequested();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void arrangePacketFields(bool twoColumns);
    void applyAppearance();
    void updatePacketLabelWidths();
    void updateLivePacketRateTexts();
    void updateSummaryTexts();
    void updateTexts();
    void createSettingsPages();
    void updateSettingsTexts();
    void updateSettingsControls();
    void arrangeSettingsFields(bool twoColumns);
    VaporView::EpsilonSettingsOperation editedSettings() const;

    struct SettingsField
    {
        std::string name;
        VaporView::EpsilonSettingsGroup group;
        QLabel *label = nullptr;
        QLabel *state = nullptr;
        QWidget *editor = nullptr;
        QWidget *row = nullptr;
        bool read = false;
        bool unsupported = false;
        bool value_supported = false;
        double original = 0;
    };
    QVector<SettingsField> settings_fields_;
    QVector<QPushButton *> page_buttons_;
    QVector<QLabel *> settings_hints_;
    QVector<QGridLayout *> settings_grids_;
    QStackedWidget *pages_ = nullptr;
    QWidget *communication_page_ = nullptr;
    QWidget *actions_container_ = nullptr;
    QPushButton *settings_read_button_ = nullptr;
    QPushButton *device_restart_button_ = nullptr;
    QLabel *settings_status_label_ = nullptr;
    bool settings_pending_ = false;
    bool settings_available_ = false;
    bool settings_restart_required_ = false;
    bool settings_saved_ = false;
    bool settings_verified_ = false;
    bool settings_status_custom_ = false;

    bool is_english_ = false;
    bool is_available_ = true;
    bool packet_layout_initialized_ = false;
    bool two_column_layout_ = true;
    QGridLayout *packet_grid_ = nullptr;
    QGridLayout *live_packet_rate_grid_ = nullptr;
    QLabel *live_packet_rate_title_label_ = nullptr;
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
    QLabel *rtcm_name_label_ = nullptr;
    QLabel *rtcm_description_label_ = nullptr;
    QComboBox *rtcm_device_port_combo_ = nullptr;
    QLabel *reconfigure_name_label_ = nullptr;
    QLabel *reconfigure_description_label_ = nullptr;
    QVector<QLabel *> packet_group_labels_;
    QVector<QWidget *> packet_rate_fields_;
    QVector<QLabel *> packet_rate_labels_;
    QVector<QComboBox *> packet_rate_combos_;
    QVector<int> packet_rate_group_ids_;
    QVector<QWidget *> live_packet_rate_fields_;
    QVector<QLabel *> live_packet_rate_labels_;
    QVector<QLabel *> live_packet_rate_values_;
    VaporView::EpsilonData live_epsilon_data_;
    QPushButton *recommended_button_ = nullptr;
    QPushButton *save_button_ = nullptr;
    QPushButton *rtcm_port_button_ = nullptr;
    QPushButton *reconfigure_button_ = nullptr;
};

} // namespace VaporView::Ground::Navigation

#endif
