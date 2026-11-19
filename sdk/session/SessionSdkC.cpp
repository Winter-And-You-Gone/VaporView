#include <vaporview/SessionSdkC.h>
#include <vaporview/SessionSdk.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringDecoder>

#include <cstdlib>
#include <cstring>
#include <memory>

namespace Sdk = VaporView::SessionSdk;

struct vv_session {
    Sdk::Reader reader;
};

struct vv_session_waveform {
    Sdk::Waveform frame;
    QByteArray fieldsJson;
};

namespace
{
char *copyString(const char *text, size_t size) noexcept
{
    auto *copy = static_cast<char *>(std::malloc(size + 1));
    if (copy) {
        std::memcpy(copy, text, size);
        copy[size] = '\0';
    }
    return copy;
}

vv_session_status failure(vv_session_status status, const char *text, char **message) noexcept
{
    if (message) *message = copyString(text, std::strlen(text));
    return status;
}

vv_session_status translateStatus(Sdk::Status status) noexcept
{
    switch (status) {
    case Sdk::Status::Ok: return VV_SESSION_OK;
    case Sdk::Status::Missing: return VV_SESSION_MISSING;
    case Sdk::Status::InvalidArgument: return VV_SESSION_INVALID_ARGUMENT;
    case Sdk::Status::Corrupt: return VV_SESSION_CORRUPT;
    case Sdk::Status::IoError: return VV_SESSION_IO_ERROR;
    case Sdk::Status::Cancelled: return VV_SESSION_CANCELLED;
    }
    return VV_SESSION_INTERNAL_ERROR;
}

vv_session_status finish(const Sdk::Result& result, char **message)
{
    if (message && !result.message.isEmpty()) {
        const auto utf8 = result.message.toUtf8();
        *message = copyString(utf8.constData(), size_t(utf8.size()));
        if (!*message) return VV_SESSION_INTERNAL_ERROR;
    }
    return translateStatus(result.status);
}

template<typename Operation>
vv_session_status run(char **message, Operation operation) noexcept
{
    if (message) *message = nullptr;
    try {
        return operation();
    } catch (...) {
        return failure(VV_SESSION_INTERNAL_ERROR, "Internal Session SDK failure", message);
    }
}

vv_session_status invalid(char **message) noexcept
{
    return failure(VV_SESSION_INVALID_ARGUMENT, "Invalid Session SDK argument", message);
}

QString decodePath(const char *text)
{
    if (!text || !*text) return {};
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless | QStringDecoder::Flag::ConvertInitialBom);
    const QString result = decoder(QByteArrayView(text));
    return decoder.hasError() ? QString() : result;
}

std::optional<Sdk::Category> findCategory(const char *name)
{
    if (!name) return std::nullopt;
    constexpr Sdk::Category categories[] = {Sdk::Category::Sensors, Sdk::Category::NavigationRaw,
        Sdk::Category::PressureRaw, Sdk::Category::TemperatureHumidityRaw, Sdk::Category::DistanceRaw,
        Sdk::Category::Waveforms, Sdk::Category::CycleFeatures, Sdk::Category::TcpPeaks,
        Sdk::Category::Concentrations};
    for (const auto category : categories) {
        if (Sdk::categoryName(category) == QLatin1StringView(name)) return category;
    }
    return std::nullopt;
}

vv_session_status writeJson(const QJsonDocument& document, char **out, char **message)
{
    const auto text = document.toJson(QJsonDocument::Compact);
    *out = copyString(text.constData(), size_t(text.size()));
    return *out ? VV_SESSION_OK : failure(VV_SESSION_INTERNAL_ERROR, "Cannot allocate Session SDK output", message);
}

vv_session_record recordView(const Sdk::Record& record, const QByteArray& json) noexcept
{
    return {record.ordinal, record.timestampUs, record.sequence.value_or(0),
            record.sequence ? 1u : 0u, json.constData()};
}
}

uint32_t VV_SESSION_CALL vv_session_abi_version(void)
{
    return VV_SESSION_ABI_VERSION;
}

const char *VV_SESSION_CALL vv_session_version(void)
{
    return VAPORVIEW_SESSION_SDK_VERSION;
}

void VV_SESSION_CALL vv_session_free_string(char *text)
{
    std::free(text);
}

vv_session_status VV_SESSION_CALL vv_session_open(const char *directory, vv_session **out, char **message)
{
    if (out) *out = nullptr;
    return run(message, [&] {
        const QString path = decodePath(directory);
        if (!out || path.isEmpty()) return invalid(message);
        auto session = std::make_unique<vv_session>();
        const auto status = finish(session->reader.open(path), message);
        if (status == VV_SESSION_OK) *out = session.release();
        return status;
    });
}

void VV_SESSION_CALL vv_session_close(vv_session *session)
{
    delete session;
}

vv_session_status VV_SESSION_CALL vv_session_metadata(const vv_session *session, char **json, char **message)
{
    if (json) *json = nullptr;
    if (json && json == message) return VV_SESSION_INVALID_ARGUMENT;
    return run(message, [&] {
        if (!session || !json) return invalid(message);
        return writeJson(QJsonDocument(session->reader.metadata()), json, message);
    });
}

vv_session_status VV_SESSION_CALL vv_session_categories(const vv_session *session, char **json, char **message)
{
    if (json) *json = nullptr;
    if (json && json == message) return VV_SESSION_INVALID_ARGUMENT;
    return run(message, [&] {
        if (!session || !json) return invalid(message);
        QJsonArray categories;
        for (const auto category : session->reader.categories()) categories.append(Sdk::categoryName(category));
        return writeJson(QJsonDocument(categories), json, message);
    });
}

vv_session_status VV_SESSION_CALL vv_session_visit(const vv_session *session, const char *category,
    uint64_t first_us, uint64_t last_us, vv_session_visitor visitor, void *user, uint64_t *records, char **message)
{
    if (records) *records = 0;
    return run(message, [&] {
        const auto selected = findCategory(category);
        if (!session || !selected || !visitor || first_us > last_us) return invalid(message);
        const auto result = session->reader.visit(*selected, {first_us, last_us}, [&](const Sdk::Record& record) {
            const auto json = QJsonDocument(record.fields).toJson(QJsonDocument::Compact);
            const auto view = recordView(record, json);
            if (!visitor(&view, user)) return false;
            if (records) ++*records;
            return true;
        });
        return finish(result, message);
    });
}

vv_session_status VV_SESSION_CALL vv_session_read_waveform(const vv_session *session, uint64_t ordinal,
    vv_session_waveform **out, char **message)
{
    if (out) *out = nullptr;
    return run(message, [&] {
        if (!session || !out) return invalid(message);
        auto waveform = std::make_unique<vv_session_waveform>();
        const auto result = session->reader.readWaveform(ordinal, waveform->frame);
        if (result) waveform->fieldsJson = QJsonDocument(waveform->frame.record.fields).toJson(QJsonDocument::Compact);
        const auto status = finish(result, message);
        if (status == VV_SESSION_OK) *out = waveform.release();
        return status;
    });
}

vv_session_status VV_SESSION_CALL vv_session_waveform_data(const vv_session_waveform *waveform,
    vv_session_waveform_view *out)
{
    if (out) *out = {};
    if (!waveform || !out) return VV_SESSION_INVALID_ARGUMENT;
    const auto& frame = waveform->frame;
    *out = {recordView(frame.record, waveform->fieldsJson),
            frame.h1.isEmpty() ? nullptr : frame.h1.constData(), uint64_t(frame.h1.size()),
            frame.h2.isEmpty() ? nullptr : frame.h2.constData(), uint64_t(frame.h2.size()),
            frame.raw.isEmpty() ? nullptr : frame.raw.constData(), uint64_t(frame.raw.size()),
            frame.harmonic.isEmpty() ? nullptr : frame.harmonic.constData(), uint64_t(frame.harmonic.size())};
    return VV_SESSION_OK;
}

void VV_SESSION_CALL vv_session_free_waveform(vv_session_waveform *waveform)
{
    delete waveform;
}

vv_session_status VV_SESSION_CALL vv_session_export(const vv_session *session, const char *category,
    uint64_t first_us, uint64_t last_us, const char *directory, uint64_t *records, char **message)
{
    if (records) *records = 0;
    return run(message, [&] {
        const auto selected = findCategory(category);
        const QString path = decodePath(directory);
        if (!session || !selected || path.isEmpty() || first_us > last_us) return invalid(message);
        const auto result = session->reader.exportTo(*selected, {first_us, last_us}, path);
        if (records) *records = result.records;
        return finish(result, message);
    });
}
