#include "EpsilonSettings.h"
#include "EpsilonMaintenance.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iomanip>
#include <locale>
#include <regex>
#include <sstream>

namespace VaporView
{
namespace
{
constexpr int CommandIntervalMs = 1500; // FDI_config.c vendor interval.

EpsilonParameterDescriptor real(const char* name, const char* zh, const char* en,
                                const char* unit, double minimum, double maximum)
{
    return {name, EpsilonSettingsGroup::Installation, zh, en, unit,
            EpsilonParameterKind::Real, true, true, minimum, maximum, {}, true};
}

EpsilonParameterDescriptor aid(const char* name, const char* zh, const char* en)
{
    return {name, EpsilonSettingsGroup::Fusion, zh, en, "",
            EpsilonParameterKind::Boolean, true, true, 0, 1,
            {{0, "关闭", "Disabled"}, {1, "开启", "Enabled"}}, true};
}

EpsilonParameterDescriptor realIn(EpsilonSettingsGroup group, const char* name, const char* zh,
                                  const char* en, const char* unit, bool writable,
                                  bool ranged = false, double minimum = 0, double maximum = 0)
{
    return {name, group, zh, en, unit, EpsilonParameterKind::Real, writable,
            ranged, minimum, maximum, {}, true};
}

EpsilonParameterDescriptor enumeration(EpsilonSettingsGroup group, const char* name,
                                       const char* zh, const char* en, const char* unit,
                                       std::vector<EpsilonParameterOption> options,
                                       bool writable = true)
{
    const auto limits = std::minmax_element(options.begin(), options.end(),
        [](const EpsilonParameterOption& left, const EpsilonParameterOption& right) {
            return left.value < right.value;
        });
    return {name, group, zh, en, unit, EpsilonParameterKind::Enumeration, writable,
            true, limits.first->value, limits.second->value, std::move(options), true};
}

EpsilonParameterDescriptor protocol(const char* name, const char* zh, const char* en, bool writable = false)
{
    // EPSILON_SERIES enum in FDI_config.h and EPSILON vendor Info.ini agree.
    // Older generic manuals use a different Ublox value; do not use their
    // example numbering for EPSILON. Main is never a writable target.
    auto descriptor = enumeration(EpsilonSettingsGroup::Communication, name, zh, en, "",
        {{0,"None","None"},{2,"NAV","NAV"},{3,"RTCM","RTCM"},
         {4,"RTCM_EC600（依固件）","RTCM_EC600 (firmware dependent)"},
         {5,"NMEA（依固件）","NMEA (firmware dependent)"},
         {6,"NMEA 输出（依固件）","NMEA output (firmware dependent)"},
         {7,"NMEA2000 输出（依固件）","NMEA2000 output (firmware dependent)"},
         {8,"FDI CAN（依固件）","FDI CAN (firmware dependent)"},
         {9,"Ublox（依固件）","Ublox (firmware dependent)"},
         {10,"外部位置","External position"},{11,"外部速度","External velocity"},
         {12,"外部位置速度","External position and velocity"},{13,"外部姿态","External attitude"},
         {14,"外部时间","External time"},{15,"外部航向","External heading"},
         {16,"外部深度","External depth"},{17,"外部 SLAM1","External SLAM1"},
         {18,"外部 SLAM2","External SLAM2"},{19,"外部皮托压力","External pitot pressure"},
         {20,"外部空速","External air speed"},{21,"外部里程计","External odometer"},
         {22,"外部激光雷达","External LIDAR"},{23,"光流","Optical flow"},
         {24,"SCOUT MINI","SCOUT MINI"}}, writable);
    if (!writable)
        descriptor.options.insert(descriptor.options.begin() + 1, {1,"Main","Main"});
    return descriptor;
}

const std::vector<EpsilonParameterDescriptor> Installation = {
    // The vendor offset.ui permits +/-360 degrees. Antenna configuration UI
    // permits +/-1000 metres for both vectors; these are editing limits, not
    // statements about supported hardware installation dimensions.
    real("BODY_TO_VEHICLE_ALGN_ROLL", "安装横滚角", "Installation roll", "deg", -360, 360),
    real("BODY_TO_VEHICLE_ALGN_PITCH", "安装俯仰角", "Installation pitch", "deg", -360, 360),
    real("BODY_TO_VEHICLE_ALGN_YAW", "安装偏航角", "Installation yaw", "deg", -360, 360),
    real("GNSS_L_IMU_ANT1_X", "主天线杆臂 X", "Main antenna lever arm X", "m", -1000, 1000),
    real("GNSS_L_IMU_ANT1_Y", "主天线杆臂 Y", "Main antenna lever arm Y", "m", -1000, 1000),
    real("GNSS_L_IMU_ANT1_Z", "主天线杆臂 Z", "Main antenna lever arm Z", "m", -1000, 1000),
    real("GNSS_L_ANT2_ANT1_X", "副天线向量 X", "Secondary antenna vector X", "m", -1000, 1000),
    real("GNSS_L_ANT2_ANT1_Y", "副天线向量 Y", "Secondary antenna vector Y", "m", -1000, 1000),
    real("GNSS_L_ANT2_ANT1_Z", "副天线向量 Z", "Secondary antenna vector Z", "m", -1000, 1000),
    real("GNSS_ANTS_HEADING_BIAS", "双天线航向偏角", "Dual antenna heading bias", "deg", 0, 360),
    // Manual p218 specifies metres, but no numeric editing range.
    {"GNSS_L_ANTS_BASE_LINE", EpsilonSettingsGroup::Installation, "双天线基线（只读）",
     "Dual antenna baseline (read only)", "m", EpsilonParameterKind::Real, false,
     false, 0, 0, {}, true}
};

const std::vector<EpsilonParameterDescriptor> Fusion = {
    aid("AID_GNSS_POS_UPDATE", "GNSS 位置融合", "GNSS position aid"),
    aid("AID_GNSS_VEL_UPDATE", "GNSS 速度融合", "GNSS velocity aid"),
    aid("AID_GNSS_DUAL_ANT_HEADING_UPDATE", "双天线航向融合", "Dual antenna heading aid"),
    aid("AID_GNSS_TRACK_HEADING_UPDATE", "GNSS 航迹角融合", "GNSS track heading aid"),
    aid("AID_MAG_2D_MAGNETIC", "磁力计 2D 融合", "2D magnetic aid"),
    aid("AID_MAG_3D_MAGNETIC", "磁力计 3D 融合", "3D magnetic aid"),
    aid("AID_MAG_V_MAGNETIC", "磁力计 3D 融合（MAG_V）", "3D magnetic aid (MAG_V)"),
    aid("AID_INIT_YAW_USE_MAG", "磁力计初始化航向", "Initial yaw from magnetometer"),
    aid("AID_ZERO_RATE_UPDATE", "静止零角速度辅助", "Zero rate aid"),
    aid("AID_ZERO_VEL_UPDATE", "静止零速度辅助", "Zero velocity aid"),
    aid("AID_ZERO_POS_UPDATE", "静止零位置辅助", "Zero position aid"),
    // Both spellings occur in authoritative sources. Query separately; never
    // substitute one key for the other when writing a device.
    aid("AID_GYO_TRUN_ON_TARE_ENABLED", "开机陀螺零偏估计（TRUN）", "Startup gyro tare (TRUN)"),
    aid("AID_GYO_TURN_ON_TARE_ENABLED", "开机陀螺零偏估计（TURN）", "Startup gyro tare (TURN)"),
    // Info.ini gives the enum numbers; manual p55 says other models remain in
    // development. Only general and automotive are exposed for editing.
    {"DYNAMICS_MODEL", EpsilonSettingsGroup::Fusion, "载体动力学模型", "Dynamics model", "",
     EpsilonParameterKind::Enumeration, true, true, 0, 1,
     {{0, "通用模型", "General"}, {1, "汽车模型（无侧滑）", "Automotive (no sideslip)"}}, true}
};

const std::vector<EpsilonParameterDescriptor> Communication = {
    enumeration(EpsilonSettingsGroup::Communication, "COMM_BAUD1", "COMM1 波特率（只读）", "COMM1 baud (read only)", "bps",
                {{1,"9600","9600"},{2,"19200","19200"},{3,"38400","38400"},{4,"76800","76800"},
                 {5,"115200","115200"},{6,"230400","230400"},{7,"460800","460800"},{8,"921600","921600"}}, false),
    enumeration(EpsilonSettingsGroup::Communication, "COMM_BAUD2", "COMM2 波特率", "COMM2 baud", "",
                {{1,"9600","9600"},{2,"19200","19200"},{3,"38400","38400"},{4,"76800","76800"},
                 {5,"115200","115200"},{6,"230400","230400"},{7,"460800","460800"},{8,"921600","921600"}}),
    enumeration(EpsilonSettingsGroup::Communication, "COMM_BAUD3", "COMM3 波特率", "COMM3 baud", "",
                {{1,"9600","9600"},{2,"19200","19200"},{3,"38400","38400"},{4,"76800","76800"},
                 {5,"115200","115200"},{6,"230400","230400"},{7,"460800","460800"},{8,"921600","921600"}}),
    enumeration(EpsilonSettingsGroup::Communication, "COMM_BAUD4", "COMM4 波特率", "COMM4 baud", "",
                {{1,"9600","9600"},{2,"19200","19200"},{3,"38400","38400"},{4,"76800","76800"},
                 {5,"115200","115200"},{6,"230400","230400"},{7,"460800","460800"},{8,"921600","921600"}}),
    enumeration(EpsilonSettingsGroup::Communication, "COMM_BAUD5", "COMM5 CAN 波特率", "COMM5 CAN baud", "",
                {{13,"250000","250000"},{14,"500000","500000"},{15,"1000000","1000000"}}),
    // Preserve COMM1 and whichever port currently runs Main. Baud writes on
    // other ports additionally query their protocol before any settings write.
    protocol("COMM_STREAM_TYP1", "COMM1 协议（只读）", "COMM1 protocol (read only)"),
    protocol("COMM_STREAM_TYP2", "COMM2 协议", "COMM2 protocol", true),
    protocol("COMM_STREAM_TYP3", "COMM3 协议", "COMM3 protocol", true),
    protocol("COMM_STREAM_TYP4", "COMM4 协议", "COMM4 protocol", true),
    protocol("COMM_STREAM_TYP5", "COMM5 协议（只读）", "COMM5 protocol (read only)"),
    // This parameter is an output-rate enum, not an FDILink packet ID. Keep it
    // in the named parameter path; the existing #fmsg packet model is unchanged.
    // Info.ini MSG_OUT_NMEA: 0/1/2/4/5/6/7/8/10/11 correspond to off and Hz.
    enumeration(EpsilonSettingsGroup::Communication, "MSG_OUT_NMEA", "NMEA 输出频率", "NMEA output rate", "Hz",
        {{0,"关闭","Disabled"},{1,"1 Hz","1 Hz"},{2,"2 Hz","2 Hz"},{4,"5 Hz","5 Hz"},
         {5,"10 Hz","10 Hz"},{6,"20 Hz","20 Hz"},{7,"50 Hz","50 Hz"},{8,"100 Hz","100 Hz"},
         {10,"250 Hz","250 Hz"},{11,"500 Hz","500 Hz"}})
};

const std::vector<EpsilonParameterDescriptor> Filters = {
    {"FILT_LPF_CUTOFF_FREQ_ACC_XY", EpsilonSettingsGroup::Filters, "加速度计 XY 低通截止频率", "Accelerometer XY LPF cutoff", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_LPF_CUTOFF_FREQ_ACC_Z", EpsilonSettingsGroup::Filters, "加速度计 Z 低通截止频率", "Accelerometer Z LPF cutoff", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_LPF_CUTOFF_FREQ_GYRO_XY", EpsilonSettingsGroup::Filters, "陀螺仪 XY 低通截止频率", "Gyro XY LPF cutoff", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_LPF_CUTOFF_FREQ_GYRO_Z", EpsilonSettingsGroup::Filters, "陀螺仪 Z 低通截止频率", "Gyro Z LPF cutoff", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_BLP_ORDER", EpsilonSettingsGroup::Filters, "BLP 阶数", "BLP order", "", EpsilonParameterKind::Enumeration, true, true, 2, 4, {{2,"2","2"},{3,"3","3"},{4,"4","4"}}, true},
    {"FILT_BLP_CUTOFF_FREQ_ACC_XY", EpsilonSettingsGroup::Filters, "加速度计 XY BLP 截止频率", "Accelerometer XY BLP cutoff", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_BLP_CUTOFF_FREQ_ACC_Z", EpsilonSettingsGroup::Filters, "加速度计 Z BLP 截止频率", "Accelerometer Z BLP cutoff", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_BLP_CUTOFF_FREQ_GYRO_XY", EpsilonSettingsGroup::Filters, "陀螺仪 XY BLP 截止频率", "Gyro XY BLP cutoff", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_BLP_CUTOFF_FREQ_GYRO_Z", EpsilonSettingsGroup::Filters, "陀螺仪 Z BLP 截止频率", "Gyro Z BLP cutoff", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_NOTCH_CENTER_FREQUENCY", EpsilonSettingsGroup::Filters, "陷波1中心频率", "Notch1 center", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_NOTCH_CUTOFF_FREQUENCY", EpsilonSettingsGroup::Filters, "陷波1截止频率", "Notch1 cutoff", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_NOTCH2_CENTER_FREQUENCY", EpsilonSettingsGroup::Filters, "陷波2中心频率", "Notch2 center", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true},
    {"FILT_NOTCH2_CUTOFF_FREQUENCY", EpsilonSettingsGroup::Filters, "陷波2截止频率", "Notch2 cutoff", "Hz", EpsilonParameterKind::Real, true, false, 0, 0, {}, true}
};

const std::vector<EpsilonParameterDescriptor> Sensors = {
    enumeration(EpsilonSettingsGroup::Sensors, "IMU_RANGE_ACC", "加速度计量程", "Accelerometer range", "",
                {{1,"2g","2g"},{2,"4g","4g"},{0,"8g","8g"},{3,"16g","16g"}}),
    enumeration(EpsilonSettingsGroup::Sensors, "IMU_RANGE_GYO", "陀螺仪量程", "Gyro range", "",
                {{1,"15.63 deg/s","15.63 deg/s"},{2,"31.25 deg/s","31.25 deg/s"},{3,"62.5 deg/s","62.5 deg/s"},
                 {4,"125 deg/s","125 deg/s"},{5,"250 deg/s","250 deg/s"},{6,"500 deg/s","500 deg/s"},
                 {7,"1000 deg/s","1000 deg/s"},{0,"2000 deg/s","2000 deg/s"}}),
    enumeration(EpsilonSettingsGroup::Sensors, "GPIO_1_FUNCTION", "GPIO1（内部 GNSS PPS，只读）", "GPIO1 (internal GNSS PPS, read only)", "",
                {{0,"未启用","Inactive"},{1,"1PPS 输出","1PPS output"},{2,"1PPS 输入","1PPS input"}}, false),
    enumeration(EpsilonSettingsGroup::Sensors, "GPIO_2_FUNCTION", "GPIO2 功能", "GPIO2 function", "",
                {{0,"未启用","Inactive"},{1,"1PPS 输出","1PPS output"},{2,"1PPS 输入","1PPS input"}})
};

const std::vector<EpsilonParameterDescriptor> InitialState = {
    realIn(EpsilonSettingsGroup::InitialState, "USER_DEFINE_ROLL", "初始横滚角", "Initial roll", "deg", true, true, -360, 360),
    realIn(EpsilonSettingsGroup::InitialState, "USER_DEFINE_PITCH", "初始俯仰角", "Initial pitch", "deg", true, true, -360, 360),
    realIn(EpsilonSettingsGroup::InitialState, "USER_DEFINE_YAW", "初始偏航角", "Initial yaw", "deg", true, true, 0, 360),
    realIn(EpsilonSettingsGroup::InitialState, "USER_DEFINE_VELN", "初始北向速度", "Initial north velocity", "m/s", true),
    realIn(EpsilonSettingsGroup::InitialState, "USER_DEFINE_VELE", "初始东向速度", "Initial east velocity", "m/s", true),
    realIn(EpsilonSettingsGroup::InitialState, "USER_DEFINE_VELD", "初始地向速度", "Initial down velocity", "m/s", true),
    {"USER_DEFINE_HOLDLAT_1", EpsilonSettingsGroup::InitialState, "原点纬度整数（只读）", "Origin latitude integer (read only)", "deg", EpsilonParameterKind::Real, false, false, 0, 0, {}, true},
    {"USER_DEFINE_HOLDLAT_2", EpsilonSettingsGroup::InitialState, "原点纬度小数（只读）", "Origin latitude fraction (read only)", "1e-8 deg", EpsilonParameterKind::Real, false, false, 0, 0, {}, true},
    {"USER_DEFINE_HOLDLON_1", EpsilonSettingsGroup::InitialState, "原点经度整数（只读）", "Origin longitude integer (read only)", "deg", EpsilonParameterKind::Real, false, false, 0, 0, {}, true},
    {"USER_DEFINE_HOLDLON_2", EpsilonSettingsGroup::InitialState, "原点经度小数（只读）", "Origin longitude fraction (read only)", "1e-8 deg", EpsilonParameterKind::Real, false, false, 0, 0, {}, true}
};

const std::vector<EpsilonParameterDescriptor> ReferencePoint = {
    realIn(EpsilonSettingsGroup::ReferencePoint, "USER_DEFINE_L_IMU_POINT_X", "用户参考点 X", "User reference point X", "m", true),
    realIn(EpsilonSettingsGroup::ReferencePoint, "USER_DEFINE_L_IMU_POINT_Y", "用户参考点 Y", "User reference point Y", "m", true),
    realIn(EpsilonSettingsGroup::ReferencePoint, "USER_DEFINE_L_IMU_POINT_Z", "用户参考点 Z", "User reference point Z", "m", true)
};

EpsilonParameterDescriptor externalAid(const char* name, const char* zh, const char* en)
{
    auto descriptor = aid(name, zh, en);
    descriptor.group = EpsilonSettingsGroup::ExternalAids;
    return descriptor;
}

// These parameters are exposed by the vendor Odom user page, not SPKF
// internal filter state. Calibration commands/estimation-option bitfields are
// excluded: setting them as ordinary values would start an undocumented flow.
const std::vector<EpsilonParameterDescriptor> ExternalAids = {
    enumeration(EpsilonSettingsGroup::ExternalAids, "ODOM_TYPE", "里程计输入类型", "Odometer input type", "",
        {{0,"单码盘脉冲","Single encoder"},{1,"双码盘脉冲","Dual wheel encoder"},
         {2,"单轮速度输入","Single velocity input"},{3,"双轮速度输入","Dual velocity input"}}),
    realIn(EpsilonSettingsGroup::ExternalAids, "ODOM_SCAL1", "左轮/单轮刻度因子", "Left/single wheel scale", "ratio", true),
    realIn(EpsilonSettingsGroup::ExternalAids, "ODOM_SCAL2", "右轮刻度因子", "Right wheel scale", "ratio", true),
    realIn(EpsilonSettingsGroup::ExternalAids, "ODOM_L_IMU_ODOM_X", "IMU 到里程计中心 X", "IMU to odometer center X", "m", true),
    realIn(EpsilonSettingsGroup::ExternalAids, "ODOM_L_IMU_ODOM_Y", "IMU 到里程计中心 Y", "IMU to odometer center Y", "m", true),
    realIn(EpsilonSettingsGroup::ExternalAids, "ODOM_L_IMU_ODOM_Z", "IMU 到里程计中心 Z", "IMU to odometer center Z", "m", true),
    realIn(EpsilonSettingsGroup::ExternalAids, "ODOM_IMU_ALGN_PITCH", "里程计安装俯仰偏差", "Odometer alignment pitch", "deg", true, true, -360, 360),
    realIn(EpsilonSettingsGroup::ExternalAids, "ODOM_IMU_ALGN_YAW", "里程计安装偏航偏差", "Odometer alignment yaw", "deg", true, true, -360, 358),
    externalAid("AID_ODOMETER_VEL_UPDATE", "里程计速度融合", "Odometer velocity aid"),
    externalAid("AID_EXT_HEADING_UPDATE", "外部航向融合", "External heading aid"),
    externalAid("AID_EXT_POS_VEL_UPDATE", "外部位置速度融合", "External position/velocity aid"),
    externalAid("AID_EXT_SLAM1_UPDATE", "外部 SLAM1 融合", "External SLAM1 aid"),
    externalAid("AID_CAR_YZ_ZERO_VEL_NHC_ENABLED", "汽车侧向垂向非完整性约束", "Automotive lateral/vertical NHC"),
    externalAid("AID_CAR_CENT_ACCEL_NHC_ENABLED", "汽车向心加速度补偿", "Automotive centripetal compensation")
};

const std::vector<EpsilonSettingsGroup> Groups = {
    EpsilonSettingsGroup::Installation, EpsilonSettingsGroup::Fusion,
    EpsilonSettingsGroup::Communication, EpsilonSettingsGroup::Filters,
    EpsilonSettingsGroup::Sensors, EpsilonSettingsGroup::InitialState,
    EpsilonSettingsGroup::ReferencePoint, EpsilonSettingsGroup::ExternalAids};

bool validGroup(EpsilonSettingsGroup group)
{
    return std::find(Groups.begin(), Groups.end(), group) != Groups.end();
}

bool accepted(const std::string& response)
{
    static const std::regex ok(R"((^|[\r\n])\s*\*#OK\s*([\r\n]|$))");
    static const std::regex failure("(error|failed|unsupported)", std::regex::icase);
    return std::regex_search(response, ok) && !std::regex_search(response, failure);
}

bool acceptedDgnss(const std::string& response)
{
    static const std::regex ok(R"((^|[\r\n])\s*\*#OK\s*([\r\n]|$))");
    static const std::regex failure(R"((^|[\r\n])\s*(?:\*#ERROR[^\r\n]*|ERROR\b[^\r\n]*|FAILED\b[^\r\n]*|UNSUPPORTED\b[^\r\n]*)\s*([\r\n]|$))", std::regex::icase);
    return std::regex_search(response, ok) && !std::regex_search(response, failure);
}

std::string formatValue(double value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(12) << value;
    return stream.str();
}

// Every attempted entry is paired with an exit, including an ambiguous entry
// response and exceptions. A failed exit is surfaced rather than hidden.
class ConfigurationScope
{
public:
    ConfigurationScope(const EpsilonSettingsExchange& exchange, std::string& error)
        : exchange_(exchange), error_(error) {}
    ~ConfigurationScope() { close(); }
    bool enter()
    {
        attempted_ = true;
        return accepted(exchange_("#fconfig\r\n", CommandIntervalMs));
    }
    bool close()
    {
        if (!attempted_)
            return true;
        attempted_ = false;
        try
        {
            if (accepted(exchange_("#fdeconfig\r\n", CommandIntervalMs)))
                return true;
        }
        catch (...) {}
        if (!error_.empty())
            error_ += "; ";
        error_ += "EPSILON configuration mode exit was not acknowledged; reconnect device";
        return false;
    }
private:
    const EpsilonSettingsExchange& exchange_;
    std::string& error_;
    bool attempted_ = false;
};

bool query(const EpsilonSettingsExchange& exchange, const std::string& name, double& value)
{
    return parseEpsilonParameterResponse(exchange("#fparam get " + name + "\r\n", CommandIntervalMs), name, value);
}
} // namespace

const std::vector<EpsilonParameterDescriptor>& epsilonParameterDescriptors(EpsilonSettingsGroup group)
{
    static const std::vector<EpsilonParameterDescriptor> empty;
    switch (group)
    {
    case EpsilonSettingsGroup::Installation: return Installation;
    case EpsilonSettingsGroup::Fusion: return Fusion;
    case EpsilonSettingsGroup::Communication: return Communication;
    case EpsilonSettingsGroup::Filters: return Filters;
    case EpsilonSettingsGroup::Sensors: return Sensors;
    case EpsilonSettingsGroup::InitialState: return InitialState;
    case EpsilonSettingsGroup::ReferencePoint: return ReferencePoint;
    case EpsilonSettingsGroup::ExternalAids: return ExternalAids;
    }
    return empty;
}

const std::vector<EpsilonSettingsGroup>& epsilonSettingsGroups()
{
    return Groups;
}

bool isValidEpsilonSettingsGroup(EpsilonSettingsGroup group)
{
    return validGroup(group);
}

const EpsilonParameterDescriptor* epsilonParameterDescriptor(const std::string& name)
{
    for (const auto group : Groups)
        for (const auto& descriptor : epsilonParameterDescriptors(group))
            if (descriptor.name == name)
                return &descriptor;
    return nullptr;
}

bool validateEpsilonSettings(const EpsilonSettingsOperation& operation, std::string& error)
{
    error.clear();
    if (!validGroup(operation.group) || operation.values.empty())
    {
        error = "Invalid or empty EPSILON settings operation";
        return false;
    }
    for (const auto& entry : operation.values)
    {
        const auto* descriptor = epsilonParameterDescriptor(entry.first);
        if (!descriptor || descriptor->group != operation.group || !descriptor->writable)
        {
            error = "EPSILON parameter is not editable in this group: " + entry.first;
            return false;
        }
        const double value = entry.second;
        const bool outOfRange = descriptor->has_range && descriptor->kind == EpsilonParameterKind::Real &&
            (value < descriptor->minimum || value > descriptor->maximum);
        const bool filterNegative = descriptor->group == EpsilonSettingsGroup::Filters && value < 0;
        const bool invalidScale = (entry.first == "ODOM_SCAL1" || entry.first == "ODOM_SCAL2") && value <= 0;
        if (!std::isfinite(value) || outOfRange || filterNegative || invalidScale)
        {
            error = "EPSILON parameter value is outside the documented editing range: " + entry.first;
            return false;
        }
        if (descriptor->kind != EpsilonParameterKind::Real &&
            std::none_of(descriptor->options.begin(), descriptor->options.end(),
                         [value](const EpsilonParameterOption& option) { return option.value == value; }))
        {
            error = "EPSILON parameter enum is not supported: " + entry.first;
            return false;
        }
    }
    return true;
}

bool parseEpsilonParameterResponse(const std::string& response, const std::string& name, double& value)
{
    if (!epsilonParameterDescriptor(name))
        return false;
    static const std::regex failure("(error|failed|unsupported)", std::regex::icase);
    if (std::regex_search(response, failure))
        return false;
    const std::regex pattern("^\\s*" + name + R"(\s*=\s*([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)\s*$)");
    std::istringstream lines(response);
    std::string line;
    bool found = false;
    double parsed = 0;
    while (std::getline(lines, line))
    {
        std::smatch match;
        if (!std::regex_match(line, match, pattern))
            continue;
        std::istringstream number(match[1].str());
        number.imbue(std::locale::classic());
        double candidate = 0;
        if (!(number >> candidate) || !std::isfinite(candidate))
            return false;
        if (found && candidate != parsed)
            return false; // Ambiguous values are not a current snapshot.
        found = true;
        parsed = candidate;
    }
    if (found)
        value = parsed;
    return found;
}

bool epsilonSettingsValuesEqual(double left, double right)
{
    // Parameters are stored as floats in the vendor tool. Allow float rounding,
    // while preserving meaningful angular/metre changes near zero.
    return std::isfinite(left) && std::isfinite(right) &&
        std::abs(left - right) <= 1e-6 * std::max({1.0, std::abs(left), std::abs(right)});
}

bool readEpsilonSettings(EpsilonSettingsGroup group, const EpsilonSettingsExchange& exchange,
                         EpsilonSettingsSnapshot& snapshot, std::string& error)
{
    snapshot = {};
    snapshot.group = group;
    error.clear();
    if (!validGroup(group) || !exchange)
    {
        error = "Invalid EPSILON settings group or exchange";
        return false;
    }
    ConfigurationScope scope(exchange, error);
    try
    {
        if (!scope.enter())
            error = "EPSILON configuration mode entry was not acknowledged";
        else
        {
            for (const auto& descriptor : epsilonParameterDescriptors(group))
            {
                double value = 0;
                if (query(exchange, descriptor.name, value))
                    snapshot.values.emplace(descriptor.name, value);
                else
                    snapshot.unsupported.push_back(descriptor.name);
            }
            if (snapshot.values.empty())
                error = "No EPSILON settings could be read; unsupported keys are not zero values";
        }
    }
    catch (const std::exception& exception) { error = std::string("EPSILON settings read failed: ") + exception.what(); }
    catch (...) { error = "EPSILON settings read failed"; }
    const bool exited = scope.close();
    return exited && error.empty();
}

bool applyEpsilonSettings(const EpsilonSettingsOperation& operation,
                          const EpsilonSettingsExchange& exchange,
                          EpsilonSettingsSnapshot& snapshot, std::string& error)
{
    snapshot = {};
    snapshot.group = operation.group;
    if (!validateEpsilonSettings(operation, error))
        return false;
    if (!exchange)
    {
        error = "Invalid EPSILON settings exchange";
        return false;
    }
    ConfigurationScope scope(exchange, error);
    try
    {
        if (!scope.enter())
            error = "EPSILON configuration mode entry was not acknowledged";
        else
        {
            // Read every requested key before issuing any write. This rejects
            // firmware aliases, unreadable parameters and unsupported devices.
            for (const auto& entry : operation.values)
            {
                double current = 0;
                if (query(exchange, entry.first, current))
                    snapshot.values.emplace(entry.first, current);
                else
                    snapshot.unsupported.push_back(entry.first);
            }
            if (!snapshot.unsupported.empty())
                error = "EPSILON parameter could not be read; no settings were written: " + snapshot.unsupported.front();
            // Main may be assigned to any physical port. Never change its
            // baud using the generic parameter path, which cannot reconnect
            // at the new rate. Unknown/undocumented protocols are protected.
            if (error.empty() && operation.group == EpsilonSettingsGroup::Communication)
            {
                for (const auto& entry : operation.values)
                {
                    if (entry.first.find("COMM_BAUD") != 0 && entry.first.find("COMM_STREAM_TYP") != 0)
                        continue;
                    const std::string protocolName = entry.first.find("COMM_BAUD") == 0
                        ? "COMM_STREAM_TYP" + entry.first.substr(9) : entry.first;
                    double protocol = 0;
                    if (!query(exchange, protocolName, protocol) || protocol == 1 ||
                        protocol != std::floor(protocol) || protocol < 0 || protocol > 24)
                    {
                        error = "EPSILON communication port is Main or its protocol could not be safely identified; no settings were written: " + entry.first;
                        break;
                    }
                }
            }
            bool changed = false;
            if (error.empty())
            {
                for (const auto& entry : operation.values)
                {
                    if (epsilonSettingsValuesEqual(snapshot.values.at(entry.first), entry.second))
                        continue;
                    snapshot.values.erase(entry.first); // Acknowledgement is not a readback.
                    snapshot.restart_required = snapshot.restart_required ||
                        epsilonParameterDescriptor(entry.first)->restart_required;
                    if (!accepted(exchange("#fparam set " + entry.first + " " + formatValue(entry.second) + "\r\n", CommandIntervalMs)))
                    {
                        // The failed key may also have changed despite a lost
                        // response; exclude it rather than report an old value.
                        error = "EPSILON write was not acknowledged: " + entry.first +
                            "; partial changes may be active and have not been saved";
                        break;
                    }
                    changed = true;
                }
            }
            if (error.empty() && changed)
            {
                snapshot.saved = accepted(exchange("#fsave\r\n", CommandIntervalMs));
                if (!snapshot.saved)
                    error = "EPSILON save was not acknowledged; changes may be active but persistence is unverified";
            }
            if (error.empty())
            {
                bool verified = true;
                for (const auto& entry : operation.values)
                {
                    double actual = 0;
                    if (!query(exchange, entry.first, actual))
                    {
                        snapshot.values.erase(entry.first);
                        snapshot.unsupported.push_back(entry.first);
                        verified = false;
                    }
                    else
                    {
                        snapshot.values[entry.first] = actual;
                        verified = verified && epsilonSettingsValuesEqual(actual, entry.second);
                    }
                }
                snapshot.readback_verified = verified;
                if (!verified)
                    error = "EPSILON settings readback did not match every requested value";
            }
        }
    }
    catch (const std::exception& exception) { error = std::string("EPSILON settings apply failed; partial changes may be active: ") + exception.what(); }
    catch (...) { error = "EPSILON settings apply failed; partial changes may be active"; }
    const bool exited = scope.close();
    return exited && error.empty();
}
const std::vector<EpsilonDgnssDescriptor>& epsilonDgnssDescriptors()
{
    static const std::vector<EpsilonDgnssDescriptor> descriptors = {
        {"RTCM_TYPE", "差分类型（只读）", "Correction type (read only)", false, false},
        {"NET_INFO_IMEI", "IMEI", "IMEI", false, false}, {"NET_INFO_CCID", "CCID", "CCID", false, false},
        {"NTRIP_SVR_DOMAIN", "NTRIP 服务器", "NTRIP server", true, false},
        {"NTRIP_SVR_PORT", "NTRIP 端口", "NTRIP port", true, false},
        {"NTRIP_MOUNT", "NTRIP 挂载点", "NTRIP mountpoint", true, false},
        {"NTRIP_ACCOUNT", "NTRIP 账号", "NTRIP account", true, true},
        {"NTRIP_PASSWORD", "NTRIP 密码", "NTRIP password", true, true},
        {"FDI_AUTH", "FDI 云鉴权码", "FDI authorization", true, true}};
    return descriptors;
}

const EpsilonDgnssDescriptor* epsilonDgnssDescriptor(const std::string& name)
{
    for (const auto& descriptor : epsilonDgnssDescriptors())
        if (descriptor.name == name) return &descriptor;
    return nullptr;
}

bool validateEpsilonDgnss(const EpsilonDgnssOperation& operation, std::string& error)
{
    error.clear();
    if (operation.values.empty()) { error = "Empty EPSILON DGNSS operation"; return false; }
    for (const auto& entry : operation.values)
    {
        const auto* descriptor = epsilonDgnssDescriptor(entry.first);
        // ASCII token command interface: these are application input limits,
        // not claims about device storage capacity. Reject command injection.
        bool valid = descriptor && descriptor->writable && !entry.second.empty() && entry.second.size() <= 255;
        for (unsigned char character : entry.second)
            valid = valid && character > 32 && character < 127 && character != '#' && character != ';';
        if (entry.first == "NTRIP_SVR_PORT")
        {
            unsigned long port = 0;
            valid = valid && entry.second.size() <= 5;
            for (unsigned char character : entry.second)
            {
                valid = valid && character >= '0' && character <= '9';
                port = port * 10 + (character >= '0' && character <= '9' ? character - '0' : 0);
            }
            valid = valid && port > 0 && port <= 65535;
        }
        if (!valid) { error = "Invalid or non-editable EPSILON DGNSS field: " + entry.first; return false; }
    }
    return true;
}

bool parseEpsilonDgnssResponse(const std::string& response, const std::string& name, std::string& value)
{
    if (!epsilonDgnssDescriptor(name) || response.size() > 8192) return false;
    const std::regex failure(R"((^|[\r\n])\s*(?:\*#ERROR[^\r\n]*|ERROR\b[^\r\n]*|FAILED\b[^\r\n]*|UNSUPPORTED\b[^\r\n]*)\s*([\r\n]|$))", std::regex::icase);
    if (std::regex_search(response, failure)) return false;
    std::istringstream lines(response);
    std::string line, parsed;
    bool found = false;
    while (std::getline(lines, line))
    {
        const auto start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line.compare(start, name.size(), name) != 0) continue;
        size_t offset = start + name.size();
        offset = line.find_first_not_of(" \t", offset);
        if (offset == std::string::npos) continue;
        if (line.compare(offset, 3, "：") == 0) offset += 3;
        else if (line[offset] == ':' || line[offset] == '=') ++offset;
        else continue;
        const auto first = line.find_first_not_of(" \t\r", offset);
        const auto last = line.find_last_not_of(" \t\r");
        const std::string candidate = first == std::string::npos ? "" : line.substr(first, last - first + 1);
        if (found && parsed != candidate) return false;
        found = true; parsed = candidate;
    }
    if (found) value = parsed;
    return found;
}

