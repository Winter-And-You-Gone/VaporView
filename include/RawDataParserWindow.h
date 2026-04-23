#ifndef VaporView_RAW_DATA_PARSER_WINDOW_H_
#define VaporView_RAW_DATA_PARSER_WINDOW_H_

#include <QMainWindow>

#include <memory>

class QString;

class RawDataParserWindow : public QMainWindow
{
public:
    explicit RawDataParserWindow(QWidget *parent = nullptr);
    ~RawDataParserWindow() override;

    void setEnglish(bool english);
    bool openSessionPath(const QString& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif
