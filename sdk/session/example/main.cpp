#include <vaporview/SessionSdk.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

using namespace VaporView::SessionSdk;

namespace
{
int report(const Result& result)
{
    if (!result.message.isEmpty()) QTextStream(stderr) << result.message << '\n';
    return result ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    QTextStream out(stdout);
    const auto usage = [] {
        QTextStream(stderr)
            << "Usage: vaporview_session_example SESSION [list]\n"
               "       vaporview_session_example SESSION query CATEGORY [FIRST_US LAST_US]\n"
               "       vaporview_session_example SESSION export CATEGORY DIRECTORY [FIRST_US LAST_US]\n"
               "       vaporview_session_example SESSION frame ORDINAL\n";
        return 2;
    };
    if (args.size() < 2) return usage();
    Reader session;
    if (const auto result = session.open(args[1]); report(result)) return 1;
    const auto command = args.value(2, "list");
    if (command == "list" && args.size() <= 3) {
        QJsonArray categories;
        for (const auto c : session.categories()) categories.append(categoryName(c));
        auto metadata = session.metadata();
        metadata["categories"] = categories;
        out << QJsonDocument(metadata).toJson();
        return 0;
    }
    if (command == "frame" && args.size() == 4) {
        bool ok = false;
        const auto ordinal = args[3].toULongLong(&ok);
        if (!ok) return usage();
        Waveform frame;
        if (const auto result = session.readWaveform(ordinal, frame); report(result)) return 1;
        auto details = frame.record.fields;
        details["timestamp_us"] = QString::number(frame.record.timestampUs);
        const auto preview = [](const auto& values) {
            QJsonArray first;
            for (qsizetype i = 0; i < values.size() && i < 5; ++i) first.append(double(values[i]));
            return QJsonObject{{"count", double(values.size())}, {"first_samples", first}};
        };
        details["h1"] = preview(frame.h1);
        details["h2"] = preview(frame.h2);
        details["raw"] = preview(frame.raw);
        details["harmonic"] = preview(frame.harmonic);
        out << QJsonDocument(details).toJson();
        return 0;
    }
    const qsizetype rangeIndex = command == "export" ? 5 : 4;
    if ((command != "query" && command != "export") ||
        (args.size() != rangeIndex && args.size() != rangeIndex + 2)) return usage();
    std::optional<Category> category;
    for (const auto c : session.categories()) if (categoryName(c) == args[3]) category = c;
    if (!category) {
        QTextStream(stderr) << "Unknown or absent category: " << args[3] << '\n';
        return 1;
    }
    Range range;
    if (args.size() > rangeIndex) {
        bool firstOk = false, lastOk = false;
        range.firstUs = args[rangeIndex].toULongLong(&firstOk);
        range.lastUs = args[rangeIndex + 1].toULongLong(&lastOk);
        if (!firstOk || !lastOk) return usage();
    }
    if (command == "export") {
        const auto result = session.exportTo(*category, range, args[4]);
        if (report(result)) return 1;
        out << result.records << " records exported\n";
        return 0;
    }
    return report(session.visit(*category, range, [&](const Record& record) {
        QJsonObject json{{"ordinal", QString::number(record.ordinal)},
            {"timestamp_us", QString::number(record.timestampUs)}, {"fields", record.fields}};
        if (record.sequence) json["sequence"] = QString::number(*record.sequence);
        out << QJsonDocument(json).toJson(QJsonDocument::Compact) << '\n';
        return true;
    }));
}
