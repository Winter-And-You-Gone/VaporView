#pragma once

#include <QPlainTextEdit>

class QEvent;
class QResizeEvent;
class QSyntaxHighlighter;
class QLabel;

namespace VaporView::Ground::Widgets
{

class JsonConfigEditor final : public QPlainTextEdit
{
public:
    explicit JsonConfigEditor(QWidget *parent = nullptr);

    void setValidationStatus(bool valid,
                             const QString& statusText,
                             const QString& detail = QString());

    bool hasValidationStatus() const { return validation_known_; }
    bool isValidationSuccessful() const { return validation_valid_; }

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateValidationBadgeGeometry();
    void updateValidationBadgeStyle();

    QSyntaxHighlighter *syntax_highlighter_ = nullptr;
    QLabel *validation_badge_ = nullptr;
    bool validation_known_ = false;
    bool validation_valid_ = false;
};

} // namespace VaporView::Ground::Widgets
