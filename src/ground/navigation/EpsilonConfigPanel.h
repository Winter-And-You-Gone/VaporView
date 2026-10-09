#ifndef VAPORVIEW_EPSILON_CONFIG_PANEL_H_
#define VAPORVIEW_EPSILON_CONFIG_PANEL_H_

#include <QFrame>
#include <QVector>
#include <QByteArray>

#include "data_types.h"
#include "EpsilonSettings.h"
#include "EpsilonMaintenance.h"

#include <cstdint>
#include <map>

class QComboBox;
class QButtonGroup;
class QEvent;
class QGridLayout;
class QLabel;
class QPushButton;
class QResizeEvent;
class QWidget;
class QStackedWidget;
class QLineEdit;
class QProgressBar;

namespace VaporView::Ground::Navigation
{

class EpsilonConfigPanel final : public QFrame
{
    Q_OBJECT

public:
    explicit EpsilonConfigPanel(QWidget *parent = nullptr);

    void setEnglish(bool english);
    QVector<QPushButton *> takeSettingsNavigationButtons(QWidget *newParent);
    void setSettingsNavigationVisible(bool visible);
    void clearSettingsNavigationSelection();
    void setAvailable(bool available);
    void setPacketRates(const std::map<uint8_t, int>& packetRates);
    void setLivePacketRates(const VaporView::EpsilonData& epsilonData);
    std::map<uint8_t, int> packetRates() const;
    void setRtcmDevicePortIndex(int portIndex);
    int rtcmDevicePortIndex() const;
    VaporView::EpsilonSettingsGroup currentSettingsGroup() const;
    bool isDgnssPage() const;
    void setDgnssSnapshot(const VaporView::EpsilonDgnssSnapshot& snapshot, bool partial = false);
    void setSettingsSnapshot(const VaporView::EpsilonSettingsSnapshot& snapshot, bool partial = false);
    void invalidateSettings(bool preserveMaintenanceState = false);
    void setSettingsOperationPending(bool pending);
    void setSettingsAvailable(bool available);
    void setSettingsStatus(const QString& text);
    void setSettingsError(const QString& text);
    QByteArray exportSettingsJson() const;
    bool previewSettingsImport(const QByteArray& json, VaporView::EpsilonSettingsOperation& changes, QString& error) const;
    void applyImportedSettings(const VaporView::EpsilonSettingsOperation& changes);
    void setMaintenanceResult(const VaporView::EpsilonMaintenanceResult& result);
    void setRestartResult(bool succeeded);

signals:
    void recommendedProfileRequested();
    void saveRequested();
    void rtcmPortRequested();
    void reconfigureRequested();
    void settingsReadRequested(VaporView::EpsilonSettingsGroup group);
    void settingsApplyRequested(const VaporView::EpsilonSettingsOperation& operation);
    void deviceRestartRequested();
    void maintenanceRequested(VaporView::EpsilonMaintenanceAction action);
    void maintenanceCancelRequested();
    void dgnssReadRequested();
    void dgnssApplyRequested(const VaporView::EpsilonDgnssOperation& operation);

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void arrangePacketFields(bool twoColumns);
    void applyAppearance();
    void updatePacketLabelWidths();
    void updateLivePacketRateTexts();
    void updateDeviceInfoTexts();
    void updateSummaryTexts();
    void updateTexts();
    void createSettingsPages();
    void setAdvancedFeaturesExpanded(bool expanded);
    void updateSettingsNavigationVisibility();
    void updateSettingsTexts();
    void updateSettingsControls();
    void arrangeSettingsFields(bool twoColumns);
    void createAdvancedSettingsPage();
    QWidget *createParameterField(const VaporView::EpsilonParameterDescriptor& descriptor, QWidget *parent);
    VaporView::EpsilonSettingsOperation editedSettings() const;
    VaporView::EpsilonDgnssOperation editedDgnss() const;
    struct DgnssField
    {
        std::string name;
        QLabel *label = nullptr;
        QLabel *state = nullptr;
        QLineEdit *editor = nullptr;
        QString original;
        bool read = false;
        bool unsupported = false;
    };
    QVector<DgnssField> dgnss_fields_;
    QLabel *dgnss_title_ = nullptr;
    QPushButton *maintenance_cancel_button_ = nullptr;
    QProgressBar *maintenance_progress_ = nullptr;
    bool maintenance_running_ = false;
    bool maintenance_cancel_requested_ = false;

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
    QButtonGroup *settings_tab_group_ = nullptr;
    QPushButton *advanced_features_button_ = nullptr;
    QFrame *settings_navigation_tabs_ = nullptr;
    QFrame *settings_navigation_track_ = nullptr;
    bool settings_navigation_visible_ = true;
    QVector<QLabel *> settings_hints_;
    QVector<QGridLayout *> settings_grids_;
    QVector<QWidget *> settings_cards_;
    QVector<QLabel *> settings_card_titles_;
    QGridLayout *dual_antenna_grid_ = nullptr;
    QLabel *settings_actions_title_ = nullptr;
    QLabel *device_info_label_ = nullptr;
    QWidget *settings_actions_card_ = nullptr;
    QWidget *settings_actions_host_ = nullptr;
    QWidget *settings_actions_body_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    QWidget *communication_page_ = nullptr;
    QWidget *actions_container_ = nullptr;
    QComboBox *advanced_group_combo_ = nullptr;
    QLabel *advanced_group_label_ = nullptr;
    QStackedWidget *advanced_pages_ = nullptr;
    QVector<VaporView::EpsilonSettingsGroup> advanced_groups_;
    QVector<QLabel *> advanced_titles_;
    QLabel *advanced_hint_ = nullptr;
    QVector<QPushButton *> maintenance_buttons_;
    QLabel *maintenance_title_ = nullptr;
    QPushButton *settings_export_button_ = nullptr;
    QPushButton *settings_import_button_ = nullptr;
    QPushButton *settings_read_button_ = nullptr;
    QPushButton *device_restart_button_ = nullptr;
    QLabel *settings_status_label_ = nullptr;
    bool settings_pending_ = false;
    bool settings_available_ = false;
    bool settings_restart_required_ = false;
    bool settings_saved_ = false;
    bool settings_verified_ = false;
    bool settings_status_custom_ = false;
    bool maintenance_verification_pending_ = false;
    bool advanced_features_expanded_ = false;

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
