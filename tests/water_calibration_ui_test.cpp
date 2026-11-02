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
using namespace IndependentCalibrationTest;
template<class T>T *control(QWidget& page,const char *id){auto *w=page.findChild<T*>(id);check(w,"named independent UI control exists");return w;}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);Rig r;WaterCalibrationPage page(nullptr,r.root.path(),r.service);page.resize(1280,1000);page.show();
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
    std::cout<<"Independent calibration UI software tests passed.\n";
}
