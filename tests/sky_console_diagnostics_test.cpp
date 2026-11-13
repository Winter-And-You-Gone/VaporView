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
    std::cout << "Sky console diagnostics passed\n";
}
