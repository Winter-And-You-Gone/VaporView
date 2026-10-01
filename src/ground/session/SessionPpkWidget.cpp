#include "ground/session/SessionPpkWidget.h"
#include "ppk/SessionNavigationSource.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace VaporView::Ground::SessionUi
{
using namespace Ppk;
namespace
{
constexpr std::array<int, 5> systemMasks{1, 4, 32, 8, 16};
}
QString ppkStatusText(const PpkStatus &status, bool hasSession, bool busy, bool english)
{
    auto text = [english](const char *en, const char *zh) { return QString::fromUtf8(english ? en : zh); };
    if (!hasSession)
        return text("No Session", "无会话");
    if (busy || status.state == "Processing")
        return text("Processing", "处理中");
    if (status.completed)
        return text("Completed", "已完成");
    if (status.state == "Failed")
        return text("Failed", "失败");
    if (status.ready())
        return text("Ready", "就绪");
    if (!status.roverAvailable && !status.baseAvailable && !status.navigationAvailable)
        return text("Not processed", "未处理");
    return text("Waiting for inputs", "等待输入数据");
}
SessionPpkWidget::SessionPpkWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("sessionPpkPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    session_label_ = new QLabel(this);
    session_label_->setObjectName("sessionPpkSessionLabel");
    session_label_->setWordWrap(true);
    session_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(session_label_);
    status_ = new QLabel(this);
    status_->setObjectName("sessionPpkStatus");
    status_->setWordWrap(true);
    quality_ = new QLabel(this);
    quality_->setObjectName("sessionPpkQuality");
    quality_->setWordWrap(true);
    quality_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_);
    auto makeGroup = [this, layout](QGroupBox *&group)
    {
        group = new QGroupBox(this);
        group->setObjectName(QStringLiteral("sensorGroupBox"));
        auto *card = new QVBoxLayout(group);
        card->setContentsMargins(12, 12, 12, 12);
        auto *title = new QLabel(group);
        title->setObjectName(QStringLiteral("sectionTitleLabel"));
        card->addWidget(title);
        auto *grid = new QGridLayout();
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(8);
        card->addLayout(grid);
        layout->addWidget(group);
        return grid;
    };
    auto *inputs = makeGroup(inputs_group_);
    base_ = new QPushButton(this);
    nav_ = new QPushButton(this);
    run_ = new QPushButton(this);
    clear_ = new QPushButton(this);
    cancel_ = new QPushButton(this);
    base_->setObjectName("sessionPpkBaseButton");
    nav_->setObjectName("sessionPpkNavButton");
    run_->setObjectName("sessionPpkRunButton");
    cancel_->setObjectName("sessionPpkCancelButton");
    clear_->setObjectName("sessionPpkClearButton");
    rover_status_ = new QLabel(this);
    base_status_ = new QLabel(this);
    navigation_status_ = new QLabel(this);
    rover_status_->setObjectName("sessionPpkRoverStatus");
    base_status_->setObjectName("sessionPpkBaseStatus");
    navigation_status_->setObjectName("sessionPpkNavigationStatus");
    inputs->addWidget(rover_status_, 0, 0);
    inputs->addWidget(base_status_, 1, 0);
    inputs->addWidget(base_, 1, 1);
    inputs->addWidget(navigation_status_, 2, 0);
    inputs->addWidget(nav_, 2, 1);
    inputs->setColumnStretch(0, 1);
    source_label_ = new QLabel(this);
    source_ = new QComboBox(this);
    source_->setObjectName("sessionPpkSourceCombo");
    source_->addItems({"Original", "PPK"});
    auto *settings = makeGroup(settings_group_);
    arm_label_ = new QLabel(this);
    mask_label_ = new QLabel(this);
    settings->addWidget(arm_label_, 0, 0, 1, 3);
    for (int i = 0; i < 3; ++i)
    {
        arm_[i] = new QDoubleSpinBox(this);
        arm_[i]->setRange(-100, 100);
        arm_[i]->setDecimals(4);
        arm_[i]->setPrefix(QStringLiteral("%1 ").arg(QChar('X' + i)));
        arm_[i]->setSuffix(" m");
        settings->addWidget(arm_[i], 1, i);
    }
    mask_ = new QDoubleSpinBox(this);
    mask_->setRange(0, 89);
    mask_->setSuffix(QStringLiteral("°"));
    receiver_label_ = new QLabel(this);
    receiver_ = new QSpinBox(this);
    receiver_->setRange(0, 255);
    frequencies_label_ = new QLabel(this);
    frequencies_ = new QSpinBox(this);
    frequencies_->setRange(1, 5);
    settings->addWidget(receiver_label_, 2, 0);
    settings->addWidget(frequencies_label_, 2, 1);
    settings->addWidget(mask_label_, 2, 2);
    settings->addWidget(receiver_, 3, 0);
    settings->addWidget(frequencies_, 3, 1);
    settings->addWidget(mask_, 3, 2);
    auto *systemOptions = new QWidget(this);
    auto *systemLayout = new QHBoxLayout(systemOptions);
    systemLayout->setContentsMargins(0, 0, 0, 0);
    const std::array<const char *, 5> names{"GPS", "GLONASS", "BDS", "Galileo", "QZSS"};
    for (int i = 0; i < 5; ++i)
    {
        systems_[i] = new QCheckBox(QString::fromLatin1(names[i]), this);
        systemLayout->addWidget(systems_[i]);
    }
    systemLayout->addStretch();
    settings->addWidget(systemOptions, 4, 0, 1, 3);
    auto *processing = makeGroup(processing_group_);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    processing->addWidget(run_, 0, 0);
    processing->addWidget(cancel_, 0, 1);
    processing->addWidget(clear_, 0, 2);
    processing->addWidget(progress_, 1, 0, 1, 3);
    auto *results = makeGroup(results_group_);
    results->addWidget(quality_, 0, 0);
    auto *navigation = makeGroup(navigation_group_);
    navigation->addWidget(source_label_, 0, 0);
    navigation->addWidget(source_, 0, 1);
    navigation->setColumnStretch(1, 1);
    layout->addStretch();
    QWidget::setTabOrder(base_, nav_);
    QWidget *previous = nav_;
    for (auto *edit : arm_)
    {
        QWidget::setTabOrder(previous, edit);
        previous = edit;
    }
    QWidget::setTabOrder(previous, receiver_);
    QWidget::setTabOrder(receiver_, frequencies_);
    QWidget::setTabOrder(frequencies_, mask_);
    previous = mask_;
    for (auto *check : systems_)
    {
        QWidget::setTabOrder(previous, check);
        previous = check;
    }
    QWidget::setTabOrder(previous, run_);
    QWidget::setTabOrder(run_, cancel_);
    QWidget::setTabOrder(cancel_, clear_);
    QWidget::setTabOrder(clear_, source_);
    connect(base_, &QPushButton::clicked, this, [this] { importFile(false); });
    connect(nav_, &QPushButton::clicked, this, [this] { importFile(true); });
    connect(run_, &QPushButton::clicked, this, &SessionPpkWidget::run);
    connect(cancel_, &QPushButton::clicked, this,
            [this]
            {
                if (cancel_flag_)
                    cancel_flag_->store(true);
                cancel_->setEnabled(false);
            });
    connect(clear_, &QPushButton::clicked, this,
            [this]
            {
                QString error;
                if (!PpkProcessor::clearResult(session_, &error))
                    QMessageBox::warning(this, textFor("PPK error", "PPK 错误"), error);
                else
                    SessionNavigationEvents::instance()->notify(QFileInfo(session_).absoluteFilePath());
                refresh();
            });
    connect(source_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index)
            {
                if (session_.isEmpty() || busy())
                    return;
                QString error;
                if (!setSessionNavigationSource(session_, index ? NavigationSource::Ppk : NavigationSource::Original,
                                                &error))
                    QMessageBox::warning(this, textFor("PPK error", "PPK 错误"), error);
                refresh();
            });
    connect(SessionNavigationEvents::instance(), &SessionNavigationEvents::changed, this,
            [this](const QString &session)
            {
                if (QFileInfo(session_).absoluteFilePath() == session)
                    refresh();
            });
    setEnglish(false);
}
SessionPpkWidget::~SessionPpkWidget()
{
    if (watcher_)
    {
        if (cancel_flag_)
            cancel_flag_->store(true);
        watcher_->disconnect(this);
        watcher_->waitForFinished();
    }
}
QString SessionPpkWidget::textFor(const char *en, const char *zh) const
{
    return QString::fromUtf8(english_ ? en : zh);
}
void SessionPpkWidget::setEnglish(bool english)
{
    english_ = english;
    auto setTitle = [](QGroupBox *group, const QString &text)
    {
        group->findChild<QLabel *>(QStringLiteral("sectionTitleLabel"))->setText(text);
    };
    setTitle(inputs_group_, textFor("Data sources", "数据源"));
    setTitle(settings_group_, textFor("Solver settings", "解算设置"));
    setTitle(processing_group_, textFor("Processing", "处理"));
    setTitle(results_group_, textFor("Solution quality", "解算结果"));
    setTitle(navigation_group_, textFor("Navigation", "导航"));
    base_->setText(textFor("Select Base OBS", "选择基站 OBS"));
    nav_->setText(textFor("Select NAV", "选择星历 NAV"));
    clear_->setText(textFor("Clear result", "清除结果"));
    cancel_->setText(textFor("Cancel", "取消"));
    source_label_->setText(textFor("Track source", "轨迹来源"));
    arm_label_->setText(textFor("Main antenna arm", "主天线杆臂"));
    mask_label_->setText(textFor("Elevation mask", "高度角截止"));
    receiver_label_->setText(textFor("Receiver", "接收机编号"));
    frequencies_label_->setText(textFor("Frequencies", "频数"));
    arm_label_->setToolTip(
        textFor("IMU to main antenna: X forward, Y right, Z down. Recording snapshots the configured EPSILON arm.",
                "IMU 指向主天线：X 前向、Y 右向、Z 下向。录制时保存已配置的 EPSILON 杆臂。"));
    {
        QSignalBlocker blocker(source_);
        source_->setItemText(0, textFor("Original", "原始"));
        source_->setItemText(1, textFor("PPK corrected", "PPK 修正"));
    }
    refresh();
}
void SessionPpkWidget::setSessionDirectory(const QString &session)
{
    if (busy())
        return;
    session_ = session;
    operation_error_.clear();
    const auto config = PpkProcessor::loadConfig(session);
    for (int i = 0; i < 3; ++i)
        arm_[i]->setValue(config.imuToAntennaBodyM[i]);
    mask_->setValue(config.elevationMaskDeg);
    receiver_->setValue(config.receiver);
    frequencies_->setValue(config.frequencies);
    for (int i = 0; i < 5; ++i)
        systems_[i]->setChecked(config.constellations & systemMasks[i]);
    refresh();
}
void SessionPpkWidget::refresh()
{
    const auto status = session_.isEmpty() ? PpkStatus{} : PpkProcessor::status(session_);
    const bool enabled = !session_.isEmpty() && !busy();
    auto available = [&](bool value) { return textFor(value ? "available" : "missing", value ? "已有" : "缺少"); };
    const QString state = busy() && !processing_ ? textFor("Importing", "导入中")
        : !operation_error_.isEmpty() ? textFor("Failed", "失败")
        : ppkStatusText(status, !session_.isEmpty(), busy(), english_);
    session_label_->setText(session_.isEmpty() ? textFor("No Session", "无会话")
        : QStringLiteral("%1: %2").arg(textFor("Session", "会话"), QDir::toNativeSeparators(session_)));
    status_->setText(QStringLiteral("%1: %2").arg(textFor("PPK Status", "PPK 状态"), state));
    rover_status_->setText(QStringLiteral("%1: %2").arg(textFor("Rover Observation", "移动站观测"), available(status.roverAvailable)));
    base_status_->setText(QStringLiteral("%1: %2").arg(textFor("Base Observation", "基站观测"), available(status.baseAvailable)));
    navigation_status_->setText(QStringLiteral("%1: %2").arg(textFor("Navigation", "导航星历"), available(status.navigationAvailable)));
    const auto q = status.quality;
    QString summary = operation_error_.isEmpty() ? status.error : operation_error_;
    if (summary.isEmpty() && status.completed)
    {
        auto time = [](const QJsonValue &ns)
        {
            return QDateTime::fromMSecsSinceEpoch(ns.toString().toULongLong() / 1000000, Qt::UTC).toString(Qt::ISODate);
        };
        summary = QStringLiteral("FIX %1 (%2%) · FLOAT %3 (%4%) · %5 %6 · %7 — %8\nσ RMS N/E/U: %9 / %10 / %11 m · %12")
                      .arg(q.value("fix_count").toInt())
                      .arg(q.value("fix_percent").toDouble(), 0, 'f', 1)
                      .arg(q.value("float_count").toInt())
                      .arg(q.value("float_percent").toDouble(), 0, 'f', 1)
                      .arg(textFor("samples", "样本"))
                      .arg(q.value("sample_count").toInt())
                      .arg(time(q.value("start_utc_ns")), time(q.value("end_utc_ns")))
                      .arg(q.value("rms_sd_n").toDouble(), 0, 'f', 4)
                      .arg(q.value("rms_sd_e").toDouble(), 0, 'f', 4)
                      .arg(q.value("rms_sd_u").toDouble(), 0, 'f', 4)
                      .arg(QDir(session_).filePath(q.value("solution_file").toString()));
    }
    quality_->setText(summary);
    results_group_->setVisible(!summary.isEmpty());
    base_->setEnabled(enabled);
    nav_->setEnabled(enabled);
    run_->setEnabled(enabled && status.ready());
    run_->setText(status.completed ? textFor("Re-run PPK", "重新运行 PPK") : textFor("Run PPK", "运行 PPK"));
    clear_->setEnabled(enabled && (status.completed || !status.error.isEmpty()));
    source_->setEnabled(enabled);
    auto *model = qobject_cast<QStandardItemModel *>(source_->model());
    if (model)
        model->item(1)->setEnabled(status.completed);
    {
        QSignalBlocker blocker(source_);
        source_->setCurrentIndex(sessionNavigationSource(session_) == NavigationSource::Ppk ? 1 : 0);
    }
    for (auto *edit : arm_)
        edit->setEnabled(enabled);
    mask_->setEnabled(enabled);
    for (auto *check : systems_)
        check->setEnabled(enabled);
    receiver_->setEnabled(enabled);
    frequencies_->setEnabled(enabled);
    progress_->setVisible(busy());
    cancel_->setVisible(busy());
    cancel_->setEnabled(busy() && processing_);
}
void SessionPpkWidget::startWork(const std::function<PpkProcessResult()> &work, bool processing)
{
    if (busy())
        return;
    processing_ = processing;
    operation_error_.clear();
    watcher_ = new QFutureWatcher<PpkProcessResult>(this);
    auto *watcher = watcher_;
    connect(watcher, &QFutureWatcher<PpkProcessResult>::finished, this,
            [this, watcher]
            {
                const auto result = watcher->result();
                watcher_ = nullptr;
                watcher->deleteLater();
                operation_error_ = !result.success && !result.cancelled ? result.error : QString();
                refresh();
                emit busyChanged(false);
                if (!result.success && !result.cancelled)
                    QMessageBox::warning(this, textFor("PPK error", "PPK 错误"), result.error);
                if (result.success)
                    SessionNavigationEvents::instance()->notify(QFileInfo(session_).absoluteFilePath());
            });
    progress_->setValue(0);
    refresh();
    emit busyChanged(true);
    watcher->setFuture(QtConcurrent::run(work));
}
void SessionPpkWidget::importFile(bool navigation)
{
    const QStringList filenames =
        QFileDialog::getOpenFileNames(this,
                                      navigation ? textFor("Navigation RINEX", "导航星历 RINEX")
                                                 : textFor("Base observation RINEX", "基站观测 RINEX"),
                                      {}, textFor("RINEX files (*)", "RINEX 文件 (*)"));
    if (filenames.isEmpty())
        return;
    const QString session = session_;
    startWork(
        [session, filenames, navigation]
        {
            PpkProcessResult result;
            result.success = true;
            for (const auto &filename : filenames)
            {
                result.success = navigation ? PpkProcessor::importNavigation(session, filename, &result.error)
                                            : PpkProcessor::importBase(session, filename, &result.error);
                if (!result.success || !navigation)
                    break;
            }
            return result;
        },
        false);
}
void SessionPpkWidget::run()
{
    auto config = PpkProcessor::loadConfig(session_);
    for (int i = 0; i < 3; ++i)
        config.imuToAntennaBodyM[i] = arm_[i]->value();
    config.elevationMaskDeg = mask_->value();
    config.constellations = 0;
    config.receiver = receiver_->value();
    config.frequencies = frequencies_->value();
    for (int i = 0; i < 5; ++i)
        if (systems_[i]->isChecked())
            config.constellations |= systemMasks[i];
    cancel_flag_ = std::make_shared<std::atomic_bool>(false);
    auto cancel = cancel_flag_;
    const QString session = session_;
    const QPointer<SessionPpkWidget> self(this);
    startWork(
        [session, config, cancel, self]
        {
            return PpkProcessor::process(session, config, cancel.get(),
                                         [self](int percent, const QString &message)
                                         {
                                             if (self)
                                                 QMetaObject::invokeMethod(
                                                     self,
                                                     [self, percent, message]
                                                     {
                                                         if (self)
                                                         {
                                                             self->progress_->setValue(percent);
                                                             self->progress_->setToolTip(message);
                                                         }
                                                     },
                                                     Qt::QueuedConnection);
                                         });
        },
        true);
}
} // namespace VaporView::Ground::SessionUi
