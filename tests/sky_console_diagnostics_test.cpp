#include "SkyConsoleDiagnostics.h"
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char *message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main()
{
    VaporView::SkyConsoleDiagnostics console;
    VaporView::LogRecord record;
    record.message = QStringLiteral("PTB: serial read timeout");
    record.fields = {{"event", "device_collector_output"}, {"device_id", "PTB"},
                     {"process_output", "full serial read timeout"}};
    require(console.accept(record, 0) == QStringList{record.message}, "first diagnosis is visible");
    for (int i = 1; i <= 100; ++i)
        require(console.accept(record, i).isEmpty(), "same diagnosis is suppressed on console only");
    const auto repeated = console.flush(5000);
    require(repeated.size() == 1 && repeated.first().contains("100"), "periodic repeat count");
    record.message = QStringLiteral("PTB: port closed");
    require(console.accept(record, 5001) == QStringList{record.message}, "new cause appears immediately");
    record.fields["device_id"] = "HC2A";
    require(console.accept(record, 5002).size() == 1, "devices are independent");
    record.fields["process_output"] = "changed beyond the displayed summary";
    require(console.accept(record, 5002).size() == 1, "different raw causes are never merged after summary truncation");
    record.fields["event"] = "device_connection_failed";
    require(console.accept(record, 5003).size() == 1, "structured errors bypass raw aggregation");
    require(record.fields.value("process_output") == "changed beyond the displayed summary", "full diagnostic remains intact");
    VaporView::SkyConsoleDiagnostics fpgaConsole;
    record.fields = {{"event", "fpga_worker_started"}, {"fpga_op", "uart_exchange"}, {"channel", 1}};
    require(fpgaConsole.accept(record, 0).isEmpty(), "dispatch trace is file-only");
    record.fields["event"] = "fpga_operation_finished";
    record.level = VaporView::LogLevel::Info;
    record.message = QStringLiteral("FPGA 请求已完成：TEC103 / LC0");
    require(fpgaConsole.accept(record, 1) == QStringList{record.message}, "first operation result is shown");
    require(fpgaConsole.accept(record, 2).isEmpty(), "same success is coalesced");
    auto otherChannel = record;
    otherChannel.fields["channel"] = 2;
    otherChannel.message = QStringLiteral("FPGA 请求已完成：TEC103 / LC1");
    require(fpgaConsole.accept(otherChannel, 3) == QStringList{otherChannel.message}, "other channel is visible independently");
    const auto summary = fpgaConsole.flush(5001);
    require(summary.size() == 1 && summary.first().contains(QStringLiteral("LC0")) &&
            summary.first().contains(QStringLiteral("重复 1 次")), "repeat summary reports channel and count");
    auto failed = record;
    failed.level = VaporView::LogLevel::Warning;
    failed.message = QStringLiteral("FPGA 请求失败：TEC103 / LC0。回读超时。");
    require(fpgaConsole.accept(failed, 5002) == QStringList{failed.message} &&
            fpgaConsole.accept(failed, 5003) == QStringList{failed.message}, "repeated failures are never coalesced");
    require(fpgaConsole.accept(record, 5004) == QStringList{record.message}, "recovery is visible immediately");
    std::cout << "Sky console diagnostics passed\n";
}
