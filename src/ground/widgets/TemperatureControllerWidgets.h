#pragma once

#include "TelemetryTypes.h"
#include "data_types.h"

#include <QComboBox>
#include <QPushButton>
#include <QWidget>

#include <functional>

namespace VaporView::Ground::Widgets
{

class SourceModeOverviewSwitchButton : public QPushButton
{
public:
    explicit SourceModeOverviewSwitchButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
    }

    virtual void setEnglish(bool english) = 0;
    virtual bool switchChecked() const = 0;
    virtual void setSwitchChecked(bool checked, bool animated) = 0;
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
