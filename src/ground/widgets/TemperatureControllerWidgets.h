#pragma once

#include "TelemetryTypes.h"
#include "data_types.h"
#include "ground/widgets/SegmentedSwitchButton.h"

#include <QComboBox>
#include <QWidget>

#include <functional>

namespace VaporView::Ground::Widgets
{

class SourceModeOverviewSwitchButton : public SegmentedSwitchButton
{
public:
    explicit SourceModeOverviewSwitchButton(QWidget *parent = nullptr)
        : SegmentedSwitchButton(parent)
    {
    }

    virtual void setEnglish(bool english) = 0;
};

class TemperatureControllerOverviewPanel : public QWidget
{
public:
    explicit TemperatureControllerOverviewPanel(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }

    virtual void setEnglish(bool english) = 0;
    virtual void updateData(const VaporView::TemperatureControllerData& sample) = 0;
    virtual void setOutputEnabledCallback(std::function<void(quint8, bool)> callback) = 0;
    virtual void updateThemedIcons() = 0;
};

QComboBox *createSingleLevelPopupComboBox(QWidget *parent = nullptr,
                                          bool showSelectionCheck = true,
                                          bool popupFitContents = false);
SourceModeOverviewSwitchButton *createSourceModeOverviewSwitchButton(QWidget *parent = nullptr);
TemperatureControllerOverviewPanel *createTemperatureControllerOverviewPanel(QWidget *parent = nullptr);

} // namespace VaporView::Ground::Widgets
