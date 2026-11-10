#include "test_settings_sandbox.h"
#include "IndependentCalibrationFixture.h"
#include "ground/calibration/WaterCalibrationPage.h"
#include "ground/calibration/CalibrationReviewPlot.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QThread>
#include <QLayout>
#include <QScreen>
using namespace IndependentCalibrationTest;
template<class T>T *control(QWidget& page,const char *id){auto *w=page.findChild<T*>(id);check(w,"named independent UI control exists");return w;}
void largeReview()
{
    Rig r;auto file=r.complete();auto project=file.project;project.stages.reserve(20);auto& stage=project.stages[0];
    stage.configuredMs=stage.effectiveMs=stage.wallMs=600000;
    for(int wave:{816,1368}) {
        const auto original=stage.samples[wave].first();const auto stream=calibrationUuid();auto& rows=stage.samples[wave];rows.clear();rows.reserve(24000);
        for(int i=0;i<24000;++i) {
            auto record=original;auto& f=record.feature;f.streamIdentity=stream;f.waveformSequence=quint64(i+1);f.cycleId=quint32(i+1);
            f.hostTimeUs=1700000000000000ULL+quint64(i)*25000;
            f.id=QString::fromLatin1(QCryptographicHash::hash((f.streamIdentity+'|'+QString::number(f.channel)+'|'+QString::number(f.waveformSequence)).toUtf8(),QCryptographicHash::Sha256).toHex());
            f.peak=i==10001?9.0:i==0?1.0:2.0;rows.append(record);
        }
    }
    for(int i=3;i<20;++i){auto next=ConcentrationStage::create(100*(i+1));next.note=QStringLiteral("中文长备注：软件模拟阶段，用于滚动和窄窗口验证");project.stages.append(next);}
    project.name=QStringLiteral("中文长任务名称：双波长软件模拟审核与原始记录完整性检查");
    check(project.fit().isEmpty(),"large UI fixture has two valid fits");
    std::cout<<"Large UI fixture created and fitted."<<std::endl;
    const double originalA=project.fits[816].parameters.a;
    CalibrationReviewPlot plot;plot.resize(1000,450);plot.setStage(stage,816);plot.show();QApplication::processEvents();
    // The unique high peak is at index 10001, omitted by the 3:1 display stride.
    const QRectF area=QRectF(plot.rect()).adjusted(64,20,-20,-42);
    const QPoint at(qRound(area.left()+double(10001)/23999*area.width()),
        qRound(area.bottom()-(9.0-0.36)/9.28*area.height()));
    auto gesture=[&](QPoint from,QPoint to){
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(from),plot.mapToGlobal(from),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(to),plot.mapToGlobal(to),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&plot,&press);QApplication::sendEvent(&plot,&release);
    };
    gesture(at,at);check(plot.selection()==QSet<QString>{stage.samples[816][10001].feature.id},"click selects raw point omitted by display thinning");
    QElapsedTimer timer;timer.start();gesture(QPoint(1,1),QPoint(plot.width()-1,plot.height()-1));const auto selectMs=timer.elapsed();
    check(plot.selection().size()==24000,"rectangle selects all raw records, including points omitted by display thinning");
    const auto id=stage.samples[816][10001].feature.id;QString error;
    check(project.exclude(stage.id,816,{id},true,QStringLiteral("测试剔除抽稀未绘点"),&error),qPrintable(error));
    check(stage.statistics(816).raw==24000&&stage.statistics(816).used==23999&&stage.statistics(1368).used==24000,"exclusion changes only selected wavelength statistics, never raw count");
    check(project.exclude(stage.id,816,{id},false,QStringLiteral("测试恢复原始记录"),&error)&&project.fit().isEmpty(),"restore full raw record and refit");
    check(project.fits[816].parameters.a==originalA,"display thinning and restored selection do not change full-data fit");
    std::cout<<"Full selection, exclusion and restored fit verified."<<std::endl;
    CalibrationFileStore store(r.root.path());check(store.saveDraft(project,&error),qPrintable(error));
    r.service->openProject(store.drafts().first(),true);r.service->synchronize();
    std::cout<<"Large UI draft saved and reopened."<<std::endl;
    plot.hide();
    WaterCalibrationPage page(nullptr,r.root.path(),r.service);page.resize(1280,1000);page.show();
    timer.restart();QApplication::processEvents();const auto initialMs=timer.elapsed();
    auto *tabs=control<QTabWidget>(page,"calibrationWorkspaces");tabs->setCurrentIndex(1);
    QApplication::processEvents();
    auto *table=control<QTableWidget>(page,"calibrationSamples");auto *pager=control<QSpinBox>(page,"calibrationSamplePage");
    check(table->rowCount()==500&&pager->maximum()==48,"large table pages all raw records with bounded 500-row views");
    timer.restart();pager->setValue(21);QApplication::processEvents();const auto pageMs=timer.elapsed();
    check(table->item(1,0)->text()==id,"pagination retains exact original raw record ID");
    check(pageMs<5000,"one 500-row page does not block the GUI for seconds");
    for(int scale:{100,130})for(bool dark:{false,true})for(int width:{900,1280}) {
        page.setTheme(dark,scale);page.resize(width,1000);QApplication::processEvents();
        check(page.grab().width()>0,"large-record narrow and normal UI renders in both themes and scales");
        auto *reason=control<QLineEdit>(page,"calibrationExclusionReason");
        check(std::abs(reason->palette().color(QPalette::PlaceholderText).lightness()-reason->palette().color(QPalette::Base).lightness())>50,"exclusion reason placeholder remains readable in both themes");
    }
    CalibrationFile longFile;const auto longName=QStringLiteral("中文文件名_双波长定标_用于正式录制前核对_").repeated(4);
    check(store.copy(file.filename,longName,longFile,&error),qPrintable(error));
    r.service->refreshFiles();r.service->selectFile(longFile.filename);r.service->synchronize();QApplication::processEvents();
    QJsonObject snapshot;check(r.service->prepare(snapshot,&error),qPrintable(error));QApplication::processEvents();
    page.layout()->activate();QApplication::processEvents();
    auto *currentLabel=control<QLabel>(page,"calibrationCurrentFile");const auto current=currentLabel->text();
    check(current.contains(QFileInfo(longFile.filename).fileName())&&current.contains(QStringLiteral("Session 已锁定")),"long Chinese selected filename remains explicit while Session is locked");
    check(currentLabel->height()>=currentLabel->heightForWidth(currentLabel->width()),"wrapped current filename and lock state fit the label after layout");
    r.service->finish();r.service->selectFile("missing.vcal");r.service->synchronize();QApplication::processEvents();
    check(r.service->view().error&&!control<QLabel>(page,"calibrationStatus")->text().isEmpty(),"invalid file selection has a visible failure reason");
    r.service->exclude(stage.id,816,{id},true,QStringLiteral("检查过期拟合提示"));r.service->synchronize();QApplication::processEvents();
    bool staleVisible=false;for(auto *label:page.findChildren<QLabel*>())staleVisible|=label->text().contains(QStringLiteral("尚无当前修订的拟合"));
    check(staleVisible,"editing raw review state visibly invalidates the previous fitted revision");
    r.pair(2,4);r.service->startStage(3);r.service->synchronize();check(r.service->view().acquiring,"large task can acquire a fresh stage while its review is visible");
    std::atomic<bool> done{false};std::thread producer([&]{for(int i=0;i<500;++i){++r.now;r.pair(2,4);QThread::msleep(1);}done=true;});
    QElapsedTimer pump;pump.start();qint64 maximumPumpMs=0;
    while(!done){QApplication::processEvents(QEventLoop::AllEvents,10);maximumPumpMs=std::max(maximumPumpMs,pump.restart());QThread::msleep(1);}
    producer.join();r.service->synchronize();
    const auto acquired=r.service->view().project.stages[3];
    check(acquired.samples[816].size()==500&&acquired.samples[1368].size()==500,"visible large-record review does not lose either acquisition lane");
    std::cout<<"Visible UI acquisition: max_event_pump_ms="<<maximumPumpMs<<" diagnostics="<<QJsonDocument(r.service->diagnostics()).toJson(QJsonDocument::Compact).constData()<<std::endl;
    r.bus.setDevice(false,{});r.service->synchronize();QApplication::processEvents();
    check(!r.service->view().acquiring&&r.service->view().project.stages[3].status=="paused","disconnect visibly pauses a running stage without false completion");
    check(control<QLabel>(page,"calibrationLiveConcentration")->text().contains(QStringLiteral("设备未连接")),"offline state is explicit");
    tabs->setCurrentIndex(0);QApplication::processEvents();check(control<QTableWidget>(page,"calibrationStages")->rowCount()==20,"all concentration stages remain accessible");
    page.setTheme(false,100);page.close();plot.close();
    std::cout<<"SOFTWARE_SIMULATION_ONLY: review_raw_per_lane=24000 rectangle_select_ms="<<selectMs<<" initial_refresh_ms="<<initialMs<<" page_switch_ms="<<pageMs<<'\n';
}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    if(app.arguments().value(1)=="--dpi-check") {
        std::cout<<"Qt primary screen DPI scale="<<app.primaryScreen()->devicePixelRatio()<<std::endl;
        check(std::abs(app.primaryScreen()->devicePixelRatio()-app.arguments().value(2).toDouble())<0.01,"Qt renders at the requested process DPI scale");
    }
    Rig r;WaterCalibrationPage page(nullptr,r.root.path(),r.service);page.resize(1280,1000);page.show();
    auto *name=control<QLineEdit>(page,"calibrationTaskName");auto *unit=control<QComboBox>(page,"calibrationUnit");
    auto *create=control<QPushButton>(page,"calibrationCreateTask");auto *viewport=name->parentWidget()->parentWidget();
    for(int scale:{100,130})for(int width:{900,1280}) {
        page.setTheme(false,scale);page.resize(width,1000);QApplication::processEvents();
        const QRect nameRect(name->mapTo(&page,QPoint()),name->size());const QRect unitRect(unit->mapTo(&page,QPoint()),unit->size());
        const QRect createRect(create->mapTo(&page,QPoint()),create->size());const QRect viewportRect(viewport->mapTo(&page,QPoint()),viewport->size());
        check(nameRect.center().y()==unitRect.center().y()&&unitRect.center().y()==createRect.center().y(),"task controls share one row");
        check(nameRect.right()<unitRect.left()&&unitRect.right()<createRect.left(),"task controls are ordered without overlap");
        check(viewportRect.contains(nameRect)&&viewportRect.contains(unitRect)&&viewportRect.contains(createRect),"task controls fit the visible workspace");
    }
    page.setTheme(false,100);page.resize(1280,1000);QApplication::processEvents();
    control<QLineEdit>(page,"calibrationTaskName")->setText(QStringLiteral("软件测试独立任务"));
    control<QPushButton>(page,"calibrationCreateTask")->click();r.service->synchronize();QApplication::processEvents();
    check(control<QSpinBox>(page,"calibrationStageSeconds")->value()==600,"UI ten-minute default");
    control<QDoubleSpinBox>(page,"calibrationStageConcentration")->setValue(100);control<QSpinBox>(page,"calibrationStageSeconds")->setValue(2);
    control<QPushButton>(page,"calibrationAddStage")->click();r.service->synchronize();QApplication::processEvents();
    auto *stages=control<QTableWidget>(page,"calibrationStages");check(stages->rowCount()==1,"stage-centered UI");stages->selectRow(0);
    r.pair(2,4);control<QCheckBox>(page,"calibrationStableConfirmed")->setChecked(true);control<QPushButton>(page,"calibrationStartStage")->click();r.service->synchronize();
    control<QTabWidget>(page,"calibrationWorkspaces")->setCurrentIndex(2);page.hide();
    for(int i=0;i<4;++i){r.now+=500;r.pair(2,4);}check(r.service->view().project.stages[0].status=="complete","page changes and hiding never stop background acquisition");
    page.show();QApplication::processEvents();control<QTabWidget>(page,"calibrationWorkspaces")->setCurrentIndex(1);QApplication::processEvents();
    auto *samples=control<QTableWidget>(page,"calibrationSamples");check(samples->rowCount()==4,"raw independent samples visible");samples->selectRow(0);
    control<QLineEdit>(page,"calibrationExclusionReason")->setText(QStringLiteral("软件测试审核"));control<QPushButton>(page,"calibrationExclude")->click();r.service->synchronize();QApplication::processEvents();
    check(r.service->view().project.stages[0].statistics(816).used==3,"UI exclusion reaches independent model");
    control<QComboBox>(page,"calibrationReviewWave")->setCurrentIndex(1);QApplication::processEvents();check(samples->rowCount()==4,"other wavelength unaffected");
    check(!page.findChild<QWidget*>("calibrationSession")&&!page.findChild<QWidget*>("calibrationImport"),"obsolete Session-import page removed");
    for(int scale:{100,130}){
        page.setTheme(scale==130,scale);page.resize(1280,1000);QApplication::processEvents();check(page.grab().width()>0,"actual Qt render available");
        auto *edit=control<QPlainTextEdit>(page,"calibrationDetails");const auto image=edit->viewport()->grab().toImage();
        const auto background=image.pixelColor(image.width()-4,image.height()-4);
        check(std::abs(background.lightness()-edit->palette().color(QPalette::Text).lightness())>80,"rendered details text remains readable after light/dark round trip");
    }
    page.setTheme(false,100);page.close();r.service->synchronize();
    largeReview();
    std::cout<<"Independent calibration UI software tests passed.\n";
}
