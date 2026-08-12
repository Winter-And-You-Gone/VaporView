#pragma once

#include "TelemetryTypes.h"
#include "data_types.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVector>
#include <QWidget>

#include <array>
#include <limits>

class QButtonGroup;
class QEvent;
class QObject;
class QRadioButton;
class TemperatureTrendPlotWidget;

class GnssPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GnssPanel(QWidget *parent = nullptr);
    void updateData(const VaporView::GnssData& gnss_data, quint64 timestamp_us = 0);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();

    QLabel *rate_label_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *time_label_ = nullptr;
    QLabel *lat_label_ = nullptr;
    QLabel *lon_label_ = nullptr;
    QLabel *alt_label_ = nullptr;
    QLabel *vel_n_label_ = nullptr;
    QLabel *vel_e_label_ = nullptr;
    QLabel *vel_ground_label_ = nullptr;
    QLabel *heading_label_ = nullptr;
    QLabel *pitch_label_ = nullptr;
    QLabel *heading_len_label_ = nullptr;
    QLabel *heading_type_label_ = nullptr;
    QLabel *heading_sats_label_ = nullptr;
    QLabel *sats_label_ = nullptr;
    QLabel *gdop_label_ = nullptr;
    QLabel *pdop_label_ = nullptr;
    QLabel *hdop_label_ = nullptr;
    QLabel *htdop_label_ = nullptr;
    QLabel *tdop_label_ = nullptr;
    QLabel *diff_age_label_ = nullptr;
    QLabel *undulation_label_ = nullptr;
    QLabel *sigma_lat_label_ = nullptr;
    QLabel *sigma_lon_label_ = nullptr;
    QLabel *sigma_alt_label_ = nullptr;
    QLabel *cutoff_label_ = nullptr;
    QLabel *status_lbl_ = nullptr;
    QLabel *time_lbl_ = nullptr;
    QLabel *lat_lbl_ = nullptr;
    QLabel *lon_lbl_ = nullptr;
    QLabel *alt_lbl_ = nullptr;
    QLabel *vel_n_lbl_ = nullptr;
    QLabel *vel_e_lbl_ = nullptr;
    QLabel *vel_ground_lbl_ = nullptr;
    QLabel *heading_lbl_ = nullptr;
    QLabel *pitch_lbl_ = nullptr;
    QLabel *heading_type_lbl_ = nullptr;
    QLabel *heading_len_lbl_ = nullptr;
    QLabel *heading_sats_lbl_ = nullptr;
    QLabel *sats_lbl_ = nullptr;
    QLabel *diff_lbl_ = nullptr;
    QLabel *gdop_lbl_ = nullptr;
    QLabel *pdop_lbl_ = nullptr;
    QLabel *hdop_lbl_ = nullptr;
    QLabel *htdop_lbl_ = nullptr;
    QLabel *tdop_lbl_ = nullptr;
    QLabel *cutoff_lbl_ = nullptr;
    QLabel *undulation_lbl_ = nullptr;
    QLabel *sigma_lat_lbl_ = nullptr;
    QLabel *sigma_lon_lbl_ = nullptr;
    QLabel *sigma_alt_lbl_ = nullptr;
    bool is_english_ = false;
};

class ImuPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ImuPanel(QWidget *parent = nullptr);
    void updateData(const VaporView::ImuData& imu_data, quint64 gnss_timestamp_us = 0);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();

    QLabel *rate_label_ = nullptr;
    QLabel *acc_x_label_ = nullptr;
    QLabel *acc_y_label_ = nullptr;
    QLabel *acc_z_label_ = nullptr;
    QLabel *gyr_x_label_ = nullptr;
    QLabel *gyr_y_label_ = nullptr;
    QLabel *gyr_z_label_ = nullptr;
    QLabel *roll_label_ = nullptr;
    QLabel *pitch_label_ = nullptr;
    QLabel *yaw_label_ = nullptr;
    QLabel *quat_w_label_ = nullptr;
    QLabel *quat_x_label_ = nullptr;
    QLabel *quat_y_label_ = nullptr;
    QLabel *quat_z_label_ = nullptr;
    QLabel *temp_label_ = nullptr;
    QLabel *press_label_ = nullptr;
    QLabel *source_label_ = nullptr;
    QLabel *time_label_ = nullptr;
    QLabel *pps_label_ = nullptr;
    QLabel *source_lbl_ = nullptr;
    QLabel *time_lbl_ = nullptr;
    QLabel *pps_lbl_ = nullptr;
    QLabel *accel_sep_ = nullptr;
    QLabel *gyro_sep_ = nullptr;
    QLabel *attitude_sep_ = nullptr;
    QLabel *quat_sep_ = nullptr;
    QLabel *env_sep_ = nullptr;
    QLabel *temp_lbl_ = nullptr;
    QLabel *press_lbl_ = nullptr;
    QLabel *acc_x_lbl_ = nullptr;
    QLabel *acc_y_lbl_ = nullptr;
    QLabel *acc_z_lbl_ = nullptr;
    QLabel *gyr_x_lbl_ = nullptr;
    QLabel *gyr_y_lbl_ = nullptr;
    QLabel *gyr_z_lbl_ = nullptr;
    QLabel *roll_lbl_ = nullptr;
    QLabel *pitch_lbl_ = nullptr;
    QLabel *yaw_lbl_ = nullptr;
    QLabel *quat_w_lbl_ = nullptr;
    QLabel *quat_x_lbl_ = nullptr;
    QLabel *quat_y_lbl_ = nullptr;
    QLabel *quat_z_lbl_ = nullptr;
    bool is_english_ = false;
};

class PtbPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PtbPanel(QWidget *parent = nullptr);
    void updateData(const VaporView::PtbData& ptb_data);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();
    QLabel *rate_label_ = nullptr;
    QLabel *pressure_label_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *pressure_lbl_ = nullptr;
    bool is_english_ = false;
};

class HmpPanel : public QWidget
{
    Q_OBJECT

public:
    explicit HmpPanel(QWidget *parent = nullptr);
    void updateData(const VaporView::HmpData& hmp_data);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();
    QLabel *rate_label_ = nullptr;
    QLabel *humidity_label_ = nullptr;
    QLabel *temperature_label_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *temp_lbl_ = nullptr;
    QLabel *humidity_lbl_ = nullptr;
    bool is_english_ = false;
};

class LidarPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LidarPanel(QWidget *parent = nullptr);
    void updateData(const VaporView::LidarData& lidar_data);
    void updateRate(double hz);
    void setEnglish(bool english);

private:
    void setupUi();
    QLabel *rate_label_ = nullptr;
    QLabel *distance_label_ = nullptr;
    QLabel *strength_label_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *distance_lbl_ = nullptr;
    QLabel *strength_lbl_ = nullptr;
    bool is_english_ = false;
};

class TemperatureControllerPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TemperatureControllerPanel(QWidget *parent = nullptr);
    QWidget *titleStatusWidget() const;
    void updateData(const VaporView::TemperatureControllerData& controllerData);
    void updateRate(double hz);
    void setEnglish(bool english);
    void setCommandStatus(const QString& text, bool error = false);
    void refreshTopControlsLayout();
    void setOutputEnabledControl(quint8 channel, bool enabled);
    void markCommandPending(VaporView::CommandId command, const VaporView::TemperatureControllerCommand& payload);
    void clearCommandPending(VaporView::CommandId command, quint8 channel);

signals:
    void targetTemperatureRequested(quint8 channel, double celsius);
    void outputEnabledRequested(quint8 channel, bool enabled);
    void outputModeRequested(quint8 channel, quint16 mode);
    void maxOutputPercentRequested(quint8 channel, quint16 percent);
    void pidRequested(quint8 channel, quint32 kp, quint32 ki, quint32 kd);
    void autoPidRequested(quint8 channel, quint16 mode);
    void overtempUpperRequested(quint8 channel, double celsius);
    void overtempLowerRequested(quint8 channel, double celsius);
    void temperatureSlopeRequested(quint8 channel, double celsiusPerSecond);
    void startupDelayRequested(quint8 channel, quint16 seconds);
    void controllerModeRequested(quint16 mode);
    void deviceAddressRequested(quint16 address);
    void rs485BaudRequested(quint16 baudIndex);
    void overtempOutputModeRequested(quint16 mode);
    void sensorConfigRequested(const VaporView::TemperatureControllerCommand& command);
    void factoryResetRequested();

