#pragma once

#include "Ai8TemperatureControllerProtocol.h"

#include <QList>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include <array>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QLabel;
class QResizeEvent;
class QStackedWidget;
class TemperatureTrendPlotWidget;
class QToolButton;

namespace VaporView::Ground::Widgets
{

class Ai8TemperatureControllerPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit Ai8TemperatureControllerPanel(QWidget *parent = nullptr);

    void setEnglish(bool english);
    void setBackendConnected(bool connected, const QString& detail = QString());
    void setPageCommandsEnabled(bool enabled, const QString& disabledToolTip = QString());
    void setProtocolStatusLabel(QLabel *label);
    void setOperationStatus(const QString& text, bool success);
    QString currentOutputStatusText() const;
    Ai8TemperatureControllerProtocol::PageData currentPageData() const;
    void applyPageData(const Ai8TemperatureControllerProtocol::PageData& pageData);
    void applyLiveData(const Ai8TemperatureControllerProtocol::LiveData& liveData);

signals:
    void readPageRequested();
    void writePageRequested();
    void outputStatusChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;

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

    struct DetailSectionBinding
    {
        QToolButton *toggle = nullptr;
        QWidget *content = nullptr;
    };

    void setupUi();
    QWidget *createChannelPage();
    QWidget *createInputPage();
    QWidget *createOutputPage();
    QWidget *createGlobalPage();
    QWidget *createDetailSection(const QString& objectName,
                                 const QString& chinese,
                                 const QString& english,
                                 const QList<QWidget *>& fields,
                                 QWidget *parent);
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
    void updateRunStateCombo(quint16 rawValue);
    void updateRunStateAccessibility();
    void updateStatusText();
    void updateMeasuredValue();
    void updateAlarmStatusDisplay();
    void updateTemperaturePlot();
    void refreshPageCommandControls();
    void adjustTemperaturePlotHeight();
    void setDetailSectionsExpanded(bool expanded);
    void syncDetailStackHeight();

    QButtonGroup *page_button_group_ = nullptr;
    QStackedWidget *page_stack_ = nullptr;
    QStackedWidget *detail_stack_ = nullptr;
    TemperatureTrendPlotWidget *temperature_plot_ = nullptr;
    QLabel *protocol_status_label_ = nullptr;
    QAbstractButton *read_button_ = nullptr;
    QAbstractButton *write_button_ = nullptr;
    QComboBox *run_state_combo_ = nullptr;
    QVector<LabelBinding> label_bindings_;
    QVector<ButtonBinding> button_bindings_;
    QVector<ComboItemBinding> combo_item_bindings_;
    QVector<DetailSectionBinding> detail_section_bindings_;
    std::array<QVector<double>, Ai8TemperatureControllerProtocol::kChannelCount>
        measured_temperature_history_{};
    std::array<QVector<double>, Ai8TemperatureControllerProtocol::kChannelCount>
        measured_temperature_time_history_{};
    Ai8TemperatureControllerProtocol::LiveData latest_live_data_;
    QString backend_detail_;
    QString operation_status_;
    int run_state_unknown_item_index_ = -1;
    quint16 run_state_raw_ = 0;
    quint16 run_state_write_value_ = 0;
    bool run_state_write_requested_ = false;
    bool english_ = false;
    bool backend_connected_ = false;
    bool page_commands_enabled_ = true;
    QString page_commands_disabled_tooltip_;
    bool operation_succeeded_ = true;
    bool detail_sections_expanded_ = false;
};

} // namespace VaporView::Ground::Widgets
