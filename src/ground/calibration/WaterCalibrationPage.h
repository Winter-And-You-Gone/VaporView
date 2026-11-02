#pragma once
#include <QWidget>
#include <memory>

namespace VaporView::Ground::Calibration
{
class CalibrationService;
class WaterCalibrationPage final : public QWidget
{
public:
    explicit WaterCalibrationPage(QWidget *parent = nullptr, const QString& installationDirectory = {},
                                  std::shared_ptr<CalibrationService> service = {});
    ~WaterCalibrationPage() override;
    void setTheme(bool dark, int fontScalePercent = 100);
    std::shared_ptr<CalibrationService> service() const;
    void loadConcentrationResults(const QString& filename);
    void exportConcentrationResults(const QString& filename);
protected:
    void showEvent(QShowEvent *) override;
private:
    struct State;
    std::unique_ptr<State> state_;
    void refresh();
    void refreshReview();
};
}