private:
    struct ChannelWidgets
    {
        QLabel *target_label_text = nullptr;
        QLabel *enable_label_text = nullptr;
        QLabel *mode_label_text = nullptr;
        QLabel *max_output_label_text = nullptr;
        QLabel *auto_pid_label_text = nullptr;
        QLabel *overtemp_upper_label_text = nullptr;
        QLabel *overtemp_lower_label_text = nullptr;
        QLabel *temperature_slope_label_text = nullptr;
        QLabel *startup_delay_label_text = nullptr;
        QLabel *sensor_resistance_label_text = nullptr;
        QLabel *sensor_model_label_text = nullptr;
        QLabel *ntc_r0_label_text = nullptr;
        QLabel *ntc_b_label_text = nullptr;
        QLabel *pt_r0_label_text = nullptr;
        QLabel *pt_a_label_text = nullptr;
        QLabel *pt_b_label_text = nullptr;
        QLabel *pt_c_label_text = nullptr;
        std::array<QLabel *, 8> polynomial_label_text{};
        QDoubleSpinBox *target_spin = nullptr;
        QPushButton *enable_switch = nullptr;
        QComboBox *mode_combo = nullptr;
        QSpinBox *max_output_spin = nullptr;
        QSpinBox *kp_spin = nullptr;
        QSpinBox *ki_spin = nullptr;
        QSpinBox *kd_spin = nullptr;
        QComboBox *auto_pid_combo = nullptr;
        QDoubleSpinBox *overtemp_upper_spin = nullptr;
        QDoubleSpinBox *overtemp_lower_spin = nullptr;
        QDoubleSpinBox *temperature_slope_spin = nullptr;
        QSpinBox *startup_delay_spin = nullptr;
        QLineEdit *sensor_resistance_edit = nullptr;
        QWidget *common_top_controls = nullptr;
        QWidget *common_top_leading_spacer = nullptr;
        QWidget *common_top_middle_spacer = nullptr;
        QWidget *common_top_mode_spacer = nullptr;
        QWidget *enable_field = nullptr;
        QWidget *auto_pid_field = nullptr;
        QWidget *sensor_model_field = nullptr;
        QWidget *sensor_model_selector = nullptr;
        QButtonGroup *sensor_model_group = nullptr;
        std::array<QRadioButton *, 4> sensor_model_radios{};
        QLineEdit *ntc_r0_edit = nullptr;
        QLineEdit *ntc_b_edit = nullptr;
        QLineEdit *pt_r0_edit = nullptr;
        QLineEdit *pt_a_edit = nullptr;
        QLineEdit *pt_b_edit = nullptr;
        QLineEdit *pt_c_edit = nullptr;
        std::array<QLineEdit *, 8> polynomial_edits{};
        QWidget *sensor_config_page = nullptr;
        QFrame *sensor_calibration_overlay = nullptr;
        QWidget *sensor_calibration_drawer = nullptr;
        QFrame *sensor_config_top_bar = nullptr;
        QPushButton *common_params_button = nullptr;
        QPushButton *advanced_params_button = nullptr;
        QPushButton *sensor_config_button = nullptr;
        QStackedWidget *config_sub_stack = nullptr;
        QWidget *sub_page_row = nullptr;
    };
    struct PendingChannelEdits
    {
        bool target_temperature = false;
        double target_temperature_c = std::numeric_limits<double>::quiet_NaN();
        bool output_mode = false;
        int output_mode_value = 0;
        bool max_output_percent = false;
        int max_output_percent_value = 0;
        bool pid = false;
        int kp = 0;
        int ki = 0;
        int kd = 0;
        bool auto_pid = false;
        int auto_pid_mode = 0;
        bool overtemp_upper = false;
        double overtemp_upper_c = std::numeric_limits<double>::quiet_NaN();
        bool overtemp_lower = false;
        double overtemp_lower_c = std::numeric_limits<double>::quiet_NaN();
        bool temperature_slope = false;
        double temperature_slope_c_per_s = std::numeric_limits<double>::quiet_NaN();
        bool startup_delay = false;
        int startup_delay_s = 3;
        bool sensor_config = false;
        VaporView::TemperatureControllerCommand sensor_config_value;
    };
    struct CommonWidgets
    {
        QLabel *address_label_text = nullptr;
        QLabel *rs485_baud_label_text = nullptr;
        QLabel *overtemp_output_label_text = nullptr;
        QLabel *internal_temperature_label_text = nullptr;
        QSpinBox *address_spin = nullptr;
        QComboBox *rs485_baud_combo = nullptr;
        QComboBox *overtemp_output_combo = nullptr;
        QLineEdit *internal_temperature_edit = nullptr;
        QPushButton *factory_reset_button = nullptr;
        QFrame *sub_top_bar = nullptr;
        QPushButton *common_params_button = nullptr;
        QPushButton *advanced_params_button = nullptr;
        QPushButton *sensor_config_button = nullptr;
    };
    struct PendingCommonEdits
    {
        bool device_address = false;
        int device_address_value = 1;
        bool rs485_baud = false;
        int rs485_baud_index = 1;
        bool overtemp_output_mode = false;
        int overtemp_output_mode_value = 1;
    };

    void setupUi();
    QWidget *createChannelTopControlsPage(int index);
    QWidget *createChannelPage(int index);
    QWidget *createChannelCommonParamsPage(int index);
    QWidget *createChannelAdvancedParamsPage(int index);
    QWidget *createChannelSensorConfigPage(int index);
    QWidget *createCommonSettingsPage();
    void selectChannel(int index);
    void selectChannelSubPage(int channelIndex, int subPageIndex);
    bool eventFilter(QObject *watched, QEvent *event) override;
    void alignChannelTopControlFields(int channelIndex);
    void placeControllerModeFieldInTopControls(int channelIndex, int subPageIndex);
    void alignCommonSettingsColumns(int channelIndex);
    void alignSensorCalibrationOverlay(int channelIndex);
    void updateCalibrationDrawerVisibility();
    void updateChannelStackMinimumHeight();
    void emitSensorConfigRequest(int index);
    void fitControllerModeComboWidth();
    void updateChannelTexts();
    void updateChannelData(int index, const VaporView::TemperatureControllerChannelData& channel, bool valid);
    int channelIndex(quint8 channel) const;

    QFrame *channel_top_bar_ = nullptr;
    QPushButton *channel_button_1_ = nullptr;
    QPushButton *channel_button_2_ = nullptr;
    QPushButton *common_settings_button_ = nullptr;
    QStackedWidget *channel_top_controls_stack_ = nullptr;
    QWidget *controller_mode_top_controls_ = nullptr;
    QStackedWidget *channel_stack_ = nullptr;
    QStackedWidget *sub_page_bar_stack_ = nullptr;
    TemperatureTrendPlotWidget *temperature_plot_ = nullptr;
    QWidget *temperature_plot_container_ = nullptr;
    QWidget *title_status_strip_ = nullptr;
    QWidget *controller_mode_field_ = nullptr;
    QLabel *rate_title_lbl_ = nullptr;
    QLabel *rate_label_ = nullptr;
    QLabel *internal_temperature_label_ = nullptr;
    QLabel *error_code_label_ = nullptr;
    QLabel *error_text_label_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *internal_temperature_lbl_ = nullptr;
    QLabel *error_code_lbl_ = nullptr;
    QLabel *controller_mode_lbl_ = nullptr;
    QComboBox *controller_mode_combo_ = nullptr;
    CommonWidgets common_{};
    std::array<ChannelWidgets, 2> channels_{};
    std::array<QVector<double>, 2> measured_temperature_history_{};
    std::array<QVector<double>, 2> measured_temperature_time_history_{};
    std::array<double, 2> target_temperature_by_channel_{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN()};
    std::array<PendingChannelEdits, 2> pending_channel_edits_{};
    PendingCommonEdits pending_common_edits_{};
    bool pending_controller_mode_ = false;
    int pending_controller_mode_value_ = 0;
    int selected_channel_index_ = 0;
    int selected_config_page_index_ = 0;
    int selected_channel_sub_page_index_ = 0;
    bool is_english_ = false;
};