bool readEpsilonDgnss(const EpsilonSettingsExchange& exchange, EpsilonDgnssSnapshot& snapshot, std::string& error)
{
    snapshot = {}; error.clear();
    if (!exchange) { error = "Invalid EPSILON DGNSS exchange"; return false; }
    ConfigurationScope scope(exchange, error);
    try
    {
        if (!scope.enter()) error = "EPSILON DGNSS configuration entry was not acknowledged";
        else for (const auto& descriptor : epsilonDgnssDescriptors())
        {
            std::string value;
            if (parseEpsilonDgnssResponse(exchange("#fdgnss get " + descriptor.name + "\r\n", 1500), descriptor.name, value))
                snapshot.values[descriptor.name] = value;
            else snapshot.unsupported.push_back(descriptor.name);
        }
        if (error.empty() && snapshot.values.empty()) error = "No D4G DGNSS values could be read";
    }
    catch (...) { error = "EPSILON DGNSS read failed"; }
    const bool exited = scope.close();
    return exited && error.empty();
}

bool applyEpsilonDgnss(const EpsilonDgnssOperation& operation, const EpsilonSettingsExchange& exchange,
                      EpsilonDgnssSnapshot& snapshot, std::string& error)
{
    snapshot = {};
    if (!validateEpsilonDgnss(operation, error)) return false;
    if (!exchange) { error = "Invalid EPSILON DGNSS exchange"; return false; }
    ConfigurationScope scope(exchange, error);
    try
    {
        if (!scope.enter()) error = "EPSILON DGNSS configuration entry was not acknowledged";
        else
        {
            for (const auto& entry : operation.values)
            {
                std::string value;
                if (parseEpsilonDgnssResponse(exchange("#fdgnss get " + entry.first + "\r\n", 1500), entry.first, value))
                    snapshot.values[entry.first] = value;
                else snapshot.unsupported.push_back(entry.first);
            }
            if (!snapshot.unsupported.empty()) error = "D4G DGNSS field could not be read; no values were written";
            bool changed = false;
            if (error.empty()) for (const auto& entry : operation.values)
            {
                if (snapshot.values.at(entry.first) == entry.second) continue;
                snapshot.values.erase(entry.first);
                snapshot.restart_required = true;
                if (!acceptedDgnss(exchange("#fdgnss set " + entry.first + " " + entry.second + "\r\n", 1500)))
                { error = "D4G DGNSS write failed; partial changes may be active and unsaved"; break; }
                changed = true;
            }
            if (error.empty() && changed)
            {
                snapshot.saved = accepted(exchange("#fsave\r\n", 1500));
                if (!snapshot.saved) error = "D4G DGNSS save was not acknowledged";
            }
            if (error.empty())
            {
                snapshot.readback_verified = true;
                for (const auto& entry : operation.values)
                {
                    std::string actual;
                    if (!parseEpsilonDgnssResponse(exchange("#fdgnss get " + entry.first + "\r\n", 1500), entry.first, actual))
                    { snapshot.values.erase(entry.first); snapshot.unsupported.push_back(entry.first); snapshot.readback_verified = false; }
                    else { snapshot.values[entry.first] = actual; snapshot.readback_verified &= actual == entry.second; }
                }
                if (!snapshot.readback_verified) error = "D4G DGNSS readback mismatch; persistence is unverified";
            }
        }
    }
    catch (...) { error = "D4G DGNSS apply failed; partial changes may be active"; }
    const bool exited = scope.close();
    return exited && error.empty();
}

