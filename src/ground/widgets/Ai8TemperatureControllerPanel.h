#pragma once

#include <QString>
#include <QVariant>
#include <QVector>
#include <QWidget>

class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QLabel;
class QStackedWidget;

namespace VaporView::Ground::Widgets
{

class Ai8TemperatureControllerPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit Ai8TemperatureControllerPanel(QWidget *parent = nullptr);

    void setEnglish(bool english);

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

    QButtonGroup *page_button_group_ = nullptr;
    QStackedWidget *page_stack_ = nullptr;
    QLabel *protocol_status_label_ = nullptr;
    QAbstractButton *read_button_ = nullptr;
    QAbstractButton *write_button_ = nullptr;
    QVector<LabelBinding> label_bindings_;
    QVector<ButtonBinding> button_bindings_;
    QVector<ComboItemBinding> combo_item_bindings_;
};

} // namespace VaporView::Ground::Widgets
