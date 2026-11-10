#include "WaterCalibrationPage.h"
#include "CalibrationService.h"
#include "CalibrationReviewPlot.h"
#include "RecordedConcentrations.h"
#include "shared/theme/AppTheme.h"
#include "shared/theme/TopLevelCardStyle.h"
#include "LogService.h"
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QScopeGuard>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>

namespace VaporView::Ground::Calibration
{
namespace
{
QPushButton *button(const QString& text,const char *id,QHBoxLayout *row,std::function<void()> action)
{
    auto *b=new QPushButton(text);b->setObjectName(QLatin1String(id));row->addWidget(b);
    QObject::connect(b,&QPushButton::clicked,b,[action]{action();});return b;
}
QTableWidget *table(const QStringList& columns,QWidget *parent)
{
    auto *t=new QTableWidget(0,columns.size(),parent);t->setHorizontalHeaderLabels(columns);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    t->horizontalHeader()->setStretchLastSection(true);t->verticalHeader()->setVisible(false);return t;
}
void cell(QTableWidget *t,int r,int c,const QString& text) {t->setItem(r,c,new QTableWidgetItem(text));}
QString duration(quint64 ms) {return QStringLiteral("%1:%2").arg(ms/60000).arg(ms/1000%60,2,10,QChar('0'));}
QString statusText(const QString& s)
{
    static const QMap<QString,QString> names{{"pending",QStringLiteral("待采集")},{"acquiring",QStringLiteral("采集中")},
        {"paused",QStringLiteral("已暂停")},{"complete",QStringLiteral("完成")},{"incomplete",QStringLiteral("未完成")},{"abandoned",QStringLiteral("已废弃")}};
    return names.value(s,s);
}
QString sampleDetails(const PeakRecord& row,const ConcentrationStage& stage)
{
    auto j=row.toJson();QJsonArray audit;
    for(const auto& op:stage.operations)if(op.recordId==row.feature.id)audit.append(op.toJson());
    j["audit_history"]=audit;j["currently_excluded"]=stage.isExcluded(row.feature.id);
    return QString::fromUtf8(QJsonDocument(j).toJson());
}
struct HistoryResult {ConcentrationSnapshot legacy;RecordedConcentrations recorded;bool isRecorded=false;};
QString documentSummary(QJsonObject document)
{
    document.remove("vcal_document");
    auto project=document["project"].toObject();auto stages=project["stages"].toArray();
    for(int i=0;i<stages.size();++i) {
        auto stage=stages[i].toObject();auto channels=stage["channels"].toObject();
        for(const auto& wave:channels.keys()) {
            if(channels[wave].isArray())channels[wave]=QJsonObject{{"raw_sample_count",channels[wave].toArray().size()}};
        }
        if(stage.contains("exclusions"))stage["audit_operation_count"]=stage.take("exclusions").toArray().size();
        stage["channels"]=channels;stages[i]=stage;
    }
    if(!project.isEmpty()){project["stages"]=stages;document["project"]=project;}
    return QString::fromUtf8(QJsonDocument(document).toJson());
}
}
struct WaterCalibrationPage::State
{
    std::shared_ptr<CalibrationService> service;
    CalibrationView view;
    QTabWidget *tabs;
    QLineEdit *name,*note,*reason,*filename;
    QComboBox *unit,*wave,*files,*drafts,*reviewStage;
    QDoubleSpinBox *concentration;
    QSpinBox *seconds,*samplePage;
    QCheckBox *stable,*identityConfirmed;
    QTableWidget *stages,*samples,*statistics;
    QLabel *status,*progressText,*currentFile,*live,*fitSummary;
    QProgressBar *progress;
    QPlainTextEdit *details,*sampleDetails;
    CalibrationReviewPlot *review;
    CalibrationFitPlot *fitPlot;
    CalibrationReviewPlot *trend816,*trend1368;
    QSet<QString> selectedIds;
    QString reviewFingerprint,lastMessage;
    int stageIndex=-1;
    QVector<ConcentrationResult> historicalResults;
    QVector<Fit> historicalFits;
    RecordedConcentrations historicalRecorded;
    quint64 historyRequest=0;
    quint64 inspection=0;
};
WaterCalibrationPage::WaterCalibrationPage(QWidget *parent,const QString& root,std::shared_ptr<CalibrationService> service)
    :QWidget(parent),state_(std::make_unique<State>())
{
    auto& s=*state_;s.service=service?std::move(service):std::make_shared<CalibrationService>(root);
    setObjectName("waterCalibrationPage");auto *rootLayout=new QVBoxLayout(this);
    auto *card=new QFrame(this);card->setObjectName("calibrationPageCard");configureTopLevelCard(card);rootLayout->addWidget(card);
    auto *cardLayout=new QVBoxLayout(card);cardLayout->setContentsMargins(1,0,1,1);cardLayout->setSpacing(0);
    auto *header=new QWidget(card);header->setObjectName("calibrationCardHeader");auto *headerLayout=new QHBoxLayout(header);headerLayout->setContentsMargins(10,2,10,2);
    auto *title=new QLabel(QStringLiteral("水汽定标"));title->setObjectName("sectionTitleLabel");
    title->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);title->setFocusPolicy(Qt::ClickFocus);headerLayout->addWidget(title);cardLayout->addWidget(header);
    auto *body=new QWidget(card);body->setObjectName("calibrationCardBody");auto *layout=new QVBoxLayout(body);cardLayout->addWidget(body,1);
    s.currentFile=new QLabel;s.currentFile->setObjectName("calibrationCurrentFile");s.currentFile->setWordWrap(true);layout->addWidget(s.currentFile);
    s.live=new QLabel;s.live->setObjectName("calibrationLiveConcentration");s.live->setWordWrap(true);layout->addWidget(s.live);
    auto *navigation=new QFrame(body);navigation->setObjectName("calibrationNavigationBar");navigation->setAttribute(Qt::WA_StyledBackground,true);
    auto *navigationLayout=new QHBoxLayout(navigation);navigationLayout->setContentsMargins(4,3,4,3);navigationLayout->setSpacing(4);
    auto *pageButtons=new QButtonGroup(this);pageButtons->setExclusive(true);
    const QStringList pageNames{QStringLiteral("任务与采集"),QStringLiteral("审核与拟合"),QStringLiteral("定标文件管理")};
    for(int i=0;i<pageNames.size();++i){auto *selector=new QPushButton(pageNames[i],navigation);selector->setObjectName(QStringLiteral("calibrationPageSelectorButton%1").arg(i+1));selector->setCheckable(true);selector->setCursor(Qt::PointingHandCursor);selector->setFocusPolicy(Qt::TabFocus);navigationLayout->addWidget(selector);pageButtons->addButton(selector,i);}
    layout->addWidget(navigation,0,Qt::AlignLeft);
    auto *workspaceCard=new QFrame(body);workspaceCard->setObjectName("calibrationWorkspaceCard");workspaceCard->setAttribute(Qt::WA_StyledBackground,true);
    auto *workspaceLayout=new QVBoxLayout(workspaceCard);workspaceLayout->setContentsMargins(6,12,6,12);layout->addWidget(workspaceCard,1);
    s.tabs=new QTabWidget;s.tabs->setObjectName("calibrationWorkspaces");workspaceLayout->addWidget(s.tabs);
    connect(pageButtons,&QButtonGroup::idClicked,s.tabs,&QTabWidget::setCurrentIndex);
    connect(s.tabs,&QTabWidget::currentChanged,this,[pageButtons](int index){if(auto *selector=pageButtons->button(index))selector->setChecked(true);});
    auto *acquisition=new QWidget;auto *a=new QVBoxLayout(acquisition);
    auto *createRow=new QHBoxLayout;s.name=new QLineEdit;s.name->setObjectName("calibrationTaskName");s.name->setPlaceholderText(QStringLiteral("例如：实验室标准定标"));s.name->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    s.unit=new QComboBox;s.unit->setObjectName("calibrationUnit");s.unit->addItems({"ppmv","mol/mol","mg/m³"});
    createRow->addWidget(new QLabel(QStringLiteral("任务名称")));createRow->addWidget(s.name,3);
    createRow->addWidget(new QLabel(QStringLiteral("统一浓度单位")));createRow->addWidget(s.unit,1);
    button(QStringLiteral("创建新任务"),"calibrationCreateTask",createRow,[this]{
        auto& s=*state_;s.service->createProject(s.name->text(),s.unit->currentIndex()==2?QStringLiteral("mg/m3"):s.unit->currentText());});
    a->addLayout(createRow);
    s.drafts=new QComboBox;s.drafts->setObjectName("calibrationDrafts");auto *draftRow=new QHBoxLayout;draftRow->addWidget(s.drafts,1);
    button(QStringLiteral("恢复草稿"),"calibrationRecoverDraft",draftRow,[this]{state_->service->openProject(state_->drafts->currentData().toString(),true);});a->addLayout(draftRow);
    s.stages=table({QStringLiteral("阶段"),QStringLiteral("浓度"),QStringLiteral("状态"),QStringLiteral("目标时长"),QStringLiteral("有效时长"),QStringLiteral("816 / 1368 周期数"),QStringLiteral("备注")},acquisition);
    s.stages->setObjectName("calibrationStages");s.stages->setSelectionMode(QAbstractItemView::SingleSelection);a->addWidget(s.stages,1);
    auto *stageForm=new QHBoxLayout;s.concentration=new QDoubleSpinBox;s.concentration->setObjectName("calibrationStageConcentration");s.concentration->setDecimals(8);s.concentration->setRange(0,1e12);
    s.seconds=new QSpinBox;s.seconds->setObjectName("calibrationStageSeconds");s.seconds->setRange(1,86400);s.seconds->setValue(600);s.seconds->setSuffix(QStringLiteral(" 秒"));
    s.note=new QLineEdit;s.note->setPlaceholderText(QStringLiteral("阶段备注"));s.note->setObjectName("calibrationStageNote");
    stageForm->addWidget(new QLabel(QStringLiteral("已知浓度")));stageForm->addWidget(s.concentration);stageForm->addWidget(s.seconds);stageForm->addWidget(s.note,1);a->addLayout(stageForm);
    auto *editRow=new QHBoxLayout;
    button(QStringLiteral("添加阶段"),"calibrationAddStage",editRow,[this]{auto& s=*state_;s.service->addStage(s.concentration->value(),quint64(s.seconds->value())*1000,s.note->text());});
    button(QStringLiteral("修改待采阶段"),"calibrationEditStage",editRow,[this]{auto& s=*state_;s.service->editStage(s.stageIndex,s.concentration->value(),quint64(s.seconds->value())*1000,s.note->text());});
    button(QStringLiteral("删除 / 废弃"),"calibrationRemoveStage",editRow,[this]{state_->service->removeStage(state_->stageIndex);});
    button(QStringLiteral("上移"),"calibrationStageUp",editRow,[this]{state_->service->moveStage(state_->stageIndex,-1);});
    button(QStringLiteral("下移"),"calibrationStageDown",editRow,[this]{state_->service->moveStage(state_->stageIndex,1);});editRow->addStretch();a->addLayout(editRow);
    s.stable=new QCheckBox(QStringLiteral("已切换至此阶段标气，并确认气路和测量信号稳定"));s.stable->setObjectName("calibrationStableConfirmed");a->addWidget(s.stable);
    auto *runRow=new QHBoxLayout;
    button(QStringLiteral("确认稳定，开始 / 继续采集"),"calibrationStartStage",runRow,[this]{
        if(!state_->stable->isChecked()){state_->status->setText(QStringLiteral("请先人工确认标气稳定"));return;}
        state_->service->startStage(state_->stageIndex);state_->stable->setChecked(false);});
    button(QStringLiteral("暂停"),"calibrationPauseStage",runRow,[this]{state_->service->pauseStage();});
    button(QStringLiteral("停止并保留未完成阶段"),"calibrationStopStage",runRow,[this]{state_->service->pauseStage(true);});runRow->addStretch();a->addLayout(runRow);
    s.progress=new QProgressBar;s.progress->setRange(0,1000);s.progress->setObjectName("calibrationProgress");a->addWidget(s.progress);
    s.progressText=new QLabel;s.progressText->setWordWrap(true);a->addWidget(s.progressText);
    auto *trends=new QHBoxLayout;s.trend816=new CalibrationReviewPlot;s.trend1368=new CalibrationReviewPlot;
    s.trend816->setStage({},816);s.trend1368->setStage({},1368);
    trends->addWidget(s.trend816);trends->addWidget(s.trend1368);a->addLayout(trends);
    const auto workspace=[&](QWidget *content,const QString& name){content->setObjectName("calibrationWorkspace");auto *scroll=new QScrollArea;scroll->setObjectName("calibrationWorkspaceScroll");scroll->viewport()->setObjectName("calibrationWorkspaceViewport");scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setWidget(content);s.tabs->addTab(scroll,name);};
    workspace(acquisition,QStringLiteral("任务与采集"));

