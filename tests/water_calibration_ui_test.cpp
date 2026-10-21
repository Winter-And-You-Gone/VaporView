#include "test_settings_sandbox.h"
#include "WaterCalibrationFixture.h"
#include "ground/calibration/WaterCalibrationPage.h"
#include "shared/theme/AppTheme.h"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
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
        find<QLineEdit>(page,"calibrationSession")->setText(low);find<QDoubleSpinBox>(page,"calibrationKnownConcentration")->setValue(5);click(page,"calibrationImport");
        complete(page,[&]{return find<QTableWidget>(page,"calibrationSamples")->rowCount()==1;});
        find<QLineEdit>(page,"calibrationSession")->setText(high);find<QDoubleSpinBox>(page,"calibrationKnownConcentration")->setValue(9);click(page,"calibrationImport");
        complete(page,[&]{return find<QTableWidget>(page,"calibrationSamples")->rowCount()==2;});
        auto *tabs=page.findChild<QTabWidget*>();check(tabs&&tabs->count()==3,"three focused pages");tabs->setCurrentIndex(1);
        click(page,"calibrationFit");complete(page,[&]{return find<QComboBox>(page,"calibrationVersions")->count()==1;});
        click(page,"calibrationEnable");complete(page,[&]{return find<QLabel>(page,"calibrationStatus")->text().contains(QStringLiteral("已启用"));});
        tabs->setCurrentIndex(2);click(page,"calibrationCalculate");complete(page,[&]{return find<QTableWidget>(page,"calibrationResults")->rowCount()==2;});
        auto *results=find<QTableWidget>(page,"calibrationResults");check(results->item(0,3)->text()=="9"&&results->item(0,5)->text().contains(QStringLiteral("未进行温压补偿")),"visible known initial concentration and compensation status");
        check(results->item(1,3)->text()==QStringLiteral("—")&&results->item(1,5)->text().contains(QStringLiteral("未启用")),"other wavelength cannot silently use current fit");
        page.setTheme(true,130);QApplication::processEvents();
        check(page.property(VaporView::kAppDarkThemeProperty).toBool()&&page.styleSheet().contains("QTabBar::tab:selected"),"scoped dark-theme tabs and font scaling");
        page.setTheme(false,100);
        page.close();
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
    std::cout<<"water_calibration_ui_test passed (SOFTWARE TEST DATA ONLY)\n";return 0;
}
