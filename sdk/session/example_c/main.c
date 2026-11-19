#include <vaporview/SessionSdkC.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

static int report(vv_session_status status, char *message)
{
    if (status == VV_SESSION_OK) {
        if (message && *message) fprintf(stderr, "Warning: %s\n", message);
    } else if (status == VV_SESSION_CANCELLED) {
        fprintf(stderr, "%s\n", message && *message ? message : "Visit cancelled by callback");
    } else {
        fprintf(stderr, "Session SDK status %" PRId32 ": %s\n", status, message ? message : "");
    }
    vv_session_free_string(message);
    return status == VV_SESSION_OK || status == VV_SESSION_CANCELLED ? 0 : 1;
}

static int parse_uint64(const char *text, uint64_t *result)
{
    char *end = NULL;
    if (*text < '0' || *text > '9') return 0;
    errno = 0;
    uintmax_t value = strtoumax(text, &end, 10);
    if (errno || *end || value > UINT64_MAX) return 0;
    *result = (uint64_t)value;
    return 1;
}

static int32_t VV_SESSION_CALL print_record(const vv_session_record *record, void *user)
{
    uint64_t *remaining = (uint64_t *)user;
    if (*remaining == 0) return 0;
    printf("{\"ordinal\":\"%" PRIu64 "\",\"timestamp_us\":\"%" PRIu64 "\",", record->ordinal, record->timestamp_us);
    if (record->has_sequence) printf("\"sequence\":\"%" PRIu64 "\",", record->sequence);
    printf("\"fields\":%s}\n", record->fields_json);
    --*remaining;
    return 1;
}

static int list_session(vv_session *session)
{
    char *json = NULL, *message = NULL;
    vv_session_status status = vv_session_metadata(session, &json, &message);
    int exit_code = report(status, message);
    if (!exit_code) puts(json);
    vv_session_free_string(json);
    if (exit_code) return exit_code;
    status = vv_session_categories(session, &json, &message);
    exit_code = report(status, message);
    if (!exit_code) puts(json);
    vv_session_free_string(json);
    return exit_code;
}

static void preview(const char *component, uint64_t count, double first)
{
    printf("%s: %" PRIu64 " samples", component, count);
    if (count) printf(", first=%g", first);
    putchar('\n');
}

static int waveform(vv_session *session, uint64_t ordinal)
{
    vv_session_waveform *frame = NULL;
    char *message = NULL;
    vv_session_status status = vv_session_read_waveform(session, ordinal, &frame, &message);
    int exit_code = report(status, message);
    if (!exit_code) {
        vv_session_waveform_view view;
        exit_code = report(vv_session_waveform_data(frame, &view), NULL);
        if (!exit_code) {
            puts(view.record.fields_json);
            preview("h1", view.h1_count, view.h1_count ? view.h1[0] : 0);
            preview("h2", view.h2_count, view.h2_count ? view.h2[0] : 0);
            preview("raw", view.raw_count, view.raw_count ? view.raw[0] : 0);
            preview("harmonic", view.harmonic_count, view.harmonic_count ? view.harmonic[0] : 0);
        }
    }
    vv_session_free_waveform(frame);
    return exit_code;
}

static int usage(void)
{
    fputs("Usage: vaporview_session_c_example SESSION list\n"
          "       vaporview_session_c_example SESSION query CATEGORY FIRST_US LAST_US [LIMIT]\n"
          "       vaporview_session_c_example SESSION frame ORDINAL\n"
          "       vaporview_session_c_example SESSION export CATEGORY DIRECTORY FIRST_US LAST_US\n", stderr);
    return 2;
}

static int run(int argc, char **argv)
{
    if (argc < 3) return usage();
    const char *command = argv[2];
    uint64_t first = 0, last = UINT64_MAX, limit = UINT64_MAX, ordinal = 0;
    if (strcmp(command, "list") == 0) {
        if (argc != 3) return usage();
    } else if (strcmp(command, "frame") == 0) {
        if (argc != 4 || !parse_uint64(argv[3], &ordinal)) return usage();
    } else if (strcmp(command, "query") == 0) {
        if ((argc != 6 && argc != 7) || !parse_uint64(argv[4], &first) || !parse_uint64(argv[5], &last) ||
            (argc == 7 && !parse_uint64(argv[6], &limit))) return usage();
    } else if (strcmp(command, "export") == 0) {
        if (argc != 7 || !parse_uint64(argv[5], &first) || !parse_uint64(argv[6], &last)) return usage();
    } else {
        return usage();
    }

    if (vv_session_abi_version() != VV_SESSION_ABI_VERSION) {
        fprintf(stderr, "Incompatible VaporView Session SDK ABI\n");
        return 1;
    }
    vv_session *session = NULL;
    char *message = NULL;
    vv_session_status status = vv_session_open(argv[1], &session, &message);
    int exit_code = report(status, message);
    if (exit_code) return exit_code;

    if (strcmp(command, "list") == 0) {
        exit_code = list_session(session);
    } else if (strcmp(command, "frame") == 0) {
        exit_code = waveform(session, ordinal);
    } else {
        uint64_t records = 0;
        if (strcmp(command, "query") == 0) {
            status = vv_session_visit(session, argv[3], first, last, print_record, &limit, &records, &message);
        } else {
            status = vv_session_export(session, argv[3], first, last, argv[4], &records, &message);
            if (status == VV_SESSION_OK) printf("%" PRIu64 " records exported\n", records);
        }
        exit_code = report(status, message);
    }
    vv_session_close(session);
    return exit_code;
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
{
    char **utf8 = (char **)calloc((size_t)argc, sizeof(*utf8));
    if (!utf8) return 1;
    int exit_code = 1;
    int index = 0;
    for (; index < argc; ++index) {
        int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1, NULL, 0, NULL, NULL);
        if (!length) break;
        utf8[index] = (char *)malloc((size_t)length);
        if (!utf8[index] || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[index], -1,
                                               utf8[index], length, NULL, NULL)) break;
    }
    if (index == argc) exit_code = run(argc, utf8);
    else fputs("Unable to convert command-line arguments to UTF-8\n", stderr);
    for (int i = 0; i < argc; ++i) free(utf8[i]);
    free(utf8);
    return exit_code;
}
#else
int main(int argc, char **argv)
{
    return run(argc, argv);
}
#endif