    auto *review=new QWidget;auto *r=new QVBoxLayout(review);
    auto *reviewRow=new QHBoxLayout;s.wave=new QComboBox;s.wave->setObjectName("calibrationReviewWave");s.wave->addItems({"816 nm · DLIA1 / 0x31","1368 nm · DLIA0 / 0x30"});reviewRow->addWidget(s.wave);reviewRow->addStretch();
    s.reviewStage=new QComboBox;s.reviewStage->setObjectName("calibrationReviewStage");reviewRow->insertWidget(0,s.reviewStage,1);
    s.samplePage=new QSpinBox;s.samplePage->setObjectName("calibrationSamplePage");s.samplePage->setMinimum(1);reviewRow->addWidget(new QLabel(QStringLiteral("样本页（每页 500 条）")));reviewRow->addWidget(s.samplePage);r->addLayout(reviewRow);
    s.review=new CalibrationReviewPlot;s.review->setObjectName("calibrationReviewPlot");r->addWidget(s.review,2);
    s.samples=table({QStringLiteral("记录 ID"),QStringLiteral("UTC 时间"),"R",QStringLiteral("周期 / tick"),QStringLiteral("质量"),QStringLiteral("审核状态")},review);
    s.samples->setObjectName("calibrationSamples");r->addWidget(s.samples,2);
    s.sampleDetails=new QPlainTextEdit;s.sampleDetails->setReadOnly(true);s.sampleDetails->setMinimumHeight(90);s.sampleDetails->setMaximumHeight(140);r->addWidget(s.sampleDetails);
    auto *excludeRow=new QHBoxLayout;s.reason=new QLineEdit;s.reason->setPlaceholderText(QStringLiteral("人工排除原因（恢复操作也记录审核历史）"));s.reason->setObjectName("calibrationExclusionReason");excludeRow->addWidget(s.reason,1);
    const auto exclude=[this](bool excluded){auto& s=*state_;if(s.stageIndex<0 || s.stageIndex>=s.view.project.stages.size())return;
        s.service->exclude(s.view.project.stages[s.stageIndex].id,s.wave->currentIndex()==0?816:1368,s.selectedIds,excluded,s.reason->text());};
    button(QStringLiteral("排除选中点"),"calibrationExclude",excludeRow,[exclude]{exclude(true);});
    button(QStringLiteral("恢复选中点"),"calibrationRestore",excludeRow,[exclude]{exclude(false);});r->addLayout(excludeRow);
    s.statistics=table({QStringLiteral("波长"),QStringLiteral("原始 / 有效 / 质量无效 / 人工排除 / 参与统计"),QStringLiteral("均值"),QStringLiteral("标准差"),QStringLiteral("最小 / 最大")},review);s.statistics->setMaximumHeight(120);r->addWidget(s.statistics);
    auto *fitRow=new QHBoxLayout;button(QStringLiteral("双波长拟合（阶段均值等权）"),"calibrationFit",fitRow,[this]{state_->service->fitProject();});fitRow->addStretch();r->addLayout(fitRow);
    s.fitSummary=new QLabel;s.fitSummary->setWordWrap(true);s.fitSummary->setTextInteractionFlags(Qt::TextSelectableByMouse);r->addWidget(s.fitSummary);
    s.fitPlot=new CalibrationFitPlot;s.fitPlot->setMinimumHeight(210);r->addWidget(s.fitPlot,1);workspace(review,QStringLiteral("审核与拟合"));