bool runEpsilonMaintenance(EpsilonMaintenanceAction action,
                           const EpsilonMaintenanceExchange& exchange,
                           EpsilonMaintenanceResult& result, std::string& error,
                           EpsilonMaintenanceProgress progress, std::function<bool()> shouldCancel)
{
    result = {};
    result.action = action;
    error.clear();
    if (!validEpsilonMaintenanceAction(action) || !exchange)
    {
        error = "Invalid EPSILON maintenance action or exchange";
        result.error = error;
        return false;
    }
    if (action == EpsilonMaintenanceAction::Magnetic2D || action == EpsilonMaintenanceAction::Magnetic3D)
    {
        std::map<std::string, double> original;
        bool attempted = false, completed = false, cancelled = false, aidWriteAttempted = false;
        const auto send = [&](const std::string& command) { return exchange(command, 1500, {}); };
        try
        {
            attempted = true;
            if (!accepted(send("#fconfig\r\n"))) throw std::runtime_error("Magnetic configuration entry failed");
            for (const std::string key : {"AID_MAG_2D_MAGNETIC", "AID_MAG_3D_MAGNETIC", "AID_MAG_V_MAGNETIC"})
            {
                double value = 0;
                if (parseEpsilonParameterResponse(send("#fparam get " + key + "\r\n"), key, value) && (value == 0 || value == 1))
                    original[key] = value;
            }
            if (!original.count("AID_MAG_2D_MAGNETIC") ||
                (!original.count("AID_MAG_3D_MAGNETIC") && !original.count("AID_MAG_V_MAGNETIC")))
                throw std::runtime_error("Magnetic AID original values could not be read");
            if (original.count("AID_MAG_3D_MAGNETIC") && original.count("AID_MAG_V_MAGNETIC") &&
                original.at("AID_MAG_3D_MAGNETIC") != original.at("AID_MAG_V_MAGNETIC"))
                throw std::runtime_error("Both magnetic 3D aliases exist with conflicting values; refusing to guess firmware semantics");
            const auto writeAid = [&](const std::string& command) {
                aidWriteAttempted = true; // Ambiguous failed writes also require restoring originals.
                return exchange(command, 1500, [&result]() { result.restart_required = true; });
            };
            for (const auto& entry : original)
                if (entry.second != 0 && !accepted(writeAid("#fparam set " + entry.first + " 0\r\n")))
                    throw std::runtime_error("Magnetic AID disable failed");
            if (shouldCancel && shouldCancel()) cancelled = true;
            else
            {
                bool calibrationWritten = false;
                std::string buffer = exchange(action == EpsilonMaintenanceAction::Magnetic2D
                    ? "#fmagcal2d\r\n" : "#fmagcal3d\r\n", 1500,
                    [&result, &calibrationWritten]() {
                        calibrationWritten = true;
                        result.restart_required = true;
                    });
                if (!calibrationWritten) throw std::runtime_error("Magnetic calibration command was not sent");
                result.status = EpsilonMaintenanceStatus::Running;
                const std::regex failure("(failed|unsupported|\\*#ERROR)", std::regex::icase);
                const std::regex percent(R"(Now:\s*([0-9]{1,3})\s*percent\s*$)", std::regex::icase);
                const std::regex fitting(R"(^\s*The fitting error of the current calculation:\s*([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)\s*$)", std::regex::icase);
                const std::regex algorithm(R"(^\s*Calibration Algorithm:\s*(\w+)\s*$)", std::regex::icase);
                bool reportHasError = false;
                double reportError = 0;
                for (int poll = 0; poll < 300 && !completed; ++poll)
                {
                    if (shouldCancel && shouldCancel()) { cancelled = true; break; }
                    if (buffer.size() > 8192) throw std::runtime_error("Magnetic response exceeded 8 KB buffer limit");
                    size_t newline = 0;
                    while ((newline = buffer.find('\n')) != std::string::npos)
                    {
                        std::string line = buffer.substr(0, newline); buffer.erase(0, newline + 1);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (std::regex_search(line, failure)) throw std::runtime_error("Magnetic device reported failure");
                        std::smatch match;
                        if (action == EpsilonMaintenanceAction::Magnetic2D && std::regex_search(line, match, percent))
                        {
                            const int value = std::stoi(match[1].str());
                            if (value > 100) throw std::runtime_error("Invalid magnetic progress");
                            result.progress_known = true; result.progress_percent = value;
                            completed = value == 100;
                        }
                        if (action == EpsilonMaintenanceAction::Magnetic3D)
                        {
                            if (line.find("This is a magnetometer 3D calibration.") != std::string::npos)
                            {
                                reportHasError = false;
                                result.fit_error_known = false;
                                result.fit_error = 0;
                                result.algorithm.clear();
                            }
                            // A new fitting line starts a report and invalidates
                            // the old algorithm/error pair, including NaN text.
                            if (line.find("The fitting error of the current calculation:") != std::string::npos)
                            {
                                reportHasError = false; result.fit_error_known = false; result.fit_error = 0; result.algorithm.clear();
                                if (std::regex_match(line, match, fitting))
                                {
                                    reportError = std::stod(match[1].str());
                                    reportHasError = std::isfinite(reportError) && reportError >= 0;
                                    result.fit_error_known = reportHasError; result.fit_error = reportHasError ? reportError : 0;
                                }
                            }
                            else if (std::regex_match(line, match, algorithm))
                            {
                                result.algorithm = match[1].str();
                                completed = reportHasError && reportError < 3 && result.algorithm == "High";
                                reportHasError = false; // Never reuse an earlier report's error.
                            }
                        }
                        if (progress) progress(result);
                        if (completed) break;
                    }
                    if (!completed) buffer += exchange("", 1000, {});
                }
                if (!completed && !cancelled) error = "Magnetic calibration timed out; calibration result is unverified";
            }
        }
        catch (const std::exception& exception) { error = exception.what(); }
        catch (...) { error = "Magnetic calibration failed"; }
        // Completion auto-exits configuration. Cancellation only requests exit;
        // neither timeout nor cancellation proves the device stopped calibrating.
        if (attempted)
        {
            bool exitAcknowledged = completed;
            if (!completed)
            {
                try { exitAcknowledged = accepted(send("#fdeconfig\r\n")); }
                catch (...) {}
            }
            if (!exitAcknowledged)
            {
                if (!error.empty()) error += "; ";
                error += "Magnetic calibration exit request was not acknowledged; device state is unverified";
            }
            // An uncertain exit must not prevent restoring AIDs already changed.
            try
            {
                if (aidWriteAttempted && !original.empty())
                {
                    if (!accepted(send("#fconfig\r\n"))) throw std::runtime_error("AID restore entry failed");
                    for (const auto& entry : original)
                        if (!accepted(exchange("#fparam set " + entry.first + " " + formatValue(entry.second) + "\r\n", 1500,
                            [&result]() { result.restart_required = true; })))
                            throw std::runtime_error("Original magnetic AID restore failed");
                    if (!accepted(send("#fsave\r\n"))) throw std::runtime_error("Magnetic AID restore save failed");
                    if (!accepted(send("#fdeconfig\r\n"))) throw std::runtime_error("Magnetic AID restore exit failed");
                }
            }
            catch (...)
            {
                if (!error.empty()) error += "; ";
                error += "Original magnetic AID restoration failed; reconnect/read settings before use";
                try { send("#fdeconfig\r\n"); } catch (...) {}
            }
        }
        if (cancelled && error.empty()) error = "Magnetic cancellation requested; device stop and calibration result are unverified";
        result.saved = completed; // Device auto-save fact survives later AID restore failure.
        result.status = cancelled ? EpsilonMaintenanceStatus::Cancelled
            : completed && error.empty() ? EpsilonMaintenanceStatus::Completed : EpsilonMaintenanceStatus::Failed;
        result.error = error;
        if (progress) progress(result);
        return result.status == EpsilonMaintenanceStatus::Completed;
    }
    const std::string command = action == EpsilonMaintenanceAction::Level ? "#fimucal_level\r\n"
        : action == EpsilonMaintenanceAction::Accelerometer ? "#fimucal_acce\r\n" : "#fimucal_gyro\r\n";
    bool exited = false;
    try
    {
        if (!accepted(exchange("#fconfig\r\n", CommandIntervalMs, {})))
            error = "EPSILON maintenance configuration entry was not acknowledged";
        else
        {
            const auto response = exchange(command, CommandIntervalMs,
                [&result]() { result.restart_required = true; });
            const std::regex failure("(error|failed|unsupported)", std::regex::icase);
            if (!result.restart_required || std::regex_search(response, failure))
                error = "EPSILON static tare command was not sent or device reported failure";
            else
            {
                result.status = accepted(response) ? EpsilonMaintenanceStatus::Acknowledged
                                                   : EpsilonMaintenanceStatus::SentUnverified;
                result.saved = accepted(exchange("#fsave\r\n", CommandIntervalMs, {}));
                if (!result.saved)
                    error = "EPSILON static tare save was not acknowledged; persistence is unverified";
            }
        }
    }
    catch (const std::exception& exception) { error = std::string("EPSILON maintenance failed: ") + exception.what(); }
    catch (...) { error = "EPSILON maintenance failed"; }
    try
    {
        exited = accepted(exchange("#fdeconfig\r\n", CommandIntervalMs, {}));
    }
    catch (...) {}
    if (!exited)
    {
        if (!error.empty()) error += "; ";
        error += "EPSILON maintenance configuration exit was not acknowledged; reconnect device";
    }
    if (!error.empty())
    {
        result.status = EpsilonMaintenanceStatus::Failed;
        result.error = error;
    }
    return error.empty() && result.saved && exited;
}
} // namespace VaporView
