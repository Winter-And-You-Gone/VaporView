#include "test_settings_sandbox.h"
#include "WaterCalibrationFixture.h"
#include "ground/calibration/WaterCalibrationPage.h"
#include "shared/theme/AppTheme.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QTemporaryDir>

using namespace WaterCalibrationTest;
using namespace VaporView::Ground::Calibration;
namespace
{
template<typename T> T *find(QWidget& page,const char *name)
{
    auto *w=page.findChild<T*>(QString::fromLatin1(name));check(w,"named UI control exists");return w;
}
void complete(QWidget& page,const std::function<bool()>& ready)
{
    QElapsedTimer elapsed;elapsed.start();
    while(!ready()&&elapsed.elapsed()<15000){QApplication::processEvents();QThread::msleep(10);}
    check(ready(),qPrintable(find<QLabel>(page,"calibrationStatus")->text()));
    check(!find<QLabel>(page,"calibrationStatus")->property("calibrationError").toBool(),"UI operation succeeded");
}
void click(QWidget& page,const char *name)
{
    if(QByteArray(name)=="calibrationImport")find<QCheckBox>(page,"calibrationSingleConcentration")->setChecked(true);
    auto *b=find<QPushButton>(page,name);check(b->isEnabled(),"button available");b->click();
}
}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);QTemporaryDir temp;check(temp.isValid(),"isolated UI scope");
    const auto low=recordSession(temp.filePath("low"),2),high=recordSession(temp.filePath("high"),4);
    const auto libraryFilename=temp.filePath("library.json");
    {
        WaterCalibrationPage page(nullptr,libraryFilename);page.resize(1150,900);page.show();
        complete(page,[&]{return find<QLabel>(page,"calibrationStatus")->text().contains(QStringLiteral("定标库已加载"));});
        check(!page.findChild<QComboBox*>("calibrationWavelength")&&!page.findChild<QComboBox*>("calibrationChannel"),
            "joint import has no wavelength or channel dropdown");
        auto *views=find<QTabBar>(page,"calibrationWavelengthViews");
        check(views->count()==2&&views->tabText(0).contains("816")&&views->tabText(1).contains("1368"),
            "two separate wavelength views are available");
        find<QLineEdit>(page,"calibrationSession")->setText(low);find<QDoubleSpinBox>(page,"calibrationKnownConcentration")->setValue(5);click(page,"calibrationImport");
        complete(page,[&]{return find<QTableWidget>(page,"calibrationSamples")->rowCount()==1;});
        find<QLineEdit>(page,"calibrationSession")->setText(high);find<QDoubleSpinBox>(page,"calibrationKnownConcentration")->setValue(9);click(page,"calibrationImport");
        complete(page,[&]{return find<QTableWidget>(page,"calibrationSamples")->rowCount()==2;});
        auto *tabs=page.findChild<QTabWidget*>();check(tabs&&tabs->count()==3,"three focused pages");tabs->setCurrentIndex(1);
        click(page,"calibrationFit");complete(page,[&]{return find<QComboBox>(page,"calibrationVersions")->count()==1;});
        click(page,"calibrationImport");complete(page,[&]{return find<QPushButton>(page,"calibrationImport")->isEnabled();});
        check(find<QTableWidget>(page,"calibrationSamples")->rowCount()==2&&
            find<QTableWidget>(page,"calibrationSamples")->item(0,7)->text()!=QStringLiteral("—"),"duplicate-only import preserves sample count and existing fit state");
        click(page,"calibrationEnable");complete(page,[&]{return find<QLabel>(page,"calibrationStatus")->text().contains(QStringLiteral("已启用"));});
        tabs->setCurrentIndex(2);click(page,"calibrationCalculate");complete(page,[&]{return find<QTableWidget>(page,"calibrationResults")->rowCount()==2;});
        auto *results=find<QTableWidget>(page,"calibrationResults");check(results->item(1,3)->text()=="9"&&results->item(1,5)->text().contains(QStringLiteral("未进行温压补偿")),"visible known initial concentration and compensation status");
        check(results->item(0,3)->text()==QStringLiteral("—")&&results->item(0,5)->text().contains(QStringLiteral("未启用")),"other wavelength cannot silently use current fit");
        click(page,"calibrationDisable");complete(page,[&]{return find<QLabel>(page,"calibrationStatus")->text().contains(QStringLiteral("已停用"));});
        check(results->rowCount()==0,"disabling calibration clears obsolete current concentration display");
        click(page,"calibrationEnable");complete(page,[&]{return find<QLabel>(page,"calibrationStatus")->text().contains(QStringLiteral("已启用"));});
        click(page,"calibrationCalculate");complete(page,[&]{return results->rowCount()==2;});
        find<QLineEdit>(page,"calibrationSession")->setText(low);
        check(results->rowCount()==0,"switching source clears results from the previous Session");
        click(page,"calibrationCalculate");
        // Change while the completion callback is still queued, as can happen
        // when a source-selection dialog returns during a follow calculation.
        find<QLineEdit>(page,"calibrationSession")->setText(high);
        complete(page,[&]{return find<QPushButton>(page,"calibrationCalculate")->isEnabled();});
        check(results->rowCount()==0,"asynchronous old-Session result cannot repopulate the current source");
        click(page,"calibrationCalculate");complete(page,[&]{return results->rowCount()==2;});
        auto *samples=find<QTableWidget>(page,"calibrationSamples");samples->setCurrentCell(0,0);
        click(page,"calibrationExclude");
        check(samples->item(0,3)->text()==QStringLiteral("已排除")&&samples->item(0,7)->text()==QStringLiteral("—"),"exclusion clears residuals and updates validity");
        check(samples->item(1,7)->text()==QStringLiteral("—"),"old fit residuals are not presented for edited data");
        QTimer::singleShot(0,[]{auto *dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());check(dialog,"discard confirmation shown");dialog->button(QMessageBox::Discard)->click();});
        auto *versions=find<QComboBox>(page,"calibrationVersions");
        QMetaObject::invokeMethod(versions,"activated",Qt::DirectConnection,Q_ARG(int,0));
        check(samples->item(0,3)->text().startsWith(QStringLiteral("有效")),"historical fit restores immutable original input after edits");
        VaporView::Ground::Session::GroundRecordingService recorder;
        VaporView::Ground::Session::GroundRecordingOptions options;options.baseDirectory=temp.filePath("following");options.fpgaConfiguration={{"backend","simulator"}};
        check(recorder.start(options),"start followed software-test Session");
        find<QLineEdit>(page,"calibrationSession")->setText(recorder.status().sessionDirectory);
        auto *follow=find<QCheckBox>(page,"calibrationFollow");auto *timer=page.findChild<QTimer*>();check(timer,"single follow timer exists");timer->setInterval(20);follow->setChecked(true);
        VaporView::FpgaWave::CompletedStream wave;wave.source=0x31;wave.totalPoints=1;wave.complete=true;
        VaporView::FpgaWave::CompletedStream::DliaPoint point;point.hasHarmonics=true;point.h1=1;point.h2=3;wave.dliaPoints.append(point);
        const auto timestamp=VaporView::Ground::Session::GroundRecordingService::currentTimestampUs();
        check(recorder.recordFpgaHarmonics(timestamp,wave),"admit followed cycle");
        complete(page,[&]{return results->rowCount()==1&&!find<QLabel>(page,"calibrationStatus")->text().contains(QStringLiteral("正在处理"));});
        check(results->item(0,3)->text()=="7","follow uses saved active coefficients on existing recorded feature");
        check(recorder.pause()&&!recorder.recordFpgaHarmonics(timestamp,wave),"pause prevents new concentration input");
        check(recorder.start(options)&&recorder.recordFpgaHarmonics(timestamp,wave),"resume admits a separately identified cycle");
        complete(page,[&]{return results->rowCount()==2;});
        check(results->item(0,6)->text()==results->item(1,6)->text(),"resumed records retain exact active version identity");
        page.hide();page.show();check(page.findChildren<QTimer*>().size()==1,"page visibility changes do not duplicate follow subscriptions");
        check(!recorder.stop().writeFailed&&!recorder.recordFpgaHarmonics(timestamp,wave),"stop seals followed Session");
        follow->setChecked(false);timer->setInterval(2000);
        find<QLineEdit>(page,"calibrationSession")->clear();click(page,"calibrationCalculate");
        QElapsedTimer invalidWait;invalidWait.start();
        while(!find<QPushButton>(page,"calibrationCalculate")->isEnabled()&&invalidWait.elapsed()<15000){QApplication::processEvents();QThread::msleep(10);}
        check(results->rowCount()==0&&find<QLabel>(page,"calibrationStatus")->property("calibrationError").toBool(),"no source Session cannot leave valid or untraceable concentrations");
        page.setTheme(true,130);QApplication::processEvents();
        check(page.property(VaporView::kAppDarkThemeProperty).toBool()&&page.styleSheet().contains("QTabBar::tab:selected"),"scoped dark-theme tabs and font scaling");
        page.setTheme(false,100);
        page.close();
    }
    {
        auto *closing=new WaterCalibrationPage(nullptr,libraryFilename);
        complete(*closing,[&]{return find<QLabel>(*closing,"calibrationStatus")->text().contains(QStringLiteral("定标库已加载"));});
        find<QLineEdit>(*closing,"calibrationSession")->setText(low);click(*closing,"calibrationCalculate");
        delete closing;
        check(QThreadPool::globalInstance()->waitForDone(15000),"page destruction leaves only bounded independent worker data");
        QApplication::processEvents();
        Library library;QString error;check(Store(libraryFilename).load(library,&error)&&library.fits.size()==1,"destroyed calculation cannot alter the saved library");
    }
    {
        WaterCalibrationPage reopened(nullptr,libraryFilename);reopened.resize(1150,900);reopened.show();
        complete(reopened,[&]{return find<QComboBox>(reopened,"calibrationVersions")->count()==1;});
        auto *versions=find<QComboBox>(reopened,"calibrationVersions");
        // Selecting a saved version restores the exact input dataset in the UI.
        QMetaObject::invokeMethod(versions,"activated",Qt::DirectConnection,Q_ARG(int,0));
        check(find<QTableWidget>(reopened,"calibrationSamples")->rowCount()==2,"saved input records reload after page recreation");
        click(reopened,"calibrationDisable");complete(reopened,[&]{return find<QLabel>(reopened,"calibrationStatus")->text().contains(QStringLiteral("已停用"));});
        Library library;QString error;check(Store(libraryFilename).load(library,&error)&&!library.activeFit(816)&&library.fits.size()==1,"UI activation lifecycle persists without removing history");
    }
    {
        // Construct a schema-v1 library from the previously selectable mapping.
        // It must stay inspectable without moving samples to another channel.
        auto oldData=Dataset::create("LEGACY MAPPING SOFTWARE TEST",1368,0x30,"ppmv");
        oldData.samples=importSession(low,oldData,5,true).samples;
        oldData.samples+=importSession(high,oldData,9,true).samples;
        oldData.wavelengthNm=816;oldData.revision=1;
        const auto oldFit=FitModel::create("linear",1)->fit(oldData);
        check(oldFit.success,"historical linear model remains reproducible");
        Library library;library.datasets={oldData};library.fits={oldFit.fit};library.active[816]=oldFit.fit.id;
        QString error;const auto oldFilename=temp.filePath("old-mapping-library.json");
        check(Store(oldFilename).save(library,&error),qPrintable(error));
        const auto originalLibrary=read(oldFilename);
        const auto oldResult=calculate(oldData.samples.first(),816,&oldFit.fit);
        const auto historyFilename=temp.filePath("old-mapping-results.json");
        check(exportResults(historyFilename,{oldResult},&error,{oldFit.fit}),qPrintable(error));
        WaterCalibrationPage page(nullptr,oldFilename);page.resize(1200,900);page.show();
        complete(page,[&]{return find<QLabel>(page,"calibrationStatus")->text().contains(QStringLiteral("定标库已加载"));});
        find<QCheckBox>(page,"calibrationFollow")->setChecked(false);
        check(find<QLabel>(page,"calibrationCalculationSummary")->text().contains(QStringLiteral("旧映射不符")),
            "legacy active mapping is explicitly identified");
        auto *versions=find<QComboBox>(page,"calibrationVersions");
        QMetaObject::invokeMethod(versions,"activated",Qt::DirectConnection,Q_ARG(int,0));
        check(find<QLabel>(page,"calibrationChannel")->text().contains(QStringLiteral("历史映射不符")),
            "old dataset retains its actual recorded channel");
        click(page,"calibrationEnable");
        check(find<QLabel>(page,"calibrationStatus")->property("calibrationError").toBool(),"legacy wrong-channel version cannot be enabled");
        click(page,"calibrationFit");
        check(find<QLabel>(page,"calibrationStatus")->property("calibrationError").toBool(),"legacy wrong-channel data cannot produce a new calibration version");
        find<QLineEdit>(page,"calibrationSession")->setText(high);click(page,"calibrationCalculate");
        complete(page,[&]{return find<QTableWidget>(page,"calibrationResults")->rowCount()==2;});
        auto *results=find<QTableWidget>(page,"calibrationResults");
        for(int row=0;row<results->rowCount();++row){
            check(results->item(row,3)->text()==QStringLiteral("—"),"old enabled mapping cannot produce a valid current concentration");
            if(results->item(row,0)->text()=="816")
                check(results->item(row,7)->text().startsWith("DLIA1")&&results->item(row,5)->text().contains(QStringLiteral("不匹配")),
                    "current 816 input always comes from DLIA1 even with old active parameters");
        }
        check(read(oldFilename)==originalLibrary,"loading and calculating never rewrite legacy library or relabel samples");
        page.loadConcentrationResults(historyFilename);
        complete(page,[&]{return results->rowCount()==1;});
        check(results->item(0,3)->text()=="5"&&results->item(0,7)->text().startsWith("DLIA0"),
            "historical concentration retains its original parameters and channel");
        check(find<QLabel>(page,"calibrationStatus")->text().contains(QStringLiteral("旧硬件映射")),
            "historical values cannot be mistaken for a calculation with the current wiring");
        click(page,"calibrationNew");
        check(find<QLabel>(page,"calibrationChannel")->text().contains("DLIA1")&&
            !find<QLabel>(page,"calibrationChannel")->text().contains(QStringLiteral("历史映射不符")),
            "new task after viewing legacy data returns to fixed wiring");
    }
    std::cout<<"water_calibration_ui_test passed (SOFTWARE TEST DATA ONLY)\n";return 0;
}