    auto *files=new QWidget;auto *f=new QVBoxLayout(files);auto *fileRow=new QHBoxLayout;
    s.filename=new QLineEdit;s.filename->setObjectName("calibrationFilename");s.filename->setPlaceholderText(QStringLiteral("自定义中文文件名（不覆盖已有文件）"));fileRow->addWidget(s.filename,1);
    button(QStringLiteral("生成 .vcal"),"calibrationGenerate",fileRow,[this]{state_->service->generateFile(state_->filename->text());});f->addLayout(fileRow);
    s.files=new QComboBox;s.files->setObjectName("calibrationFiles");f->addWidget(s.files);
    s.identityConfirmed=new QCheckBox(QStringLiteral("已核对文件与当前设备的身份差异，明确确认使用"));f->addWidget(s.identityConfirmed);
    auto *manage=new QHBoxLayout;
    button(QStringLiteral("查看文件详情"),"calibrationInspectFile",manage,[this]{state_->service->inspectFile(state_->files->currentData().toString());});
    button(QStringLiteral("选择用于测量 / 校验"),"calibrationSelectFile",manage,[this]{state_->service->selectFile(state_->files->currentData().toString(),state_->identityConfirmed->isChecked());});
    button(QStringLiteral("导入外部 .vcal"),"calibrationBrowseFile",manage,[this]{const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("导入定标文件"),{},"Calibration (*.vcal)");if(!path.isEmpty())state_->service->copyFile(path,QFileInfo(path).fileName());});
    button(QStringLiteral("创建修订任务"),"calibrationRevise",manage,[this]{state_->service->openProject(state_->files->currentData().toString(),false);});
    button(QStringLiteral("刷新文件"),"calibrationRefreshFiles",manage,[this]{state_->service->refreshFiles();});f->addLayout(manage);
    auto *fileActions=new QHBoxLayout;
    button(QStringLiteral("复制 / 另存为"),"calibrationCopyFile",fileActions,[this]{state_->service->copyFile(state_->files->currentData().toString(),state_->filename->text());});
    button(QStringLiteral("归档"),"calibrationArchiveFile",fileActions,[this]{state_->service->removeFile(state_->files->currentData().toString(),true);});
    button(QStringLiteral("删除"),"calibrationDeleteFile",fileActions,[this]{if(QMessageBox::question(this,QStringLiteral("删除定标文件"),QStringLiteral("确认删除当前文件？活动 Session 使用的文件不能删除。"))==QMessageBox::Yes)state_->service->removeFile(state_->files->currentData().toString(),false);});
    button(QStringLiteral("导入旧库参数"),"calibrationLegacyImport",fileActions,[this]{
        const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("选择旧 library.json"),Store::defaultFilename(),"JSON (*.json)");if(path.isEmpty())return;
        bool ok=false;const auto a=QInputDialog::getText(this,QStringLiteral("旧版本导入"),QStringLiteral("816 nm 原始 Fit UUID（原库保持不变）"),QLineEdit::Normal,{},&ok);if(!ok)return;
        const auto b=QInputDialog::getText(this,QStringLiteral("旧版本导入"),QStringLiteral("1368 nm 原始 Fit UUID"),QLineEdit::Normal,{},&ok);if(ok)state_->service->importLegacy(path,a,b);
    });fileActions->addStretch();f->addLayout(fileActions);
    auto *history=new QHBoxLayout;
    button(QStringLiteral("查看历史浓度快照"),"calibrationHistory",history,[this]{const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("历史浓度快照 / Session 浓度记录"),{},"JSON / JSONL (*.json *.jsonl)");if(!path.isEmpty())loadConcentrationResults(path);});
    button(QStringLiteral("导出已查看的历史浓度"),"calibrationExportHistory",history,[this]{const auto path=QFileDialog::getSaveFileName(this,QStringLiteral("导出自包含历史浓度"),{},"JSON (*.json)");if(!path.isEmpty())exportConcentrationResults(path);});
    button(QStringLiteral("结束异常恢复的 Session"),"calibrationFinishRecovered",history,[this]{
        if(QMessageBox::question(this,QStringLiteral("结束异常会话"),QStringLiteral("确认此会话已经停止？将解除持久化定标锁，保留历史快照。"))==QMessageBox::Yes)state_->service->finishRecoveredSession();});history->addStretch();f->addLayout(history);
    s.details=new QPlainTextEdit;s.details->setReadOnly(true);s.details->setObjectName("calibrationDetails");f->addWidget(s.details,1);
    auto *boundary=new QLabel(QStringLiteral("验证用线性模型 C=aR+b；显示初始浓度，未进行温压补偿。文件长期有效，不自动过期。\n文件和草稿固定保存于程序安装目录 calibrations/，写入失败不会改存其他目录。"));boundary->setWordWrap(true);f->addWidget(boundary);workspace(files,QStringLiteral("定标文件管理"));
    s.tabs->tabBar()->hide();pageButtons->button(s.tabs->currentIndex())->setChecked(true);
    s.status=new QLabel;s.status->setObjectName("calibrationStatus");s.status->setWordWrap(true);s.status->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(s.status);
    connect(s.service.get(),&CalibrationService::changed,this,[this]{refresh();},Qt::QueuedConnection);
    connect(s.stages,&QTableWidget::itemSelectionChanged,this,[this]{auto& s=*state_;s.stageIndex=s.stages->currentRow();s.reviewFingerprint.clear();
        if(s.stageIndex>=0 && s.stageIndex<s.view.project.stages.size()){const auto& st=s.view.project.stages[s.stageIndex];s.concentration->setValue(st.concentration);s.seconds->setValue(int(st.configuredMs/1000));s.note->setText(st.note);}refreshReview();});
    connect(s.wave,&QComboBox::currentIndexChanged,this,[this]{state_->reviewFingerprint.clear();state_->selectedIds.clear();refreshReview();});
    connect(s.reviewStage,&QComboBox::currentIndexChanged,this,[this](int i){state_->stageIndex=i;state_->reviewFingerprint.clear();state_->selectedIds.clear();refreshReview();});
    connect(s.samplePage,&QSpinBox::valueChanged,this,[this]{state_->reviewFingerprint.clear();refreshReview();});
    connect(s.samples,&QTableWidget::itemSelectionChanged,this,[this]{auto& s=*state_;s.selectedIds.clear();
        for(const auto& index:s.samples->selectionModel()->selectedRows())s.selectedIds.insert(s.samples->item(index.row(),0)->text());
        s.review->setSelection(s.selectedIds);
        if(!s.selectedIds.isEmpty() && s.stageIndex>=0){const auto& stage=s.view.project.stages[s.stageIndex];for(const auto& row:stage.samples.value(s.wave->currentIndex()==0?816:1368))if(s.selectedIds.contains(row.feature.id)){s.sampleDetails->setPlainText(sampleDetails(row,stage));break;}}
    });
    s.review->selectionChanged=[this]{auto& s=*state_;s.selectedIds=s.review->selection();QSignalBlocker block(s.samples);s.samples->clearSelection();
        for(int i=0;i<s.samples->rowCount();++i)if(s.selectedIds.contains(s.samples->item(i,0)->text()))s.samples->selectionModel()->select(s.samples->model()->index(i,0),QItemSelectionModel::Select|QItemSelectionModel::Rows);
        if(s.stageIndex>=0)for(const auto& row:s.view.project.stages[s.stageIndex].samples.value(s.wave->currentIndex()==0?816:1368))if(s.selectedIds.contains(row.feature.id)){s.sampleDetails->setPlainText(sampleDetails(row,s.view.project.stages[s.stageIndex]));break;}
    };
    setTheme(false);refresh();
}
WaterCalibrationPage::~WaterCalibrationPage()=default;
void WaterCalibrationPage::showEvent(QShowEvent *event) {QWidget::showEvent(event);refresh();}
std::shared_ptr<CalibrationService> WaterCalibrationPage::service() const {return state_->service;}
void WaterCalibrationPage::refresh()
{
    auto& s=*state_;const auto oldTask=s.view.project.id;s.view=s.service->view();const auto& v=s.view;
    if(oldTask!=v.project.id){++s.historyRequest;s.name->setText(v.project.name);s.unit->setCurrentIndex(v.project.unit=="mg/m3"?2:s.unit->findText(v.project.unit));s.stageIndex=-1;}
    s.status->setText(v.message);s.status->setProperty("calibrationError",v.error);
    if(v.message!=s.lastMessage && !v.message.isEmpty()) {
        s.lastMessage=v.message;
        VaporView::LogService::withCurrentInstance([&](VaporView::LogService& log){log.publish(v.error?VaporView::LogLevel::Warning:VaporView::LogLevel::Info,
            QStringLiteral("water_calibration"),QStringLiteral("calibration"),v.message,{{QStringLiteral("event"),QStringLiteral("calibration_state")},{QStringLiteral("task_id"),v.project.id}});});
    }
    if(!isVisible())return;
    s.currentFile->setText(v.selected.valid?QStringLiteral("当前定标：%1 · %2 · %3%4").arg(QFileInfo(v.selected.filename).fileName(),v.selected.project.name,v.locked?QStringLiteral("Session 已锁定"):QStringLiteral("可切换"),v.identityWarning.isEmpty()?QString():" · "+v.identityWarning):QStringLiteral("尚未选择有效双波长 .vcal；正式录制前必须选择"));
    QStringList live;for(int w:{816,1368}){const auto c=v.live.value(w);live.append(QStringLiteral("%1 nm：%2").arg(w).arg(c.valid?QString::number(c.initial,'g',8)+" "+c.unit:!c.status.isEmpty()?c.status:QStringLiteral("等待有效周期和定标参数")));}
    QStringList peaks;for(int w:{816,1368})peaks.append(QStringLiteral("%1 nm 最近 R=%2").arg(w).arg(v.recentPeaks.contains(w)?QString::number(v.recentPeaks[w],'g',8):QStringLiteral("无数据")));
    s.live->setText(live.join("    ")+QStringLiteral("    初始浓度 · 未温压补偿\n")+peaks.join("    ")+(v.connected?QStringLiteral("    设备在线"):QStringLiteral("    设备未连接")));
    {QSignalBlocker block(s.stages);const int current=s.stageIndex;s.stages->setRowCount(v.project.stages.size());
        for(int i=0;i<v.project.stages.size();++i){const auto& st=v.project.stages[i];cell(s.stages,i,0,QString::number(i+1));cell(s.stages,i,1,QString::number(st.concentration,'g',10)+" "+v.project.unit);cell(s.stages,i,2,statusText(st.status));cell(s.stages,i,3,duration(st.configuredMs));cell(s.stages,i,4,duration(st.effectiveMs));cell(s.stages,i,5,QStringLiteral("%1 / %2").arg(st.samples.value(816).size()).arg(st.samples.value(1368).size()));cell(s.stages,i,6,st.note);}
        if(current>=0 && current<s.stages->rowCount())s.stages->selectRow(current);else if(s.stages->rowCount()){s.stageIndex=0;s.stages->selectRow(0);}else s.stageIndex=-1;
    }
    const int active=v.acquiring?v.activeStage:s.stageIndex;
    if(active>=0 && active<v.project.stages.size()) {
        const auto& st=v.project.stages[active];s.progress->setValue(int(st.effectiveMs*1000/st.configuredMs));
        s.trend816->setStage(st,816);s.trend1368->setStage(st,1368);
        const auto a=st.statistics(816),b=st.statistics(1368);
        s.progressText->setText(QStringLiteral("%1 %2 · %3；有效 %4 / %5，剩余 %6，墙上耗时 %7\n有效周期 816 / 1368：%8 / %9；质量无效：%10 / %11；设备%12").arg(st.concentration,0,'g',8).arg(v.project.unit,statusText(st.status),duration(st.effectiveMs),duration(st.configuredMs),duration(st.configuredMs-st.effectiveMs),duration(st.wallMs)).arg(a.valid).arg(b.valid).arg(a.invalid).arg(b.invalid).arg(v.connected?QStringLiteral("在线"):QStringLiteral("离线")));
    }else{s.progress->setValue(0);s.progressText->setText(QStringLiteral("创建任务并添加浓度阶段；每阶段默认 10 分钟，两波长同步采集。"));}
    const auto fill=[](QComboBox *combo,const QStringList& paths){const auto old=combo->currentData().toString();QSignalBlocker b(combo);combo->clear();for(const auto& path:paths)combo->addItem(QFileInfo(path).fileName(),path);const int i=combo->findData(old);if(i>=0)combo->setCurrentIndex(i);};
    fill(s.files,v.files);fill(s.drafts,v.drafts);
    {QSignalBlocker block(s.reviewStage);s.reviewStage->clear();for(const auto& stage:v.project.stages)s.reviewStage->addItem(QStringLiteral("%1 %2 · %3").arg(stage.concentration,0,'g',8).arg(v.project.unit,statusText(stage.status)));s.reviewStage->setCurrentIndex(s.stageIndex);}
    if(v.selected.valid && s.details->toPlainText().isEmpty())s.details->setPlainText(documentSummary(v.selected.summary));
    if(s.inspection!=v.inspection){s.inspection=v.inspection;s.details->setPlainText(documentSummary(v.inspectedDocument));}
    refreshReview();
}
void WaterCalibrationPage::refreshReview()
{
    auto& s=*state_;const auto& p=s.view.project;const int w=s.wave->currentIndex()==0?816:1368;
    if(s.stageIndex<0 || s.stageIndex>=p.stages.size()){s.samples->setRowCount(0);s.statistics->setRowCount(0);}
    else {
    const auto& stage=p.stages[s.stageIndex];const auto& rows=stage.samples[w];
    const auto key=stage.id+":"+QString::number(p.revision)+":"+QString::number(rows.size())+":"+QString::number(w)+":"+QString::number(s.samplePage->value());
    if(key!=s.reviewFingerprint) {
        s.reviewFingerprint=key;const int pages=std::max(1,(int(rows.size())+499)/500);s.samplePage->setMaximum(pages);
        const int offset=(s.samplePage->value()-1)*500,count=std::min(500,int(rows.size())-offset);
        // A visible ResizeToContents header otherwise measures the entire table
        // after every replaced cell. Resize once after the page is populated.
        auto *header=s.samples->horizontalHeader();const auto mode=header->sectionResizeMode(0);
        const bool updates=s.samples->updatesEnabled();s.samples->setUpdatesEnabled(false);
        header->setSectionResizeMode(QHeaderView::Interactive);
        const auto restore=qScopeGuard([&]{header->setSectionResizeMode(mode);s.samples->setUpdatesEnabled(updates);});
        QSignalBlocker block(s.samples);s.samples->setRowCount(std::max(0,count));
        for(int i=0;i<count;++i){const auto& row=rows[offset+i];const auto& f=row.feature;
            cell(s.samples,i,0,f.id);cell(s.samples,i,1,QDateTime::fromMSecsSinceEpoch(qint64(f.hostTimeUs/1000),Qt::UTC).toString(Qt::ISODateWithMs));cell(s.samples,i,2,std::isfinite(f.peak)?QString::number(f.peak,'g',10):"—");cell(s.samples,i,3,QStringLiteral("%1 / %2").arg(f.cycleId).arg(f.fpgaTick));cell(s.samples,i,4,f.invalidReason().isEmpty()?QStringLiteral("有效"):f.invalidReason());cell(s.samples,i,5,stage.isExcluded(f.id)?QStringLiteral("已人工排除"):QStringLiteral("保留"));}
        s.review->setStage(stage,w);s.selectedIds.clear();
    }
    s.statistics->setRowCount(2);for(int i=0;i<2;++i){const int wave=i?1368:816;const auto st=stage.statistics(wave);
        cell(s.statistics,i,0,QString::number(wave));cell(s.statistics,i,1,QStringLiteral("%1 / %2 / %3 / %4 / %5").arg(st.raw).arg(st.valid).arg(st.invalid).arg(st.excluded).arg(st.used));cell(s.statistics,i,2,st.used?QString::number(st.mean,'g',8):"—");cell(s.statistics,i,3,st.used?QString::number(st.standardDeviation,'g',8):"—");cell(s.statistics,i,4,st.used?QStringLiteral("%1 / %2").arg(st.minimum,0,'g',7).arg(st.maximum,0,'g',7):"—");}
    }
    QStringList summary;
    for(int wave:{816,1368}) {
        if(!p.fits.contains(wave)){summary.append(QStringLiteral("%1 nm：尚无当前修订的拟合").arg(wave));continue;}
        const auto& f=p.fits[wave];QStringList residuals;for(double v:f.parameters.residuals)residuals.append(QString::number(v,'g',6));
        summary.append(QStringLiteral("%1 nm：C=%2×R%3%4；阶段数 %5，浓度水平 %6；R²=%7，RMSE=%8\n范围 R=[%9,%10]，C=[%11,%12] %13；残差：%14\n%15")
            .arg(wave).arg(f.parameters.a,0,'g',9).arg(f.parameters.b>=0?"+":"").arg(f.parameters.b,0,'g',9).arg(f.parameters.sampleCount).arg(f.parameters.concentrationCount).arg(f.parameters.rSquared,0,'g',7).arg(f.parameters.rmse,0,'g',7).arg(f.parameters.rMin,0,'g',7).arg(f.parameters.rMax,0,'g',7).arg(f.parameters.cMin,0,'g',7).arg(f.parameters.cMax,0,'g',7).arg(p.unit,residuals.join(", "),f.warning));
    }
    s.fitSummary->setText(summary.join('\n'));s.fitPlot->project=p;s.fitPlot->wavelength=w;s.fitPlot->update();
}
void WaterCalibrationPage::setTheme(bool dark,int percent)
{
    setProperty(kAppDarkThemeProperty,dark);setPalette(appThemePalette(dark));
    const qreal scale=qBound(60,percent,180)/100.0;
    const auto pixels=[scale](int value){return qRound(value*scale);};
    setStyleSheet(applyAppThemeTokens(QStringLiteral(
        "QWidget#waterCalibrationPage { background-color: @vv-window; color: @vv-text; }"
        "QWidget#waterCalibrationPage QFrame#calibrationPageCard { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 12px; }"
        "QWidget#waterCalibrationPage QWidget#calibrationCardHeader { background-color: @vv-surface-raised; border: none; border-bottom: 1px solid @vv-border; border-top-left-radius: 11px; border-top-right-radius: 11px; min-height: %9px; max-height: %9px; }"
        "QWidget#waterCalibrationPage QWidget#calibrationCardBody { background-color: @vv-surface-raised; border: none; border-bottom-left-radius: 11px; border-bottom-right-radius: 11px; }"
        "QWidget#waterCalibrationPage QLabel { color: @vv-text; background-color: transparent; font-size: %2px; }"
        "QWidget#waterCalibrationPage QLabel#sectionTitleLabel { border: none; color: @vv-text; font-size: %1px; font-weight: 700; margin: 0px; padding: 0px; }"
        "QWidget#waterCalibrationPage QPushButton { background-color: @vv-primary; color: @vv-white; border: none; border-radius: 6px; padding: 4px %3px; font-size: %4px; font-weight: 500; min-height: %5px; max-height: %5px; }"
        "QWidget#waterCalibrationPage QPushButton:hover { background-color: @vv-primary-hover; }"
        "QWidget#waterCalibrationPage QPushButton:pressed { background-color: @vv-primary-pressed; }"
        "QWidget#waterCalibrationPage QPushButton:disabled { background-color: @vv-border-strong; color: @vv-white; }"
        "QWidget#waterCalibrationPage QPushButton:focus { border: 1px solid @vv-focus; }"
        "QWidget#waterCalibrationPage QTableWidget, QWidget#waterCalibrationPage QPlainTextEdit { background: @vv-surface; alternate-background-color: @vv-surface-alt; color: @vv-text; gridline-color: @vv-border; }"
        "QWidget#waterCalibrationPage QHeaderView::section { background: @vv-surface-alt; color: @vv-text; padding: 4px; }"
        "QWidget#waterCalibrationPage QLineEdit, QWidget#waterCalibrationPage QComboBox, QWidget#waterCalibrationPage QAbstractSpinBox { background-color: @vv-field-bg; border: 1px solid @vv-border; border-radius: 6px; padding: 4px 10px; min-height: %6px; max-height: %6px; color: @vv-text; font-size: %2px; }"
        "QWidget#waterCalibrationPage QLineEdit:hover, QWidget#waterCalibrationPage QComboBox:hover, QWidget#waterCalibrationPage QAbstractSpinBox:hover { border-color: @vv-border-strong; }"
        "QWidget#waterCalibrationPage QLineEdit:focus, QWidget#waterCalibrationPage QComboBox:focus, QWidget#waterCalibrationPage QAbstractSpinBox:focus { border-color: @vv-primary; }"
        "QWidget#waterCalibrationPage QLineEdit:disabled, QWidget#waterCalibrationPage QComboBox:disabled, QWidget#waterCalibrationPage QAbstractSpinBox:disabled { background-color: @vv-surface-alt; color: @vv-text-disabled; }"
        "QWidget#waterCalibrationPage QAbstractSpinBox QLineEdit { background-color: transparent; border: none; border-radius: 0px; padding: 0px; min-height: 0px; max-height: 16777215px; }"
        "QWidget#waterCalibrationPage QFrame#calibrationNavigationBar { background-color: @vv-surface-alt; border: 1px solid @vv-border; border-radius: 8px; }"
        "QWidget#waterCalibrationPage QFrame#calibrationNavigationBar QPushButton { background-color: transparent; border: none; border-radius: 6px; color: @vv-text; font-size: %2px; font-weight: 500; min-height: %8px; max-height: %8px; padding: 0px %7px; text-align: center; outline: none; }"
        "QWidget#waterCalibrationPage QFrame#calibrationNavigationBar QPushButton:checked { background-color: @vv-surface; color: @vv-primary; font-weight: 600; }"
        "QWidget#waterCalibrationPage QFrame#calibrationNavigationBar QPushButton:!checked:hover { background-color: @vv-primary-subtle; color: @vv-primary; }"
        "QWidget#waterCalibrationPage QFrame#calibrationWorkspaceCard { background-color: @vv-surface-raised; border: 1px solid @vv-border; border-radius: 8px; }"
        "QWidget#waterCalibrationPage QTabWidget#calibrationWorkspaces, QWidget#waterCalibrationPage QTabWidget#calibrationWorkspaces::pane { background-color: transparent; border: none; }"
        "QWidget#waterCalibrationPage QScrollArea#calibrationWorkspaceScroll { background-color: @vv-surface-raised; border: none; }"
        "QWidget#waterCalibrationPage QScrollArea#calibrationWorkspaceScroll QWidget#calibrationWorkspaceViewport, QWidget#waterCalibrationPage QWidget#calibrationWorkspace { background-color: @vv-surface-raised; }"
    ).arg(pixels(16)).arg(pixels(14)).arg(pixels(16)).arg(pixels(15)).arg(pixels(28))
        .arg(pixels(26)).arg(pixels(10)).arg(pixels(30)).arg(pixels(40)),dark));
    auto colors=appThemePalette(dark);
    colors.setColor(QPalette::PlaceholderText,appThemeColor(AppThemeColor::TextMuted,dark));
    for(auto *widget:findChildren<QWidget*>())widget->setPalette(colors);
    for(auto *combo:findChildren<QComboBox*>())configureComboBoxPopup(combo,dark);
    for(auto *edit:{state_->details,state_->sampleDetails}) {
        edit->viewport()->setStyleSheet(applyAppThemeTokens(QStringLiteral("background-color: @vv-surface; color: @vv-text;"),dark));
    }
}
void WaterCalibrationPage::loadConcentrationResults(const QString& filename)
{
    const auto request=++state_->historyRequest;auto *watcher=new QFutureWatcher<HistoryResult>(this);
    connect(watcher,&QFutureWatcher<HistoryResult>::finished,this,[this,watcher,request]{const auto loaded=watcher->result();watcher->deleteLater();if(request!=state_->historyRequest)return;
        if(loaded.isRecorded){if(!loaded.recorded.success){state_->status->setText(loaded.recorded.error);return;}
            state_->historicalRecorded=loaded.recorded;state_->historicalResults.clear();state_->historicalFits.clear();
            auto snapshot=loaded.recorded.snapshot;snapshot.remove("vcal_document");QJsonArray preview;
            for(int i=0;i<std::min(200,int(loaded.recorded.rows.size()));++i)preview.append(loaded.recorded.rows[i]);
            state_->details->setPlainText(QString::fromUtf8(QJsonDocument(QJsonObject{{"snapshot",snapshot},{"record_count",loaded.recorded.rows.size()},{"first_200_records",preview}}).toJson()));
            state_->status->setText(QStringLiteral("已读取 Session 原始参数和浓度记录，原 .vcal 文件无需存在"));return;}
        const auto& result=loaded.legacy;if(!result.success){state_->status->setText(result.error);return;}state_->historicalRecorded={};state_->historicalResults=result.results;state_->historicalFits=result.calibrations;
        QJsonArray rows;for(int i=0;i<std::min(200,int(result.results.size()));++i)rows.append(result.results[i].toJson());state_->details->setPlainText(QString::fromUtf8(QJsonDocument(QJsonObject{{"record_count",result.results.size()},{"first_200_records",rows}}).toJson()));state_->status->setText(QStringLiteral("已读取独立历史快照；当前定标选择不会覆盖历史结果。 ")+result.warning);});
    watcher->setFuture(QtConcurrent::run([filename]{HistoryResult r;r.recorded=readRecordedConcentrations(filename);r.isRecorded=r.recorded.success || filename.endsWith(".jsonl",Qt::CaseInsensitive) || QFileInfo(filename).fileName()=="calibration_snapshot.json";
        if(!r.isRecorded){Library l;QString e;Store().load(l,&e);r.legacy=readResults(filename,l.fits);}return r;}));
}
void WaterCalibrationPage::exportConcentrationResults(const QString& filename)
{
    const auto results=state_->historicalResults;
    const auto recorded=state_->historicalRecorded;
    const auto calibrations=state_->historicalFits;auto *watcher=new QFutureWatcher<QString>(this);
    connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher]{state_->status->setText(watcher->result().isEmpty()?QStringLiteral("历史快照已导出"):watcher->result());watcher->deleteLater();});
    watcher->setFuture(QtConcurrent::run([filename,results,calibrations,recorded]{QString e;if(recorded.success)exportRecordedConcentrations(filename,recorded,&e);else exportResults(filename,results,&e,calibrations);return e;}));
}
}
