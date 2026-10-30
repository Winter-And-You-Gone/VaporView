#include "WaterCalibrationPage.h"
#include "WaterCalibration.h"
#include "LogService.h"
#include "shared/theme/AppTheme.h"
#include "ground/widgets/DeviceEnabledCheckBox.h"
#include "ground/session/SessionLoader.h"
#include "ground/session/GroundRecordingService.h"
#include "shared/session/SessionPathResolver.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTabBar>
#include <QSet>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <functional>
#include <optional>
#include <cmath>

namespace VaporView::Ground::Calibration
{
namespace
{
QString number(double v) { return std::isfinite(v) ? QString::number(v, 'g', 12) : QStringLiteral("—"); }
QString timeText(quint64 us) { return QDateTime::fromMSecsSinceEpoch(qint64(us / 1000), Qt::UTC).toString(Qt::ISODateWithMs); }
void cell(QTableWidget *t, int r, int c, const QString& text)
{
    auto *item = new QTableWidgetItem(text); item->setToolTip(text); t->setItem(r, c, item);
}
QTableWidget *table(const QStringList& headers, QWidget *parent)
{
    auto *t = new QTableWidget(0, int(headers.size()), parent); t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers); t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection); t->setAlternatingRowColors(true);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    t->horizontalHeader()->setStretchLastSection(true); t->verticalHeader()->hide(); t->setMinimumHeight(170);
    return t;
}
class CalibrationPlot final : public QWidget
{
public:
    QVector<QPointF> points;
    std::optional<Fit> fit;
    QString unit;
    bool dark = false;
    explicit CalibrationPlot(QWidget *parent) : QWidget(parent) { setMinimumSize(300, 230); setAccessibleName(QStringLiteral("归一化峰值—浓度散点与线性拟合")); }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const auto color = [this](AppThemeColor c) { return appThemeColor(c, dark); };
        p.fillRect(rect(), color(AppThemeColor::Surface)); p.setPen(color(AppThemeColor::PlotText));
        const QRectF box(70, 20, std::max(1, width()-95), std::max(1, height()-75));
        p.drawLine(box.bottomLeft(), box.bottomRight()); p.drawLine(box.bottomLeft(), box.topLeft());
        p.drawText(QRectF(70, height()-27, width()-90, 24), Qt::AlignCenter, QStringLiteral("R = max(H2/H1)，全扫描有效点"));
        p.save(); p.translate(18, height()/2.0); p.rotate(-90); p.drawText(QRectF(-100,-12,200,24), Qt::AlignCenter, QStringLiteral("C (%1)").arg(unit)); p.restore();
        if (points.isEmpty()) { p.drawText(box, Qt::AlignCenter, QStringLiteral("导入有效标气峰值后显示散点")); return; }
        double xmin = points.first().x(), xmax = xmin, ymin = points.first().y(), ymax = ymin;
        for (const auto& v : points) { xmin = std::min(xmin,v.x()); xmax = std::max(xmax,v.x()); ymin=std::min(ymin,v.y()); ymax=std::max(ymax,v.y()); }
        const double dx = xmax > xmin ? xmax-xmin : 1, dy = ymax > ymin ? ymax-ymin : 1;
        xmin-=dx*.08; xmax+=dx*.08; ymin-=dy*.08; ymax+=dy*.08;
        const auto map = [&](double x, double y) { return QPointF(box.left()+(x-xmin)/(xmax-xmin)*box.width(),box.bottom()-(y-ymin)/(ymax-ymin)*box.height()); };
        p.drawText(QRectF(box.left(),box.bottom()+3,100,20), number(xmin));
        p.drawText(QRectF(box.right()-100,box.bottom()+3,100,20), Qt::AlignRight, number(xmax));
        p.drawText(QRectF(22,box.top(),45,30), Qt::AlignRight, number(ymax));
        p.drawText(QRectF(22,box.bottom()-23,45,23), Qt::AlignRight, number(ymin));
        p.save(); p.setClipRect(box);
        if (fit) { p.setPen(QPen(color(AppThemeColor::PlotSeriesWaveOrange),2));
            p.drawLine(map(fit->rMin, std::fma(fit->a,fit->rMin,fit->b)), map(fit->rMax,std::fma(fit->a,fit->rMax,fit->b))); }
        p.setPen(Qt::NoPen); p.setBrush(color(AppThemeColor::Primary));
        for (const auto& v : points) p.drawEllipse(map(v.x(),v.y()),4,4);
        p.restore();
    }
};
struct Work
{
    QString error, message;
    Library library;
    std::optional<Fit> fit;
    TaskImportResult imported;
    CalibrationTask task;
    QVector<ConcentrationResult> results;
    QVector<Fit> calibrations;
};
}
struct WaterCalibrationPage::State
{
    Store store;
    Library library;
    CalibrationTask task = CalibrationTask::create(QStringLiteral("新双波长定标任务"), "ppmv");
    int viewWave = 816;
    QMap<int, std::optional<Fit>> fits;
    QMap<int, bool> dirtyFlags;
    QMap<int, ImportResult> imports;
    Dataset& dataset() { return task.datasets[viewWave]; }
    std::optional<Fit>& fit() { return fits[viewWave]; }
    bool& dirty() { return dirtyFlags[viewWave]; }
    bool anyDirty() const { for (const auto dirty : dirtyFlags) if (dirty) return true; return false; }
    void selectDataset(const Dataset& anchor)
    {
        task.id = anchor.taskId; task.datasets.clear(); task.datasets[anchor.wavelengthNm] = anchor;
        if (!task.id.isEmpty()) for (const auto& data : library.datasets)
            if (data.taskId == task.id && data.wavelengthNm != anchor.wavelengthNm &&
                (!task.datasets.contains(data.wavelengthNm) || data.revision > task.datasets[data.wavelengthNm].revision))
                task.datasets[data.wavelengthNm] = data;
        viewWave = anchor.wavelengthNm; fits.clear(); dirtyFlags.clear(); imports.clear();
        for (const auto& data : task.datasets) for (const auto& fit : library.fits)
            if (fit.dataset.toJson() == data.toJson()) fits[data.wavelengthNm] = fit;
    }
    QVector<ConcentrationResult> results;
    QVector<Fit> resultCalibrations;
    bool busy = false;
    QComboBox *datasets, *unit, *versions;
    QTabBar *wavelengthViews;
    QLabel *channel;
    QLineEdit *name, *session;
    QDoubleSpinBox *concentration;
    QSpinBox *recordPage;
    int sampleOffset = 0;
    QString followFingerprint;
    quint64 resultGeneration = 0;
    QCheckBox *softwareTest, *follow, *singleConcentration;
    QTableWidget *samples, *groups, *output;
    QPlainTextEdit *details;
    QLabel *status, *fitSummary, *activeSummary, *calculationSummary, *resultContext, *importSummary, *sampleSummary;
    CalibrationPlot *plot;
    QWidget *controls;
    QTimer *timer;
    explicit State(const QString& filename) : store(filename.isEmpty() ? Store::defaultFilename() : filename) {}
    void run(WaterCalibrationPage *page, std::function<Work()> job, std::function<void(Work)> done)
    {
        if (busy) return;
        busy = true; controls->setEnabled(false); page->setStatus(QStringLiteral("正在处理…"));
        auto *watcher = new QFutureWatcher<Work>(page);
        QObject::connect(watcher, &QFutureWatcher<Work>::finished, page, [this,page,watcher,done] {
            const auto result = watcher->result(); watcher->deleteLater(); busy = false; controls->setEnabled(true);
            if (!result.error.isEmpty()) { follow->setChecked(false); page->setStatus(result.error,true); return; }
            done(result);
        });
        watcher->setFuture(QtConcurrent::run([job] {
            try { return job(); } catch (const std::exception& e) { Work w; w.error=QStringLiteral("定标操作异常：%1").arg(QString::fromUtf8(e.what())); return w; }
        }));
    }
};

