#pragma once
#include <QWidget>
#include <memory>

namespace VaporView::Ground::Calibration
{
// The page owns view state only; import, fit, persistence and calculation live in the core.
class WaterCalibrationPage final : public QWidget
{
public:
    explicit WaterCalibrationPage(QWidget *parent = nullptr, const QString& libraryFilename = {});
    ~WaterCalibrationPage() override;
    void setTheme(bool dark, int fontScalePercent = 100);
private:
    struct State;
    std::unique_ptr<State> state_;
    void refresh();
    void refreshVersions();
    void setStatus(const QString&, bool error = false, bool publishLog = true);
    bool discardChanges();
    void newDataset(bool announce = true);
    void loadLibrary(bool announce = true);
    void importData();
    void saveDataset(bool fit);
    void setActive(bool enabled);
    void calculateSession();
};
}
