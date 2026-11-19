#include "test_settings_sandbox.h"
#include "IndependentCalibrationFixture.h"
#include "ground/calibration/WaterCalibrationPage.h"
#include "ground/calibration/CalibrationReviewPlot.h"
#include "ground/widgets/VisualTextLabel.h"
#include "shared/theme/AppTheme.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QMouseEvent>
#include <array>
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
    auto *title=control<QLabel>(page,"sectionTitleLabel");
    check(dynamic_cast<VaporView::VisualTextLabel*>(title)&&
          !title->textInteractionFlags().testFlag(Qt::TextSelectableByMouse)&&
          title->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard)&&title->focusPolicy()==Qt::ClickFocus,
          "card title uses shared selection behavior without a text caret");
    auto *header = control<QWidget>(page, "calibrationCardHeader");
    auto *stepPath = control<QWidget>(page, "calibrationStepPath");
    const std::array<QToolButton*, 6> steps{
        control<QToolButton>(page, "calibrationStep1"), control<QToolButton>(page, "calibrationStep2"),
        control<QToolButton>(page, "calibrationStep3"), control<QToolButton>(page, "calibrationStep4"),
        control<QToolButton>(page, "calibrationStep5"), control<QToolButton>(page, "calibrationStep6")};
    const auto stepImage = [](QToolButton *step) {
        return step->icon().pixmap(step->iconSize(), step->devicePixelRatioF()).toImage();
    };
    const auto checkSteps = [&](const std::array<bool, 6>& expected) {
        QApplication::processEvents();
        for (size_t i = 0; i < steps.size(); ++i) {
            auto *step = steps[i];
            check(step->property("completed").isValid() && step->property("completed").toBool() == expected[i],
                  "header step follows actual calibration state");
            check(!step->icon().isNull() && step->toolTip().contains(expected[i] ? QStringLiteral("已完成") : QStringLiteral("未完成")),
                  "step shows an icon and the correct tooltip state");
        }
    };
    checkSteps({false, false, false, false, false, false});
    const auto incompleteIcon = stepImage(steps[0]);
    const std::array<QString, 6> expectedInstructions{QStringLiteral("创建新任务"), QStringLiteral("已知浓度"),
        QStringLiteral("气路和信号稳定"), QStringLiteral("确认审核完成"),
        QStringLiteral("阶段均值等权"), QStringLiteral("生成 .vcal")};
    for (size_t i = 0; i < steps.size(); ++i)
        check(steps[i]->toolTip().contains(expectedInstructions[i]), "each node carries its own detailed instructions");
    auto *tabs=control<QTabWidget>(page,"calibrationWorkspaces");auto *navigation=control<QFrame>(page,"calibrationNavigationBar");
    auto *workspaceCard=control<QFrame>(page,"calibrationWorkspaceCard");
    for(int i=0;i<tabs->count();++i) {
        auto *selector=control<QPushButton>(page,qPrintable(QStringLiteral("calibrationPageSelectorButton%1").arg(i+1)));
        selector->click();QApplication::processEvents();
        check(tabs->currentIndex()==i&&selector->isChecked()&&tabs->currentWidget()->isVisible(),"navigation selects its corresponding workspace");
    }
    tabs->setCurrentIndex(0);QApplication::processEvents();
    check(control<QPushButton>(page,"calibrationPageSelectorButton1")->isChecked(),"programmatic tab changes update navigation selection");
    check(navigation->geometry().bottom()<workspaceCard->geometry().top(),"navigation sits above the workspace card");
    auto *name=control<QLineEdit>(page,"calibrationTaskName");auto *unit=control<QComboBox>(page,"calibrationUnit");
    auto *create=control<QPushButton>(page,"calibrationCreateTask");auto *viewport=name->parentWidget()->parentWidget();
    for(int scale:{100,130})for(int width:{900,1280}) {
        page.setTheme(false,scale);page.resize(width,1000);QApplication::processEvents();
        const QRect nameRect(name->mapTo(&page,QPoint()),name->size());const QRect unitRect(unit->mapTo(&page,QPoint()),unit->size());
        const QRect createRect(create->mapTo(&page,QPoint()),create->size());const QRect viewportRect(viewport->mapTo(&page,QPoint()),viewport->size());
        check(nameRect.center().y()==unitRect.center().y()&&unitRect.center().y()==createRect.center().y(),"task controls share one row");
        check(nameRect.right()<unitRect.left()&&unitRect.right()<createRect.left(),"task controls are ordered without overlap");
        check(viewportRect.contains(nameRect)&&viewportRect.contains(unitRect)&&viewportRect.contains(createRect),"task controls fit the visible workspace");
        const QRect pathRect(stepPath->mapTo(header, QPoint()), stepPath->size());
        const QRect titleRect(title->mapTo(header, QPoint()), title->size());
        check(header->rect().contains(pathRect) && pathRect.left() - titleRect.right() == 11 &&
                  qAbs(pathRect.center().y() - titleRect.center().y()) <= 1,
              "step path sits directly after the title at both sizes and scales");
        const auto firstCenter = steps.front()->geometry().center();
        const auto lastCenter = steps.back()->geometry().center();
        check(lastCenter.x() - firstCenter.x() >= 350 * scale / 100 && lastCenter.y() == firstCenter.y(),
              "path has six evenly aligned, widely spaced nodes");
        auto *node = steps.front();
        node->clearFocus();
        const auto bg = [&](bool hovered) {
            node->setAttribute(Qt::WA_UnderMouse, hovered);
            const auto image = node->grab().toImage();
            return image.pixelColor(image.width() / 2, image.height() / 5);
        };
        check(node->width() == qRound(32 * scale / 100.0) &&
                  node->iconSize().width() == qRound(18 * scale / 100.0),
              "node uses the home connection button proportions");
        check(bg(false) == VaporView::appThemeColor(VaporView::AppThemeColor::HomeDeviceDangerBackground, false) &&
                  bg(true) == VaporView::appThemeColor(VaporView::AppThemeColor::PrimarySubtle, false),
              "pending button renders a red background and the home action hover color");
        node->setAttribute(Qt::WA_UnderMouse, false);
    }
    page.setTheme(false,100);page.resize(1280,1000);QApplication::processEvents();
    control<QLineEdit>(page,"calibrationTaskName")->setText(QStringLiteral("软件测试独立任务"));
    control<QPushButton>(page,"calibrationCreateTask")->click();r.service->synchronize();QApplication::processEvents();
    checkSteps({true, false, false, false, false, false});
    check(stepImage(steps[0]) != incompleteIcon, "creating a task completes only the first indicator");
    steps.front()->setAttribute(Qt::WA_UnderMouse, false);
    const auto completeButton = steps.front()->grab().toImage();
    check(completeButton.pixelColor(completeButton.width() / 2, completeButton.height() / 5) ==
              VaporView::appThemeColor(VaporView::AppThemeColor::HomeDeviceSuccessBackground, false),
          "completed button repaints its green background");
    check(control<QSpinBox>(page,"calibrationStageSeconds")->value()==600,"UI ten-minute default");
    auto *concentration = control<QDoubleSpinBox>(page, "calibrationStageConcentration");
    auto *addStage = control<QPushButton>(page, "calibrationAddStage");
    for (int i = 0; i < 4; ++i) addStage->click();
    r.service->synchronize();
    checkSteps({true, true, false, false, false, false});
    auto *stages = control<QTableWidget>(page, "calibrationStages");
    check(stages->rowCount() == 4, "four identical concentrations satisfy the add-stage indicator");
    for (int i = 3; i >= 0; --i) r.service->removeStage(i);
    r.service->synchronize();
    checkSteps({true, false, false, false, false, false});
    concentration->setValue(100);
    control<QSpinBox>(page,"calibrationStageSeconds")->setValue(2);
    addStage->click();
    r.service->synchronize();
    checkSteps({true, true, false, false, false, false});
    check(stages->rowCount() == 1, "stage-centered UI");
    stages->selectRow(0);
    auto *stable = control<QCheckBox>(page, "calibrationStableConfirmed");
    auto *startStage = control<QPushButton>(page, "calibrationStartStage");
    check(!startStage->isEnabled(), "starting acquisition requires checking stability");
    stable->setChecked(true);
    check(startStage->isEnabled(), "checking stability enables start");
    stable->setChecked(false);
    check(!startStage->isEnabled(), "unchecking stability disables start again");
    r.pair(2,4);
    stable->setChecked(true);
    startStage->click();
    r.service->synchronize();
    check(!startStage->isEnabled() && !stable->isChecked(), "starting consumes the stability confirmation");
    control<QTabWidget>(page,"calibrationWorkspaces")->setCurrentIndex(2);page.hide();
    for(int i=0;i<4;++i){r.now+=500;r.pair(2,4);}check(r.service->view().project.stages[0].status=="complete","page changes and hiding never stop background acquisition");
    page.show();QApplication::processEvents();control<QTabWidget>(page,"calibrationWorkspaces")->setCurrentIndex(1);QApplication::processEvents();
    auto *samples=control<QTableWidget>(page,"calibrationSamples");check(samples->rowCount()==4,"raw independent samples visible");samples->selectRow(0);
    control<QLineEdit>(page,"calibrationExclusionReason")->setText(QStringLiteral("软件测试审核"));control<QPushButton>(page,"calibrationExclude")->click();r.service->synchronize();QApplication::processEvents();
    check(r.service->view().project.stages[0].statistics(816).used==3,"UI exclusion reaches independent model");
    control<QComboBox>(page,"calibrationReviewWave")->setCurrentIndex(1);QApplication::processEvents();check(samples->rowCount()==4,"other wavelength unaffected");
    check(!page.findChild<QWidget*>("calibrationSession")&&!page.findChild<QWidget*>("calibrationImport"),"obsolete Session-import page removed");
    checkSteps({true, true, true, false, false, false});
    r.service->addStage(200, 2000);
    r.service->synchronize();
    checkSteps({true, true, false, false, false, false});
    auto *confirmReview = control<QPushButton>(page, "calibrationConfirmReview");
    check(!confirmReview->isEnabled(), "review confirmation is available after collection");
    r.pair(4, 8);
    r.service->startStage(1);
    r.service->synchronize();
    for (int i = 0; i < 4; ++i) { r.now += 500; r.pair(4, 8); }
    checkSteps({true, true, true, false, false, false});
    confirmReview->click();
    r.service->synchronize();
    checkSteps({true, true, true, true, false, false});
    control<QPushButton>(page, "calibrationFit")->click();
    r.service->synchronize();
    checkSteps({true, true, true, true, true, false});
    r.service->generateFile(QStringLiteral("步骤指示测试"));
    r.service->synchronize();
    checkSteps({true, true, true, true, true, true});
    auto view = r.service->view();
    check(view.files.size() == 1 && !view.selected.valid, "generating alone completes the last step");
    check(view.drafts.size() == 1, "task saved one draft");
    r.service->openProject(view.drafts.first(), true);
    r.service->synchronize();
    checkSteps({true, true, true, true, true, true});
    r.service->fitProject();
    r.service->synchronize();
    checkSteps({true, true, true, true, true, false});
    r.service->selectFile(view.files.first());
    r.service->synchronize();
    checkSteps({true, true, true, true, true, false});
    r.service->addStage(300, 2000);
    r.service->synchronize();
    checkSteps({true, true, false, false, false, false});
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
