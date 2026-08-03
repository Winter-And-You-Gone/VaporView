#pragma once

#include "Ai8TemperatureControllerProtocol.h"

#include <QString>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include <array>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QLabel;
class QStackedWidget;
class TemperatureTrendPlotWidget;

namespace VaporView::Ground::Widgets
{

class Ai8TemperatureControllerPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit Ai8TemperatureControllerPanel(QWidget *parent = nullptr);

    void setEnglish(bool english);
    void setBackendConnected(bool connected, const QString& detail = QString());
    void setOperationStatus(const QString& text, bool success);
    QString currentOutputStatusText() const;
    Ai8TemperatureControllerProtocol::PageData currentPageData() const;
    void applyPageData(const Ai8TemperatureControllerProtocol::PageData& pageData);
    void applyLiveData(const Ai8TemperatureControllerProtocol::LiveData& liveData);

signals:
    void readPageRequested();
    void writePageRequested();
    void outputStatusChanged();

private:
    struct LabelBinding
    {
        QLabel *label = nullptr;
        QString chinese;
        QString english;
    };

    struct ButtonBinding
    {
        QAbstractButton *button = nullptr;
        QString chinese;
        QString english;
    };

    struct ComboItemBinding
    {
        QComboBox *combo = nullptr;
        int index = -1;
        QString chinese;
        QString english;
    };

    void setupUi();
    QWidget *createChannelPage();
    QWidget *createInputPage();
    QWidget *createOutputPage();
    QWidget *createGlobalPage();
    QWidget *createParameterField(const QString& chinese,
                                  const QString& english,
                                  QWidget *editor,
                                  QWidget *parent);
    QComboBox *createFixedChoiceCombo(QWidget *parent);
    void addComboItem(QComboBox *combo,
                      const QString& chinese,
                      const QString& english,
                      const QVariant& userData = QVariant());
    void selectPage(int index);
    void updateStatusText();
    void updateMeasuredValue();
    void updateTemperaturePlot();

    QButtonGroup *page_button_group_ = nullptr;
    QStackedWidget *page_stack_ = nullptr;
    TemperatureTrendPlotWidget *temperature_plot_ = nullptr;
    QLabel *protocol_status_label_ = nullptr;
    QAbstractButton *read_button_ = nullptr;
    QAbstractButton *write_button_ = nullptr;
    QVector<LabelBinding> label_bindings_;
    QVector<ButtonBinding> button_bindings_;
    QVector<ComboItemBinding> combo_item_bindings_;
    std::array<QVector<double>, Ai8TemperatureControllerProtocol::kChannelCount>
        measured_temperature_history_{};
    Ai8TemperatureControllerProtocol::LiveData latest_live_data_;
    QString backend_detail_;
    QString operation_status_;
    bool english_ = false;
    bool backend_connected_ = false;
    bool operation_succeeded_ = true;
};

} // namespace VaporView::Ground::Widgets
