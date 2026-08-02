#include "SkyTuiApp.h"

#include <QCoreApplication>

#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

VaporView::LogRecord makeRecord(int index,
                                VaporView::LogLevel level = VaporView::LogLevel::Warning,
                                const QString& message = QStringLiteral("SkyCore IPC 结构化日志测试。"))
{
    VaporView::LogRecord record;
    record.level = level;
    record.source = QStringLiteral("SkyCore");
    record.category = QStringLiteral("integration");
    record.message = message;
    record.fields = {{QStringLiteral("event"), QStringLiteral("sky_ipc_structured_log_test")}};
    record.sequence = static_cast<quint64>(index + 1);
    return record;
}
}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    VaporView::SkyTuiOptions options;
    VaporView::SkyTuiApp tui(nullptr, options);

    tui.appendLogRecordForTest(makeRecord(0));
    const VaporView::SkyTuiModel& model = tui.modelForTest();
    require(model.logs.size() == 1, "TUI stores rendered log line");
    require(model.log_records.size() == 1, "TUI stores structured log record metadata");
    require(model.log_records.first().level == VaporView::LogLevel::Warning, "original level is preserved");
    require(model.log_records.first().source == QStringLiteral("SkyCore"), "source is preserved");
    require(model.log_records.first().category == QStringLiteral("integration"), "category is preserved");
    require(model.log_records.first().event == QStringLiteral("sky_ipc_structured_log_test"), "event is preserved");
    require(model.log_records.first().message == QStringLiteral("SkyCore IPC 结构化日志测试。"), "Chinese message is preserved");
    require(model.logs.first().contains(QStringLiteral("Warning/SkyCore/integration")), "rendered line shows level/source/category");

    for (int i = 1; i < VaporView::SkyTuiModel::MaxLogLines + 8; ++i)
    {
        tui.appendLogRecordForTest(makeRecord(i, VaporView::LogLevel::Error, QStringLiteral("错误 %1").arg(i)));
    }
    require(model.logs.size() == VaporView::SkyTuiModel::MaxLogLines, "rendered TUI log cap remains enforced");
    require(model.log_records.size() == VaporView::SkyTuiModel::MaxLogLines, "structured TUI log cap remains enforced");
    require(model.log_records.last().level == VaporView::LogLevel::Error, "latest structured level survives pruning");
    require(model.log_records.last().message == QStringLiteral("错误 %1").arg(VaporView::SkyTuiModel::MaxLogLines + 7),
            "latest Chinese message survives pruning");
    return 0;
}