WaterCalibrationPage::WaterCalibrationPage(QWidget *parent, const QString& filename) : QWidget(parent), state_(std::make_unique<State>(filename))
{
    setObjectName(QStringLiteral("waterCalibrationPage"));
    auto& s = *state_;
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(12,12,12,12);
    auto *title = new QLabel(QStringLiteral("水汽标气定标"),this); title->setObjectName("sectionTitleLabel");
    title->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);outer->addWidget(title);
    auto *notice = new QLabel(QStringLiteral("首版验证模型 C = a·R + b。结果为初始浓度，未进行温压补偿；拟合优度不代表仪器精度。"),this);
    notice->setWordWrap(true); outer->addWidget(notice);
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    s.controls = new QWidget(scroll); auto *layout = new QVBoxLayout(s.controls); scroll->setWidget(s.controls); outer->addWidget(scroll,1);
    auto *management = new QGroupBox(QStringLiteral("定标数据与来源"),s.controls); management->setObjectName("sensorGroupBox");
    auto *m = new QVBoxLayout(management);
    auto *tasks = new QHBoxLayout; s.datasets = new QComboBox(management); s.datasets->setObjectName("calibrationDatasets"); tasks->addWidget(s.datasets,1);
    const auto button = [&](const QString& text, const QString& name, QHBoxLayout *row, auto action) {
        auto *b = new QPushButton(text,s.controls); b->setObjectName(name); b->setFocusPolicy(Qt::TabFocus);row->addWidget(b); connect(b,&QPushButton::clicked,this,action); return b;
    };
    button("新建","calibrationNew",tasks,[this]{ if(discardChanges()) newDataset(); });
    button("重新加载","calibrationReload",tasks,[this]{ if(discardChanges()) loadLibrary(); });
    button("存为新修订","calibrationSave",tasks,[this]{ saveDataset(false); });
    button("归档任务","calibrationArchive",tasks,[this]{
        if (QMessageBox::question(this,"归档定标任务","保存归档修订并标记任务已归档？所有已保存历史仍保留，已有启用参数不变。") != QMessageBox::Yes) return;
        for(auto& data:state_->task.datasets){data.archived=true;state_->dirtyFlags[data.wavelengthNm]=true;} saveDataset(false);
    }); m->addLayout(tasks);
    auto *form = new QFormLayout;
    s.name = new QLineEdit(management); s.name->setObjectName("calibrationName"); form->addRow("任务名称",s.name);
    auto *identity = new QHBoxLayout;

    s.channel = new QLabel(management); s.channel->setObjectName("calibrationChannel");
    s.channel->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);
    s.unit = new QComboBox(management); s.unit->addItems({"ppmv","mol/mol","mg/m3"}); s.unit->setObjectName("calibrationUnit");
    identity->addWidget(s.channel,1); identity->addWidget(s.unit,1); form->addRow("双波长固定通道 / 浓度单位",identity);
    auto *mapping = new QLabel("固定光路：816 nm → ADC1 / DLIA1（界面通道2），DAC1 / WMS1（界面WMS2）；\n1368 nm → ADC0 / DLIA0（界面通道1），DAC0 / WMS0（界面WMS1）。不自动换算浓度单位。",management);
    mapping->setWordWrap(true); form->addRow(mapping);
    s.session = new QLineEdit(management); s.session->setObjectName("calibrationSession"); s.session->setPlaceholderText("完整的本地 Session 目录（可正在录制）");
    auto *source = new QHBoxLayout; source->addWidget(s.session,1);
    button("选择 Session","calibrationBrowse",source,[this]{
        const auto directory=VaporView::Ground::Session::GroundRecordingService::defaultRecordingDirectory();
        if(!QDir().mkpath(directory)){setStatus("无法打开 Session 数据目录："+directory,true);return;}
        const auto p=QFileDialog::getExistingDirectory(this,"选择 Session",directory);
        if(!p.isEmpty()) state_->session->setText(p);
    });
    form->addRow("来源 Session",source);
    auto *standard = new QHBoxLayout; s.concentration = new QDoubleSpinBox(management); s.concentration->setDecimals(9); s.concentration->setRange(0,1e12); s.concentration->setObjectName("calibrationKnownConcentration");
    s.softwareTest = new DeviceEnabledCheckBox("软件测试数据",management); s.softwareTest->setToolTip("模拟数据必须勾选；已检测到 Session 模拟标记时也会自动标为软件测试。其余来源标为待核实。");
    standard->addWidget(s.concentration); standard->addWidget(s.softwareTest);
    button("添加标气记录（双波长）","calibrationImport",standard,[this]{ importData(); })
        ->setToolTip("将所选 Session 的峰值按当前标气浓度和单位添加到定标任务；添加后请保存任务。");
    form->addRow("本次标气浓度",standard);
    s.singleConcentration=new DeviceEnabledCheckBox("确认整个 Session 使用同一标气浓度（不支持自动区分 Session 内的浓度切换）",management);
    s.singleConcentration->setObjectName("calibrationSingleConcentration");form->addRow(s.singleConcentration);
    m->addLayout(form); layout->addWidget(management);
    s.importSummary=new QLabel(s.controls);s.importSummary->setObjectName("calibrationImportSummary");s.importSummary->setWordWrap(true);layout->addWidget(s.importSummary);
    s.wavelengthViews=new QTabBar(s.controls);s.wavelengthViews->setObjectName("calibrationWavelengthViews");
    s.wavelengthViews->setDrawBase(false);
    s.wavelengthViews->setExpanding(false);s.wavelengthViews->setAccessibleName("分别查看记录及操作定标参数，导入始终处理整个任务");layout->addWidget(s.wavelengthViews);
    connect(s.wavelengthViews,&QTabBar::currentChanged,this,[this](int index){
        if(index<0)return;state_->viewWave=state_->wavelengthViews->tabData(index).toInt();state_->recordPage->setValue(1);refresh();
    });
    auto *tabs = new QTabWidget(s.controls); layout->addWidget(tabs,1);
    auto *data = new QWidget(tabs); auto *dl = new QVBoxLayout(data);
    s.sampleSummary=new QLabel(data);s.sampleSummary->setObjectName("calibrationSampleSummary");s.sampleSummary->setWordWrap(true);dl->addWidget(s.sampleSummary);
    s.samples = table({"浓度","峰值 R","UTC 采集时间","质量 / 有效性","温度 °C","压力 hPa","备注 / 来源","拟合残差"},data); s.samples->setObjectName("calibrationSamples"); dl->addWidget(s.samples);
    auto *edit = new QHBoxLayout;
    edit->addWidget(new QLabel("记录页（每页 200 条）",data)); s.recordPage=new QSpinBox(data); s.recordPage->setRange(1,1); edit->addWidget(s.recordPage);
    button("编辑浓度 / 备注","calibrationEdit",edit,[this]{
        auto& s=*state_; if(s.samples->currentRow()<0)return;const int row=s.samples->currentRow()+s.sampleOffset; if(row>=s.dataset().samples.size()) return;
        bool ok=false; const double c=QInputDialog::getDouble(this,"编辑已知浓度","浓度（"+s.dataset().unit+"）",s.dataset().samples[row].concentration,0,1e12,9,&ok);
        if(!ok) return;
        const QString note=QInputDialog::getText(this,"编辑备注","备注",QLineEdit::Normal,s.dataset().samples[row].note,&ok); if(!ok) return;
        s.dataset().samples[row].concentration=c; s.dataset().samples[row].note=note; s.dirty()=true; s.fit().reset(); refresh();
    });
    button("排除 / 恢复","calibrationExclude",edit,[this]{ auto& s=*state_; if(s.samples->currentRow()<0)return;const int r=s.samples->currentRow()+s.sampleOffset; s.dataset().samples[r].excluded=!s.dataset().samples[r].excluded; s.dirty()=true; s.fit().reset(); refresh(); });
    button("删除所选记录","calibrationDelete",edit,[this]{
        auto& s=*state_; if(s.samples->currentRow()<0)return;const int r=s.samples->currentRow()+s.sampleOffset;
        if(QMessageBox::question(this,"删除当前记录","从当前编辑修订删除此记录？已保存的旧修订不会修改。")!=QMessageBox::Yes) return;
        s.dataset().samples.removeAt(r); s.dirty()=true; s.fit().reset(); refresh();
    }); dl->addLayout(edit);
    s.groups=table({"标气浓度","有效 / 总样本","峰值均值","标准差","最小值","最大值"},data); s.groups->setMaximumHeight(210); dl->addWidget(s.groups);
    s.details=new QPlainTextEdit(data); s.details->setReadOnly(true); s.details->setMaximumHeight(165); s.details->setPlaceholderText("选择记录查看原始 CSV 字段、Session、周期/segment/tick/sequence 和温压快照来源。"); dl->addWidget(s.details);
    tabs->addTab(data,"标气记录");
    auto *fitPage=new QWidget(tabs); auto *fl=new QVBoxLayout(fitPage); auto *fitActions=new QHBoxLayout;
    button("线性拟合并保存新版本","calibrationFit",fitActions,[this]{ saveDataset(true); }); fl->addLayout(fitActions);
    s.fitSummary=new QLabel(fitPage); s.fitSummary->setWordWrap(true); s.fitSummary->setTextInteractionFlags(Qt::TextSelectableByMouse); fl->addWidget(s.fitSummary);
    s.plot=new CalibrationPlot(fitPage); fl->addWidget(s.plot,1);
    auto *versions=new QHBoxLayout; s.versions=new QComboBox(fitPage); s.versions->setObjectName("calibrationVersions"); versions->addWidget(s.versions,1);
    button("启用此版本","calibrationEnable",versions,[this]{ setActive(true); }); button("停用当前波长","calibrationDisable",versions,[this]{ setActive(false); }); fl->addLayout(versions);
    s.activeSummary=new QLabel(fitPage); s.activeSummary->setWordWrap(true); fl->addWidget(s.activeSummary); tabs->addTab(fitPage,"拟合与版本");
    auto *calculation=new QWidget(tabs); auto *cl=new QVBoxLayout(calculation); auto *calcActions=new QHBoxLayout;
    button("计算来源 Session（两波长）","calibrationCalculate",calcActions,[this]{ calculateSession(); });
    s.follow=new DeviceEnabledCheckBox("自动跟随所选 Session（2 s）",calculation); s.follow->setObjectName("calibrationFollow");s.follow->setChecked(true);calcActions->addWidget(s.follow);
    button("导出可追溯结果","calibrationExport",calcActions,[this]{
        auto& s=*state_; if(s.results.isEmpty()){setStatus("没有可导出的计算结果",true);return;}
        const QSignalBlocker timerBlock(s.timer);
        const auto filename=QFileDialog::getSaveFileName(this,"导出初始浓度及来源",{},"JSON (*.json)"); if(filename.isEmpty()) return;
        exportConcentrationResults(filename);
    }); cl->addLayout(calcActions);
    auto *historyActions=new QHBoxLayout;
    button("读取浓度快照","calibrationLoadResults",historyActions,[this]{
        const QSignalBlocker timerBlock(state_->timer);
        const auto filename=QFileDialog::getOpenFileName(this,"读取已保存浓度与原始定标参数",{},"JSON (*.json)");
        if(!filename.isEmpty())loadConcentrationResults(filename);
    });historyActions->addStretch();cl->addLayout(historyActions);
    auto *explanation=new QLabel("验证用线性模型；选择 Session 后自动计算已落盘周期，无需反复刷新。每次计算使用当前启用版本，不外推、未进行温压补偿。请导出快照以保存历史结果和完整参数；读取快照会暂停自动跟随。远程数据需先同步完整 Session。",calculation); explanation->setWordWrap(true); cl->addWidget(explanation);
    s.calculationSummary=new QLabel(calculation);s.calculationSummary->setObjectName("calibrationCalculationSummary");s.calculationSummary->setWordWrap(true);cl->addWidget(s.calculationSummary);
    s.resultContext=new QLabel(calculation);s.resultContext->setObjectName("calibrationResultContext");s.resultContext->setWordWrap(true);cl->addWidget(s.resultContext);
    s.output=table({"波长 nm","UTC 主机记录时间","峰值 R","初始浓度","单位","状态","数据来源 / 定标版本","DLIA / 记录序号 / Session"},calculation); s.output->setObjectName("calibrationResults");
    s.output->setColumnWidth(1,190);s.output->setColumnWidth(5,260);s.output->setColumnWidth(6,240);
    cl->addWidget(s.output,1); tabs->addTab(calculation,"初始浓度");
    auto *path=new QLabel("定标库："+s.store.filename(),s.controls); path->setWordWrap(true); path->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(path);
    s.status=new QLabel(this); s.status->setWordWrap(true); s.status->setObjectName("calibrationStatus"); s.status->setTextInteractionFlags(Qt::TextSelectableByMouse); outer->addWidget(s.status);
    connect(s.recordPage,qOverload<int>(&QSpinBox::valueChanged),this,[this]{refresh();});
    connect(s.samples,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){ if(row<0)return;row+=state_->sampleOffset;if(row<state_->dataset().samples.size()) state_->details->setPlainText(QString::fromUtf8(QJsonDocument(state_->dataset().samples[row].toJson()).toJson())); });
    connect(s.output,&QTableWidget::cellDoubleClicked,this,[this](int row,int){ row+=state_->output->property("resultOffset").toInt();if(row<0||row>=state_->results.size())return; auto json=state_->results[row].toJson();for(const auto& f:state_->resultCalibrations)if(f.id==state_->results[row].calibrationId){json["calibration"]=f.toJson();break;}auto *dialog=new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("浓度完整追溯");auto *l=new QVBoxLayout(dialog);auto *text=new QPlainTextEdit(dialog);text->setReadOnly(true);text->setPlainText(QString::fromUtf8(QJsonDocument(json).toJson()));l->addWidget(text);dialog->resize(750,550);dialog->show(); });
    const auto changed=[this]{
        auto& s=*state_;for(auto& data:s.task.datasets){data.name=s.name->text();data.unit=s.unit->currentText();s.dirtyFlags[data.wavelengthNm]=true;s.fits[data.wavelengthNm].reset();}
        refresh();
    };
    connect(s.name,&QLineEdit::textEdited,this,changed);connect(s.unit,&QComboBox::activated,this,changed);
    connect(s.datasets,&QComboBox::activated,this,[this](int index){
        auto& s=*state_; if(!discardChanges()) {refresh();return;}
        const int i=s.datasets->itemData(index).toInt();if(i<0||i>=s.library.datasets.size())return;
        s.selectDataset(s.library.datasets[i]);refresh();setStatus("已加载任务；波长页签仅切换查看和独立参数操作，导入处理整个任务");
    });
    connect(s.versions,&QComboBox::activated,this,[this](int index){
        auto& s=*state_;const int i=s.versions->itemData(index).toInt();if(i<0||i>=s.library.fits.size())return;
        const auto f=s.library.fits[i];const bool same=s.dataset().id==f.dataset.id;
        if(!(same?discardChanges(true):discardChanges())){refreshVersions();return;}
        if(!same)s.selectDataset(f.dataset);else s.dataset()=f.dataset;
        s.fit()=f;s.dirty()=false;refresh();setStatus("已查看该波长的原始拟合输入快照；两路启用状态均未改变");
    });
    connect(s.follow,&QCheckBox::toggled,this,[this]{state_->followFingerprint.clear();});
    connect(s.session,&QLineEdit::textChanged,this,[this]{state_->singleConcentration->setChecked(false);clearResults();});
    connect(s.softwareTest,&QCheckBox::toggled,this,[this]{clearResults();});
    s.timer=new QTimer(this); s.timer->setInterval(2000); connect(s.timer,&QTimer::timeout,this,[this]{
        if(!state_->follow->isChecked()||state_->busy||state_->session->text().trimmed().isEmpty())return;
        const auto directory=SessionLoader::resolveSessionDirectory(state_->session->text());
        const auto context=VaporView::Session::loadSessionPathContext(directory);
        const QFileInfo features(VaporView::Session::resolveSessionPath(context,VaporView::Session::SessionFileKind::WaveformFeaturesCsv).absolutePath);
        const QString fingerprint=features.absoluteFilePath()+':'+QString::number(features.size())+':'+QString::number(features.lastModified().toMSecsSinceEpoch())+':'+state_->library.active.value(816)+':'+state_->library.active.value(1368)+':'+QString::number(state_->softwareTest->isChecked());
        if(fingerprint!=state_->followFingerprint){state_->followFingerprint=fingerprint;calculateSession(false);}
    }); s.timer->start();
    setTheme(VaporView::isDarkThemeEnabled());refresh(); QTimer::singleShot(0,this,[this]{loadLibrary(false);});
}
WaterCalibrationPage::~WaterCalibrationPage() = default;
void WaterCalibrationPage::setTheme(bool dark, int fontScalePercent)
{
    setProperty(kAppDarkThemeProperty,dark);setPalette(appThemePalette(dark,palette()));
    const qreal scale=qBound(60,fontScalePercent,180)/100.0;
    // Match the existing FPGA page's scoped tab and secondary-action styling.
    setStyleSheet(applyAppThemeTokens(QStringLiteral(
        "QWidget#waterCalibrationPage { background-color: @vv-window; color: @vv-text; }"
        "QWidget#waterCalibrationPage QLabel { color: @vv-text; background: transparent; border: none; }"
        "QWidget#waterCalibrationPage QLabel#sectionTitleLabel { font-weight: 700; font-size: %1px; }"
        "QWidget#waterCalibrationPage QGroupBox { background: @vv-surface; color: @vv-text; border: 1px solid @vv-border; border-radius: 8px; margin-top: 12px; padding: 12px; }"
        "QWidget#waterCalibrationPage QGroupBox::title { subcontrol-origin: margin; left: 12px; }"
        "QWidget#waterCalibrationPage QTabWidget::pane { border: none; background: @vv-window; }"
        "QWidget#waterCalibrationPage QTabBar::tab { background: @vv-surface-alt; color: @vv-text; border: 1px solid transparent; border-radius: 6px; padding: 6px 14px; margin-right: 4px; font-weight: 600; }"
        "QWidget#waterCalibrationPage QTabBar::tab:selected { background: @vv-primary; color: @vv-white; }"
        "QWidget#waterCalibrationPage QTabBar::tab:hover { background: @vv-primary-subtle; color: @vv-primary; }"
        "QWidget#waterCalibrationPage QTabBar::tab:focus { border-color: @vv-focus; }"
        "QWidget#waterCalibrationPage QScrollArea, QWidget#waterCalibrationPage QScrollArea > QWidget, QWidget#waterCalibrationPage QScrollArea > QWidget > QWidget { border: none; background: @vv-window; }"
        "QWidget#waterCalibrationPage QPushButton { background: @vv-surface-alt; color: @vv-text; border: 1px solid @vv-border; border-radius: 6px; padding: 6px 12px; }"
        "QWidget#waterCalibrationPage QPushButton:hover { background: @vv-primary-subtle; color: @vv-primary; }"
        "QWidget#waterCalibrationPage QPushButton#calibrationFit { background: @vv-primary; color: @vv-white; }"
        "QWidget#waterCalibrationPage QTableWidget, QWidget#waterCalibrationPage QPlainTextEdit { background: @vv-surface; alternate-background-color: @vv-surface-alt; color: @vv-text; gridline-color: @vv-border; }"
        "QWidget#waterCalibrationPage QHeaderView::section { background: @vv-surface-alt; color: @vv-text; border: 1px solid @vv-border; padding: 4px; }"
    ).arg(qRound(18*scale)),dark));
    state_->plot->dark=dark;state_->plot->update();
}
bool WaterCalibrationPage::discardChanges(bool currentOnly)
{
    if(currentOnly?!state_->dirty():!state_->anyDirty())return true;
    return QMessageBox::question(this,"尚未保存",currentOnly?"放弃当前波长未保存的编辑？另一路不变。":"放弃当前任务未保存的编辑？已保存版本仍会保留。",QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Discard;
}
void WaterCalibrationPage::setStatus(const QString& text, bool error, bool publishLog)
{
    state_->status->setText(text); state_->status->setProperty("calibrationError",error);
    if(!publishLog||text==QStringLiteral("正在处理…"))return;
    LogService::withCurrentInstance([&](LogService& log){
        if(error) log.publish(LogLevel::Warning,QStringLiteral("water_calibration"),QStringLiteral("calibration"),text,
            {{QStringLiteral("event"),QStringLiteral("water_calibration_operation_failed")},{QStringLiteral("dataset_id"),state_->dataset().id}});
        else log.publish(LogLevel::Info,QStringLiteral("water_calibration"),QStringLiteral("calibration"),text,
            {{QStringLiteral("event"),QStringLiteral("water_calibration_operation_completed")},{QStringLiteral("dataset_id"),state_->dataset().id}});
    });
}
void WaterCalibrationPage::newDataset(bool announce)
{
    auto& s=*state_;s.task=CalibrationTask::create("新双波长定标任务",s.unit->currentText());s.viewWave=816;s.fits.clear();s.dirtyFlags.clear();s.imports.clear();
    refresh();setStatus("已创建双波长任务；一次选择 Session 和浓度，同时导入两路，分别拟合和启用",false,announce);
}
void WaterCalibrationPage::loadLibrary(bool announce)
{
    auto& s=*state_;const auto store=s.store;
    s.run(this,[store]{Work w;store.load(w.library,&w.error);return w;},[this,announce](Work w){auto& s=*state_;s.library=std::move(w.library);clearResults();newDataset(false);setStatus("定标库已加载；历史任务修订和参数版本可重新选择",false,announce);});
}
void WaterCalibrationPage::importData()
{
    auto& s=*state_;const auto task=s.task;const auto source=s.session->text();const auto concentration=s.concentration->value();
    const bool test=s.softwareTest->isChecked(),confirmed=s.singleConcentration->isChecked();
    s.run(this,[task,source,concentration,test,confirmed]{Work w;w.imported=importTaskSession(source,task,concentration,test,confirmed);if(!w.imported.success)w.error=w.imported.error;return w;},[this](Work w){
        auto& s=*state_;s.imports=w.imported.channels;
        for(auto it=s.imports.begin();it!=s.imports.end();++it)if(!it->samples.isEmpty()){
            s.task.datasets[it.key()].samples+=it->samples;s.dirtyFlags[it.key()]=true;s.fits[it.key()].reset();
        }
        refresh();setStatus("标气记录添加完成（尚未保存）：\n"+s.importSummary->text());
    });
}
void WaterCalibrationPage::saveDataset(bool fit)
{
    auto& s=*state_;const auto task=s.task;const auto library=s.library;const auto store=s.store;const int wave=s.viewWave;
    if(fit&&!s.dataset().hardwareMappingError().isEmpty()){setStatus(s.dataset().hardwareMappingError(),true);return;}
    s.run(this,[task,library,store,fit,wave]{Work w;const auto saved=saveTask(task,library,store,fit?wave:0);
        w.error=saved.error;w.task=saved.task;w.library=saved.library;w.fit=saved.fit;return w;
    },[this,fit,wave](Work w){auto& s=*state_;s.library=std::move(w.library);s.task=std::move(w.task);s.dirtyFlags.clear();
        if(fit)s.fits[wave]=w.fit;refresh();setStatus(fit?QStringLiteral("%1 nm 线性验证模型已拟合并保存新版本；另一路系数和启用状态不变，请检查后手动启用").arg(wave):"任务已原子保存，数据集各自保留历史修订");});
}
void WaterCalibrationPage::setActive(bool enabled)
{
    auto& s=*state_;auto library=s.library;const auto store=s.store;const int wavelength=s.dataset().wavelengthNm;
    const int i=s.versions->currentData().toInt();
    if(enabled&&(s.versions->currentIndex()<0||i<0||i>=library.fits.size())){setStatus("请先拟合或选择已保存版本",true);return;}
    if(enabled&&!library.fits[i].dataset.hardwareMappingError().isEmpty()){setStatus(library.fits[i].dataset.hardwareMappingError(),true);return;}
    library.active[wavelength]=enabled?library.fits[i].id:QString();
    s.run(this,[library,store]()mutable{Work w;w.library=library;store.save(w.library,&w.error);return w;},[this,enabled](Work w){state_->library=std::move(w.library);clearResults();refreshVersions();setStatus(enabled?"已启用所选波长定标版本；请重新计算来源 Session":"已停用当前波长定标版本；旧计算显示已清除");});
}
void WaterCalibrationPage::clearResults()
{
    ++state_->resultGeneration;
    state_->results.clear();state_->resultCalibrations.clear();state_->output->setRowCount(0); state_->output->setProperty("resultOffset",0);
    state_->resultContext->setText("当前输入或版本已改变，等待计算；历史结果请读取已导出的快照。");
    state_->followFingerprint.clear();
}
void WaterCalibrationPage::exportConcentrationResults(const QString& filename)
{
    auto& s=*state_;if(s.busy)return;
    if(s.results.isEmpty()){setStatus("没有可导出的计算结果",true);return;}
    const auto rows=s.results;const auto fits=s.resultCalibrations;
    s.run(this,[filename,rows,fits]{Work w;exportResults(filename,rows,&w.error,fits);return w;},
          [this](Work){setStatus("浓度快照已保存，包含原计算时间、输入记录和完整定标参数，可在重启后独立读取");});
}
void WaterCalibrationPage::loadConcentrationResults(const QString& filename)
{
    auto& s=*state_;if(s.busy)return;
    s.follow->setChecked(false);const auto generation=++s.resultGeneration;const auto legacyFits=s.library.fits;
    s.run(this,[filename,legacyFits]{Work w;const auto snapshot=readResults(filename,legacyFits);w.error=snapshot.error;
        w.results=snapshot.results;w.calibrations=snapshot.calibrations;w.message=snapshot.warning;return w;},
        [this,filename,generation](Work w){auto& s=*state_;if(generation!=s.resultGeneration)return;
            s.results=std::move(w.results);s.resultCalibrations=std::move(w.calibrations);
            refreshResults("历史快照（原计算时间与原定标版本；自动跟随已暂停）："+filename);
            for(const auto& f:s.resultCalibrations)if(!f.dataset.hardwareMappingError().isEmpty()){
                w.message+=QStringLiteral("历史快照包含旧硬件映射，仅供追溯；不能用于当前固定光路计算。");break;
            }
            setStatus(QStringLiteral("已读取并核对 %1 条历史结果。%2").arg(s.results.size()).arg(w.message));});
}
void WaterCalibrationPage::calculateSession(bool announce)
{
    auto& s=*state_;if(s.busy)return;const auto library=s.library;const auto path=s.session->text();const bool test=s.softwareTest->isChecked();
    const auto generation=s.resultGeneration;
    s.results.clear();s.output->setRowCount(0);
    s.run(this,[library,path,test]{Work w;w.calibrations=library.fits;for(const int wavelength:{816,1368}){
        const auto *fit=library.activeFit(wavelength);
        auto d=Dataset::create("计算输入",wavelength,channelForWavelength(wavelength),fit?fit->dataset.unit:"ppmv");
        const auto imported=importSession(path,d,0,test);if(!imported.success){w.error=imported.error;return w;}
        for(const auto& sample:imported.samples)w.results.append(calculate(sample,wavelength,fit));
        if(!imported.warning.isEmpty())w.message+=imported.warning+'\n';
    }
    std::stable_sort(w.results.begin(),w.results.end(),[](const auto& a,const auto& b){return a.input.feature.hostTimeUs<b.input.feature.hostTimeUs;});
    return w;},[this,announce,generation](Work w){auto& s=*state_;
        if(generation!=s.resultGeneration){setStatus("计算输入已更改，已丢弃旧 Session 的计算结果",false,false);return;}
        s.results=std::move(w.results);s.resultCalibrations=std::move(w.calibrations);
        refreshResults("当前 Session 计算快照："+s.session->text()+"（重新计算使用当前启用参数；请导出以保存历史）");
        setStatus(QStringLiteral("已计算 %1 条初始浓度记录；表格显示最近 %2 条，双击可查看来源。%3").arg(s.results.size()).arg(s.output->rowCount()).arg(w.message),false,announce);
    });
}
void WaterCalibrationPage::refreshResults(const QString& context)
{
    auto& s=*state_;s.resultContext->setText(context);s.output->setRowCount(0);
    const int start=std::max(0,int(s.results.size())-2000);
    for(int i=start;i<s.results.size();++i){const auto& r=s.results[i];const int row=s.output->rowCount();s.output->insertRow(row);
        cell(s.output,row,0,QString::number(r.wavelengthNm));cell(s.output,row,1,timeText(r.input.feature.hostTimeUs));cell(s.output,row,2,number(r.input.feature.peak));
        cell(s.output,row,3,r.valid?number(r.value):"—");cell(s.output,row,4,r.unit);
        cell(s.output,row,5,r.status+(r.status.contains("未进行温压补偿")?QString():QStringLiteral("；未进行温压补偿")));
        cell(s.output,row,6,(r.origin=="software_test"?QStringLiteral("软件测试数据"):QStringLiteral("来源待核实"))+" / "+r.calibrationId);
        cell(s.output,row,7,QStringLiteral("DLIA%1 / %2 / %3").arg(r.input.feature.channel-0x30).arg(r.input.feature.waveformSequence).arg(r.input.feature.sessionDirectory));
    }
    s.output->setProperty("resultOffset",start);
}
void WaterCalibrationPage::refreshVersions()
{
    auto& s=*state_;QSignalBlocker blocker(s.versions);s.versions->clear();
    for(int i=0;i<s.library.fits.size();++i){const auto& f=s.library.fits[i];if(f.dataset.wavelengthNm!=s.dataset().wavelengthNm)continue;
        s.versions->addItem(QStringLiteral("%1 · %2 · %3 · %4").arg(f.dataset.name,f.createdUtc,f.dataset.unit,f.id.left(8)),i);
        if(s.fit()&&s.fit()->id==f.id)s.versions->setCurrentIndex(s.versions->count()-1);
    }
    QStringList active;
    for(const int wave:{816,1368}){const auto *f=s.library.activeFit(wave);active<<QStringLiteral("%1 nm：%2").arg(wave).arg(f?QStringLiteral("%1 / DLIA%2 / %3 / %4；验证用线性模型，R∈[%5, %6]，C∈[%7, %8]").arg(f->id).arg(f->dataset.channel-0x30).arg(f->dataset.unit,f->dataset.name,number(f->rMin),number(f->rMax),number(f->cMin),number(f->cMax))+(f->dataset.hardwareMappingError().isEmpty()?QString():QStringLiteral("；旧映射不符，当前计算无效，请停用并重新定标")):QStringLiteral("未启用"));}
    s.activeSummary->setText(active.join('\n'));
    s.calculationSummary->setText("当前启用参数（历史快照使用其保存的原版本）：\n"+active.join('\n'));
}
void WaterCalibrationPage::refresh()
{
    auto& s=*state_;QSignalBlocker taskBlock(s.datasets),nameBlock(s.name),unitBlock(s.unit),pageBlock(s.recordPage),viewBlock(s.wavelengthViews);
    s.datasets->clear();s.datasets->addItem("未保存 / 当前编辑",-1);
    QMap<QString,int> anchors;
    for(int i=0;i<s.library.datasets.size();++i){const auto& d=s.library.datasets[i];
        if(d.taskId.isEmpty()){
            s.datasets->addItem(QStringLiteral("[历史单波长] %1 · %2 nm · 修订 %3%4").arg(d.name).arg(d.wavelengthNm).arg(d.revision).arg(d.archived?" [已归档]":""),i);
            if(s.task.id.isEmpty()&&d.id==s.dataset().id&&d.revision==s.dataset().revision)s.datasets->setCurrentIndex(s.datasets->count()-1);
        }else if(!anchors.contains(d.taskId)||d.wavelengthNm==816)anchors[d.taskId]=i;
    }
    for(auto it=anchors.begin();it!=anchors.end();++it){const auto& d=s.library.datasets[it.value()];
        s.datasets->addItem(QStringLiteral("%1 · 双波长 · %2%3").arg(d.name,d.taskId.left(8),d.archived?" [已归档]":""),it.value());
        if(s.task.id==d.taskId)s.datasets->setCurrentIndex(s.datasets->count()-1);
    }
    while(s.wavelengthViews->count())s.wavelengthViews->removeTab(0);QStringList summaries;
    for(auto it=s.task.datasets.begin();it!=s.task.datasets.end();++it){
        const int wave=it.key();const int tab=s.wavelengthViews->addTab(QStringLiteral("%1 nm · DLIA%2%3").arg(wave).arg(it->channel-0x30).arg(s.dirtyFlags.value(wave)?" *":""));
        s.wavelengthViews->setTabData(tab,wave);if(wave==s.viewWave)s.wavelengthViews->setCurrentIndex(tab);
        const auto batch=s.imports.value(wave);
        summaries<<QStringLiteral("%1 nm：本次导入 %2 条（有效 %3 / 质量不合格 %4），重复跳过 %5。%6").arg(wave).arg(batch.samples.size()).arg(batch.samples.size()-batch.invalid).arg(batch.invalid).arg(batch.duplicates).arg(batch.warning);
    }
    s.importSummary->setText(summaries.join('\n'));
    s.name->setText(s.dataset().name);s.unit->setCurrentText(s.dataset().unit);
    s.channel->setText(s.task.id.isEmpty()?QStringLiteral("历史单波长 %1 nm / DLIA%2%3").arg(s.viewWave).arg(s.dataset().channel-0x30).arg(s.dataset().hardwareMappingError().isEmpty()?QString():"【历史映射不符】"):
        QStringLiteral("816 nm / DLIA1 · 1368 nm / DLIA0"));
    s.channel->setToolTip(s.dataset().hardwareMappingError());
    bool identityEditable=true;for(const auto& data:s.task.datasets)identityEditable&=data.samples.isEmpty()&&data.revision==0;
    s.unit->setEnabled(identityEditable);
    findChild<QPushButton*>("calibrationImport")->setText(s.task.id.isEmpty()?"添加标气记录（历史单波长）":"添加标气记录（双波长）");
    int valid=0,invalid=0,excluded=0;
    for(const auto& sample:s.dataset().samples){if(sample.excluded)++excluded;else if(sample.feature.invalidReason().isEmpty())++valid;else ++invalid;}
    s.sampleSummary->setText(QStringLiteral("%1 nm / DLIA%2 · 数据集 %3 · 修订 %4 · 有效 %5 / 质量不合格 %6 / 已排除 %7 · 任务 %8")
        .arg(s.viewWave).arg(s.dataset().channel-0x30).arg(s.dataset().id).arg(s.dataset().revision).arg(valid).arg(invalid).arg(excluded).arg(s.task.id.isEmpty()?"历史单波长":s.task.id));
    s.recordPage->setMaximum(std::max(1,(int(s.dataset().samples.size())+199)/200));s.sampleOffset=(s.recordPage->value()-1)*200;
    s.samples->setRowCount(0);s.details->clear();s.plot->points.clear();s.plot->unit=s.dataset().unit;s.plot->fit=s.fit();
    QMap<double,QVector<double>> peaks;QMap<double,int> totals;
    int sampleIndex=0,residualIndex=0;const int plotStride=std::max(1,int(s.dataset().samples.size()/2000));
    for(const auto& sample:s.dataset().samples){const auto& f=sample.feature;const auto invalid=f.invalidReason();
        const bool usable=!sample.excluded&&invalid.isEmpty();
        const QString residual=s.fit()&&usable?number(s.fit()->residuals.value(residualIndex++)):QStringLiteral("—");
        ++totals[sample.concentration];if(usable){peaks[sample.concentration].append(f.peak);if(sampleIndex%plotStride==0)s.plot->points.append({f.peak,sample.concentration});}
        const int sourceIndex=sampleIndex++;if(sourceIndex<s.sampleOffset||sourceIndex>=s.sampleOffset+200)continue;
        const int row=s.samples->rowCount();s.samples->insertRow(row);
        cell(s.samples,row,0,number(sample.concentration));cell(s.samples,row,1,number(f.peak));cell(s.samples,row,2,timeText(f.hostTimeUs));
        cell(s.samples,row,3,sample.excluded?"已排除":(!invalid.isEmpty()?invalid:(f.qualityFlags?"有效；已跳过无效采样点":"有效")));
        cell(s.samples,row,4,number(sample.environment.temperatureC));cell(s.samples,row,5,number(sample.environment.pressureHpa));cell(s.samples,row,6,sample.note+" / "+sample.origin+(QFileInfo::exists(f.sessionDirectory)?"":" / 来源 Session 已不存在"));
        cell(s.samples,row,7,residual);
    }
    s.groups->setRowCount(0);
    for(auto it=totals.begin();it!=totals.end();++it){const auto values=peaks.value(it.key());double mean=0,ss=0;for(auto v:values)mean+=v;if(!values.isEmpty())mean/=values.size();for(auto v:values)ss+=(v-mean)*(v-mean);
        const int r=s.groups->rowCount();s.groups->insertRow(r);cell(s.groups,r,0,number(it.key()));cell(s.groups,r,1,QStringLiteral("%1 / %2").arg(values.size()).arg(it.value()));
        cell(s.groups,r,2,values.isEmpty()?"—":number(mean));cell(s.groups,r,3,values.isEmpty()?"—":number(std::sqrt(ss/values.size())));
        cell(s.groups,r,4,values.isEmpty()?"—":number(*std::min_element(values.begin(),values.end())));cell(s.groups,r,5,values.isEmpty()?"—":number(*std::max_element(values.begin(),values.end())));
    }
    if(s.fit()){const auto& f=*s.fit();QStringList residuals;for(int i=0;i<std::min(100,int(f.residuals.size()));++i)residuals<<number(f.residuals[i]);
        s.fitSummary->setText(QStringLiteral("线性最小二乘（程序验证模型，非最终物理公式），v%1\nC = %2 × R + %3 (%4)\n样本 %5，浓度点 %6；R² = %7，RMSE = %8\n浓度范围 [%9, %10]；有效峰值范围 [%11, %12]，禁止静默外推\n时间 %13；模型 %14；数据集 %15，修订 %16\n残差（已知浓度 − 拟合浓度，前 100 条）：%17")
            .arg(f.modelVersion).arg(number(f.a),number(f.b),f.dataset.unit).arg(f.sampleCount).arg(f.concentrationCount).arg(number(f.rSquared),number(f.rmse),number(f.cMin),number(f.cMax),number(f.rMin),number(f.rMax),f.createdUtc,f.id,f.dataset.id).arg(f.dataset.revision).arg(residuals.join(", ")));
    }else s.fitSummary->setText("当前编辑数据尚未拟合。需要至少两个有效样本、两个不同浓度以及可解的峰值变化。历史版本可在下方选择。");
    s.plot->update();refreshVersions();
}
}
