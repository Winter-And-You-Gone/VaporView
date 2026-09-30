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
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QSpinBox>
#include <QtConcurrent/QtConcurrentRun>

namespace VaporView::Ground::SessionUi
{
using namespace Ppk;
namespace
{
constexpr std::array<int, 5> systemMasks{1, 4, 32, 8, 16};
}
SessionPpkWidget::SessionPpkWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("sessionPpkPanel"));
    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setHorizontalSpacing(8);
    status_ = new QLabel(this);
    status_->setObjectName("sessionPpkStatus");
    status_->setWordWrap(true);
    quality_ = new QLabel(this);
    quality_->setObjectName("sessionPpkQuality");
    quality_->setWordWrap(true);
    quality_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(status_, 0, 0, 1, 6);
    layout->addWidget(quality_, 1, 0, 1, 6);
    base_ = new QPushButton(this);
    nav_ = new QPushButton(this);
    run_ = new QPushButton(this);
    clear_ = new QPushButton(this);
    cancel_ = new QPushButton(this);
    base_->setObjectName("sessionPpkBaseButton");
    nav_->setObjectName("sessionPpkNavButton");
    run_->setObjectName("sessionPpkRunButton");
    cancel_->setObjectName("sessionPpkCancelButton");
    source_label_ = new QLabel(this);
    source_ = new QComboBox(this);
    source_->setObjectName("sessionPpkSourceCombo");
    source_->addItems({"Original", "PPK"});
    layout->addWidget(base_, 2, 0);
    layout->addWidget(nav_, 2, 1);
    layout->addWidget(run_, 2, 2);
    layout->addWidget(clear_, 2, 3);
    layout->addWidget(source_label_, 2, 4);
    layout->addWidget(source_, 2, 5);
    arm_label_ = new QLabel(this);
    mask_label_ = new QLabel(this);
    auto *options = new QWidget(this);
    auto *optionsLayout = new QHBoxLayout(options);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->addWidget(arm_label_);
    for (int i = 0; i < 3; ++i)
    {
        arm_[i] = new QDoubleSpinBox(this);
        arm_[i]->setRange(-100, 100);
        arm_[i]->setDecimals(4);
        arm_[i]->setPrefix(QStringLiteral("%1 ").arg(QChar('X' + i)));
        arm_[i]->setSuffix(" m");
        optionsLayout->addWidget(arm_[i]);
    }
    optionsLayout->addWidget(mask_label_);
    mask_ = new QDoubleSpinBox(this);
    mask_->setRange(0, 89);
    mask_->setSuffix(QStringLiteral("°"));
    optionsLayout->addWidget(mask_);
    receiver_label_ = new QLabel(this);
    receiver_ = new QSpinBox(this);
    receiver_->setRange(0, 255);
    optionsLayout->addWidget(receiver_label_);
    optionsLayout->addWidget(receiver_);
    frequencies_label_ = new QLabel(this);
    frequencies_ = new QSpinBox(this);
    frequencies_->setRange(1, 5);
    optionsLayout->addWidget(frequencies_label_);
    optionsLayout->addWidget(frequencies_);
    optionsLayout->addStretch();
    layout->addWidget(options, 3, 0, 1, 6);
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
    layout->addWidget(systemOptions, 4, 0, 1, 6);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    layout->addWidget(progress_, 5, 0, 1, 5);
    layout->addWidget(cancel_, 5, 5);
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
    const auto status = PpkProcessor::status(session_);
    const bool enabled = !session_.isEmpty() && !busy();
    auto available = [&](bool value) { return textFor(value ? "available" : "missing", value ? "已有" : "缺少"); };
    QString state = status.state;
    if (busy())
        state = textFor(processing_ ? "Processing" : "Importing", processing_ ? "处理中" : "导入中");
    else if (state == "Ready")
        state = textFor("Ready", "就绪");
    else if (state == "Completed")
        state = textFor("Completed", "已完成");
    else if (state == "Failed")
        state = textFor("Failed", "失败");
    else
        state = textFor("Waiting for inputs", "等待输入数据");
    status_->setText(QStringLiteral("PPK · %1   |   Rover: %2   Base: %3   NAV: %4")
                         .arg(state, available(status.roverAvailable), available(status.baseAvailable),
                              available(status.navigationAvailable)));
    const auto q = status.quality;
    QString summary = status.error;
    if (status.completed)
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
    quality_->setVisible(!summary.isEmpty());
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
    watcher_ = new QFutureWatcher<PpkProcessResult>(this);
    auto *watcher = watcher_;
    connect(watcher, &QFutureWatcher<PpkProcessResult>::finished, this,
            [this, watcher]
            {
                const auto result = watcher->result();
                watcher_ = nullptr;
                watcher->deleteLater();
                refresh();
                emit busyChanged(false);
                if (!result.success && !result.cancelled)
                    QMessageBox::warning(this, textFor("PPK error", "PPK 错误"), result.error);
                if (result.success && processing_)
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
