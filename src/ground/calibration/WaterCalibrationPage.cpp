#include "WaterCalibrationPage.h"
#include "WaterCalibration.h"
#include "LogService.h"
#include "shared/theme/AppTheme.h"
#include "ground/session/SessionLoader.h"
#include "shared/session/SessionPathResolver.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
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
    Dataset dataset;
    std::optional<Fit> fit;
    ImportResult imported;
    QVector<ConcentrationResult> results;
};
}
struct WaterCalibrationPage::State
{
    Store store;
    Library library;
    Dataset dataset = Dataset::create(QStringLiteral("新定标任务"),816,0x30,"ppmv");
    std::optional<Fit> fit;
    QVector<ConcentrationResult> results;
    bool dirty = false, busy = false;
    QComboBox *datasets, *wavelength, *channel, *unit, *versions;
    QLineEdit *name, *session;
    QDoubleSpinBox *concentration;
    QSpinBox *recordPage;
    int sampleOffset = 0;
    QString followFingerprint;
    QCheckBox *softwareTest, *follow;
    QTableWidget *samples, *groups, *output;
    QPlainTextEdit *details;
    QLabel *status, *fitSummary, *activeSummary;
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
        state_->dataset.archived = true; state_->dirty = true; saveDataset(false);
    }); m->addLayout(tasks);
    auto *form = new QFormLayout;
    s.name = new QLineEdit(management); s.name->setObjectName("calibrationName"); form->addRow("任务名称",s.name);
    auto *identity = new QHBoxLayout;
    s.wavelength = new QComboBox(management); s.wavelength->addItem("816 nm",816); s.wavelength->addItem("1368 nm",1368); s.wavelength->setObjectName("calibrationWavelength");
    s.channel = new QComboBox(management); s.channel->addItem("DLIA0 (0x30)",0x30); s.channel->addItem("DLIA1 (0x31)",0x31); s.channel->setObjectName("calibrationChannel");
    s.unit = new QComboBox(management); s.unit->addItems({"ppmv","mol/mol","mg/m3"}); s.unit->setObjectName("calibrationUnit");
    identity->addWidget(s.wavelength); identity->addWidget(s.channel); identity->addWidget(s.unit); form->addRow("波长 / 实际硬件映射 / 单位",identity);
    auto *mapping = new QLabel("请核对实际光路与 DLIA 通道对应关系；此处选择是显式映射，程序不推断物理波长。不自动换算浓度单位。",management);
    mapping->setWordWrap(true); form->addRow(mapping);
    s.session = new QLineEdit(management); s.session->setObjectName("calibrationSession"); s.session->setPlaceholderText("完整的本地 Session 目录（可正在录制）");
    auto *source = new QHBoxLayout; source->addWidget(s.session,1);
    button("选择 Session","calibrationBrowse",source,[this]{ const auto p=QFileDialog::getExistingDirectory(this,"选择 Session",state_->session->text()); if(!p.isEmpty()) state_->session->setText(p); });
    form->addRow("来源 Session",source);
    auto *standard = new QHBoxLayout; s.concentration = new QDoubleSpinBox(management); s.concentration->setDecimals(9); s.concentration->setRange(0,1e12); s.concentration->setObjectName("calibrationKnownConcentration");
    s.softwareTest = new QCheckBox("软件测试数据",management); s.softwareTest->setToolTip("模拟数据必须勾选；已检测到 Session 模拟标记时也会自动标为软件测试。其余来源标为待核实。");
    standard->addWidget(s.concentration); standard->addWidget(s.softwareTest);
    button("导入峰值记录","calibrationImport",standard,[this]{ importData(); }); form->addRow("本次标气浓度",standard); m->addLayout(form); layout->addWidget(management);
    auto *tabs = new QTabWidget(s.controls); layout->addWidget(tabs,1);
    auto *data = new QWidget(tabs); auto *dl = new QVBoxLayout(data);
    s.samples = table({"浓度","峰值 R","UTC 采集时间","质量 / 有效性","温度 °C","压力 hPa","备注 / 来源","拟合残差"},data); s.samples->setObjectName("calibrationSamples"); dl->addWidget(s.samples);
    auto *edit = new QHBoxLayout;
    edit->addWidget(new QLabel("记录页（每页 200 条）",data)); s.recordPage=new QSpinBox(data); s.recordPage->setRange(1,1); edit->addWidget(s.recordPage);
    button("编辑浓度 / 备注","calibrationEdit",edit,[this]{
        auto& s=*state_; if(s.samples->currentRow()<0)return;const int row=s.samples->currentRow()+s.sampleOffset; if(row>=s.dataset.samples.size()) return;
        bool ok=false; const double c=QInputDialog::getDouble(this,"编辑已知浓度","浓度（"+s.dataset.unit+"）",s.dataset.samples[row].concentration,0,1e12,9,&ok);
        if(!ok) return;
        const QString note=QInputDialog::getText(this,"编辑备注","备注",QLineEdit::Normal,s.dataset.samples[row].note,&ok); if(!ok) return;
        s.dataset.samples[row].concentration=c; s.dataset.samples[row].note=note; s.dirty=true; s.fit.reset(); refresh();
    });
    button("排除 / 恢复","calibrationExclude",edit,[this]{ auto& s=*state_; if(s.samples->currentRow()<0)return;const int r=s.samples->currentRow()+s.sampleOffset; s.dataset.samples[r].excluded=!s.dataset.samples[r].excluded; s.dirty=true; s.fit.reset(); refresh(); });
    button("删除所选记录","calibrationDelete",edit,[this]{
        auto& s=*state_; if(s.samples->currentRow()<0)return;const int r=s.samples->currentRow()+s.sampleOffset;
        if(QMessageBox::question(this,"删除当前记录","从当前编辑修订删除此记录？已保存的旧修订不会修改。")!=QMessageBox::Yes) return;
        s.dataset.samples.removeAt(r); s.dirty=true; s.fit.reset(); refresh();
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
    s.follow=new QCheckBox("跟随已落盘的新周期（2 s）",calculation); calcActions->addWidget(s.follow);
    button("导出可追溯结果","calibrationExport",calcActions,[this]{
        auto& s=*state_; if(s.results.isEmpty()){setStatus("没有可导出的计算结果",true);return;}
        const auto filename=QFileDialog::getSaveFileName(this,"导出初始浓度及来源",{},"JSON (*.json)"); if(filename.isEmpty()) return;
        const auto rows=s.results;
        s.run(this,[filename,rows]{ Work w; exportResults(filename,rows,&w.error);return w; },[this](Work){setStatus("浓度及完整追溯信息已导出");});
    }); cl->addLayout(calcActions);
    auto *explanation=new QLabel("使用各波长已启用的保存版本。读取完整 Session 的现有峰值索引，不使用抽稀预览；远程数据需先同步 Session。结果保留原始输入，默认不外推。",calculation); explanation->setWordWrap(true); cl->addWidget(explanation);
    s.output=table({"波长 nm","UTC 采集时间","峰值 R","初始浓度","单位","状态","定标版本 / 数据来源"},calculation); s.output->setObjectName("calibrationResults"); cl->addWidget(s.output,1); tabs->addTab(calculation,"初始浓度");
    auto *path=new QLabel("定标库："+s.store.filename(),s.controls); path->setWordWrap(true); path->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(path);
    s.status=new QLabel(this); s.status->setWordWrap(true); s.status->setObjectName("calibrationStatus"); s.status->setTextInteractionFlags(Qt::TextSelectableByMouse); outer->addWidget(s.status);
    connect(s.recordPage,qOverload<int>(&QSpinBox::valueChanged),this,[this]{refresh();});
    connect(s.samples,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){ if(row<0)return;row+=state_->sampleOffset;if(row<state_->dataset.samples.size()) state_->details->setPlainText(QString::fromUtf8(QJsonDocument(state_->dataset.samples[row].toJson()).toJson())); });
    connect(s.output,&QTableWidget::cellDoubleClicked,this,[this](int row,int){ row+=state_->output->property("resultOffset").toInt();if(row<0||row>=state_->results.size())return; auto *dialog=new QDialog(this); dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("浓度完整追溯");auto *l=new QVBoxLayout(dialog);auto *text=new QPlainTextEdit(dialog);text->setReadOnly(true);text->setPlainText(QString::fromUtf8(QJsonDocument(state_->results[row].toJson()).toJson()));l->addWidget(text);dialog->resize(750,550);dialog->show(); });
    const auto changed=[this]{ auto& s=*state_;s.dataset.name=s.name->text();s.dataset.wavelengthNm=s.wavelength->currentData().toInt();s.dataset.channel=s.channel->currentData().toUInt();s.dataset.unit=s.unit->currentText();s.dirty=true;s.fit.reset();s.plot->fit.reset();s.plot->update();s.fitSummary->setText("数据已编辑，尚未拟合并保存当前修订。");refreshVersions(); };
    connect(s.name,&QLineEdit::textEdited,this,changed);
    connect(s.wavelength,&QComboBox::activated,this,[this,changed]{state_->channel->setCurrentIndex(state_->wavelength->currentIndex());changed();});
    connect(s.channel,&QComboBox::activated,this,changed); connect(s.unit,&QComboBox::activated,this,changed);
    connect(s.datasets,&QComboBox::activated,this,[this](int index){
        auto& s=*state_; if(!discardChanges()) {refresh();return;}
        const int i=s.datasets->itemData(index).toInt();if(i<0||i>=s.library.datasets.size())return;
        s.dataset=s.library.datasets[i];s.dirty=false;s.fit.reset();refresh();setStatus("已加载数据集修订；编辑后保存为新修订");
    });
    connect(s.versions,&QComboBox::activated,this,[this](int index){ const int i=state_->versions->itemData(index).toInt();if(i<0||i>=state_->library.fits.size())return;const auto f=state_->library.fits[i];if(!discardChanges()){refreshVersions();return;}state_->fit=f;state_->dataset=f.dataset;state_->dirty=false;refresh();setStatus("已加载不可变的拟合输入快照和历史参数"); });
    connect(s.follow,&QCheckBox::toggled,this,[this]{state_->followFingerprint.clear();});
    s.timer=new QTimer(this); s.timer->setInterval(2000); connect(s.timer,&QTimer::timeout,this,[this]{
        if(!state_->follow->isChecked()||state_->busy)return;
        const auto directory=SessionLoader::resolveSessionDirectory(state_->session->text());
        const auto context=VaporView::Session::loadSessionPathContext(directory);
        const QFileInfo features(VaporView::Session::resolveSessionPath(context,VaporView::Session::SessionFileKind::WaveformFeaturesCsv).absolutePath);
        const QString fingerprint=features.absoluteFilePath()+':'+QString::number(features.size())+':'+QString::number(features.lastModified().toMSecsSinceEpoch())+':'+state_->library.active.value(816)+':'+state_->library.active.value(1368)+':'+QString::number(state_->softwareTest->isChecked());
        if(fingerprint!=state_->followFingerprint){state_->followFingerprint=fingerprint;calculateSession();}
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
        "QWidget#waterCalibrationPage QScrollArea { border: none; background: @vv-window; }"
        "QWidget#waterCalibrationPage QPushButton { background: @vv-surface-alt; color: @vv-text; border: 1px solid @vv-border; border-radius: 6px; padding: 6px 12px; }"
        "QWidget#waterCalibrationPage QPushButton:hover { background: @vv-primary-subtle; color: @vv-primary; }"
        "QWidget#waterCalibrationPage QPushButton#calibrationFit { background: @vv-primary; color: @vv-white; }"
        "QWidget#waterCalibrationPage QTableWidget, QWidget#waterCalibrationPage QPlainTextEdit { background: @vv-surface; alternate-background-color: @vv-surface-alt; color: @vv-text; gridline-color: @vv-border; }"
        "QWidget#waterCalibrationPage QHeaderView::section { background: @vv-surface-alt; color: @vv-text; border: 1px solid @vv-border; padding: 4px; }"
    ).arg(qRound(18*scale)),dark));
    state_->plot->dark=dark;state_->plot->update();
}
bool WaterCalibrationPage::discardChanges()
{
    if(!state_->dirty) return true;
    return QMessageBox::question(this,"尚未保存","放弃当前未保存的编辑？已保存版本仍会保留。",QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel)==QMessageBox::Discard;
}
void WaterCalibrationPage::setStatus(const QString& text, bool error, bool publishLog)
{
    state_->status->setText(text); state_->status->setProperty("calibrationError",error);
    if(!publishLog||text==QStringLiteral("正在处理…"))return;
    LogService::withCurrentInstance([&](LogService& log){
        if(error) log.publish(LogLevel::Warning,QStringLiteral("water_calibration"),QStringLiteral("calibration"),text,
            {{QStringLiteral("event"),QStringLiteral("water_calibration_operation_failed")},{QStringLiteral("dataset_id"),state_->dataset.id}});
        else log.publish(LogLevel::Info,QStringLiteral("water_calibration"),QStringLiteral("calibration"),text,
            {{QStringLiteral("event"),QStringLiteral("water_calibration_operation_completed")},{QStringLiteral("dataset_id"),state_->dataset.id}});
    });
}
void WaterCalibrationPage::newDataset(bool announce)
{
    auto& s=*state_;s.dataset=Dataset::create("新定标任务",s.wavelength->currentData().toInt(),s.channel->currentData().toUInt(),s.unit->currentText());s.dirty=false;s.fit.reset();refresh();setStatus("已创建未保存任务；请填写名称并核对波长、DLIA 映射和浓度单位",false,announce);
}
void WaterCalibrationPage::loadLibrary(bool announce)
{
    auto& s=*state_;const auto store=s.store;
    s.run(this,[store]{Work w;store.load(w.library,&w.error);return w;},[this,announce](Work w){auto& s=*state_;s.library=std::move(w.library);newDataset(false);setStatus("定标库已加载；历史任务修订和参数版本可重新选择",false,announce);});
}
void WaterCalibrationPage::importData()
{
    auto& s=*state_;const auto data=s.dataset;const auto source=s.session->text();const auto concentration=s.concentration->value();const bool test=s.softwareTest->isChecked();
    s.run(this,[data,source,concentration,test]{Work w;w.imported=importSession(source,data,concentration,test);if(!w.imported.success)w.error=w.imported.error;return w;},[this](Work w){
        auto& s=*state_;s.dataset.samples+=w.imported.samples;s.dirty=s.dirty||!w.imported.samples.isEmpty();s.fit.reset();refresh();
        setStatus(QStringLiteral("导入 %1 条，其中质量不合格 %2 条（仅保留追溯，不参与拟合）；跳过已导入记录 %3 条。%4").arg(w.imported.samples.size()).arg(w.imported.invalid).arg(w.imported.duplicates).arg(w.imported.warning));
    });
}
void WaterCalibrationPage::saveDataset(bool fit)
{
    auto& s=*state_;auto data=s.dataset;auto library=s.library;const auto store=s.store;
    int latest=0;for(const auto& d:library.datasets)if(d.id==data.id)latest=std::max(latest,d.revision);
    data.revision=latest+1;data.modifiedUtc=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    s.run(this,[data,library,store,fit]()mutable{Work w;w.dataset=data;w.library=library;w.error=data.validate();if(!w.error.isEmpty())return w;
        if(fit){const auto result=FitModel::create("linear",1)->fit(data);if(!result.success){w.error=result.error;return w;}w.fit=result.fit;}
        w.library.datasets.append(data);if(w.fit)w.library.fits.append(*w.fit);store.save(w.library,&w.error);return w;
    },[this,fit](Work w){auto& s=*state_;s.library=std::move(w.library);s.dataset=std::move(w.dataset);s.fit=w.fit;s.dirty=false;refresh();
        setStatus(fit?"线性验证模型拟合成功并保存为新版本；请检查范围、残差并手动启用":"数据集已保存为新修订，历史版本已保留");});
}
void WaterCalibrationPage::setActive(bool enabled)
{
    auto& s=*state_;auto library=s.library;const auto store=s.store;const int wavelength=s.dataset.wavelengthNm;
    const int i=s.versions->currentData().toInt();
    if(enabled&&(s.versions->currentIndex()<0||i<0||i>=library.fits.size())){setStatus("请先拟合或选择已保存版本",true);return;}
    library.active[wavelength]=enabled?library.fits[i].id:QString();
    s.run(this,[library,store]()mutable{Work w;w.library=library;store.save(w.library,&w.error);return w;},[this,enabled](Work w){state_->library=std::move(w.library);refreshVersions();setStatus(enabled?"已启用所选波长定标版本":"已停用当前波长定标版本");});
}
void WaterCalibrationPage::calculateSession()
{
    auto& s=*state_;const auto library=s.library;const auto path=s.session->text();const bool test=s.softwareTest->isChecked();
    s.results.clear();s.output->setRowCount(0);
    s.run(this,[library,path,test]{Work w;for(const int wavelength:{816,1368}){
        const auto *fit=library.activeFit(wavelength);
        auto d=Dataset::create("计算输入",wavelength,fit?fit->dataset.channel:(wavelength==816?0x30:0x31),fit?fit->dataset.unit:"ppmv");
        const auto imported=importSession(path,d,0,test);if(!imported.success){w.error=imported.error;return w;}
        for(const auto& sample:imported.samples)w.results.append(calculate(sample,wavelength,fit));
        if(!imported.warning.isEmpty())w.message+=imported.warning+'\n';
    }
    std::stable_sort(w.results.begin(),w.results.end(),[](const auto& a,const auto& b){return a.input.feature.hostTimeUs<b.input.feature.hostTimeUs;});
    return w;},[this](Work w){auto& s=*state_;s.results=std::move(w.results);s.output->setRowCount(0);
        // Keep the view bounded; exports retain every computed record.
        const int start=std::max(0,int(s.results.size())-2000);
        for(int i=start;i<s.results.size();++i){const auto& r=s.results[i];const int row=s.output->rowCount();s.output->insertRow(row);
            cell(s.output,row,0,QString::number(r.wavelengthNm));cell(s.output,row,1,timeText(r.input.feature.hostTimeUs));cell(s.output,row,2,number(r.input.feature.peak));
            cell(s.output,row,3,r.valid?number(r.value):"—");cell(s.output,row,4,r.unit);cell(s.output,row,5,r.status);cell(s.output,row,6,r.calibrationId+" / "+r.origin);
        }
        // Store the offset for full provenance lookup after bounded rendering.
        s.output->setProperty("resultOffset",start);
        setStatus(QStringLiteral("已计算 %1 条初始浓度记录；表格显示最近 %2 条，双击可查看来源。%3").arg(s.results.size()).arg(s.output->rowCount()).arg(w.message));
    });
}
void WaterCalibrationPage::refreshVersions()
{
    auto& s=*state_;QSignalBlocker blocker(s.versions);s.versions->clear();
    for(int i=0;i<s.library.fits.size();++i){const auto& f=s.library.fits[i];if(f.dataset.wavelengthNm!=s.dataset.wavelengthNm)continue;
        s.versions->addItem(QStringLiteral("%1 · %2 · %3 · %4").arg(f.dataset.name,f.createdUtc,f.dataset.unit,f.id.left(8)),i);
        if(s.fit&&s.fit->id==f.id)s.versions->setCurrentIndex(s.versions->count()-1);
    }
    QStringList active;
    for(const int wave:{816,1368}){const auto *f=s.library.activeFit(wave);active<<QStringLiteral("%1 nm：%2").arg(wave).arg(f?QStringLiteral("%1 / DLIA%2 / %3 / %4").arg(f->id).arg(f->dataset.channel-0x30).arg(f->dataset.unit,f->dataset.name):QStringLiteral("未启用"));}
    s.activeSummary->setText(active.join('\n'));
}
void WaterCalibrationPage::refresh()
{
    auto& s=*state_;QSignalBlocker taskBlock(s.datasets),nameBlock(s.name),waveBlock(s.wavelength),channelBlock(s.channel),unitBlock(s.unit),pageBlock(s.recordPage);
    s.datasets->clear();s.datasets->addItem("未保存 / 当前编辑",-1);
    for(int i=0;i<s.library.datasets.size();++i){const auto& d=s.library.datasets[i];s.datasets->addItem(QStringLiteral("%1 · %2 nm · 修订 %3%4").arg(d.name).arg(d.wavelengthNm).arg(d.revision).arg(d.archived?" [已归档]":""),i);
        if(d.id==s.dataset.id&&d.revision==s.dataset.revision)s.datasets->setCurrentIndex(s.datasets->count()-1);}
    s.name->setText(s.dataset.name);s.wavelength->setCurrentIndex(s.wavelength->findData(s.dataset.wavelengthNm));s.channel->setCurrentIndex(s.channel->findData(int(s.dataset.channel)));s.unit->setCurrentText(s.dataset.unit);
    // A saved dataset's identity cannot be relabelled; create another task instead.
    const bool identityEditable=s.dataset.samples.isEmpty()&&s.dataset.revision==0;
    s.wavelength->setEnabled(identityEditable);s.channel->setEnabled(identityEditable);s.unit->setEnabled(identityEditable);
    s.recordPage->setMaximum(std::max(1,(int(s.dataset.samples.size())+199)/200));s.sampleOffset=(s.recordPage->value()-1)*200;
    s.samples->setRowCount(0);s.details->clear();s.plot->points.clear();s.plot->unit=s.dataset.unit;s.plot->fit=s.fit;
    QMap<double,QVector<double>> peaks;QMap<double,int> totals;
    int sampleIndex=0,residualIndex=0;const int plotStride=std::max(1,int(s.dataset.samples.size()/2000));
    for(const auto& sample:s.dataset.samples){const auto& f=sample.feature;const auto invalid=f.invalidReason();
        const bool usable=!sample.excluded&&invalid.isEmpty();
        const QString residual=s.fit&&usable?number(s.fit->residuals.value(residualIndex++)):QStringLiteral("—");
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
    if(s.fit){const auto& f=*s.fit;QStringList residuals;for(int i=0;i<std::min(100,int(f.residuals.size()));++i)residuals<<number(f.residuals[i]);
        s.fitSummary->setText(QStringLiteral("线性最小二乘（程序验证模型，非最终物理公式），v%1\nC = %2 × R + %3 (%4)\n样本 %5，浓度点 %6；R² = %7，RMSE = %8\n浓度范围 [%9, %10]；有效峰值范围 [%11, %12]，禁止静默外推\n时间 %13；模型 %14；数据集 %15，修订 %16\n残差（已知浓度 − 拟合浓度，前 100 条）：%17")
            .arg(f.modelVersion).arg(number(f.a),number(f.b),f.dataset.unit).arg(f.sampleCount).arg(f.concentrationCount).arg(number(f.rSquared),number(f.rmse),number(f.cMin),number(f.cMax),number(f.rMin),number(f.rMax),f.createdUtc,f.id,f.dataset.id).arg(f.dataset.revision).arg(residuals.join(", ")));
    }else s.fitSummary->setText("当前编辑数据尚未拟合。需要至少两个有效样本、两个不同浓度以及可解的峰值变化。历史版本可在下方选择。");
    s.plot->update();refreshVersions();
}
}
