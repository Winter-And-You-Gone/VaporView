#ifndef VAPORVIEW_SESSION_SDK_C_H
#define VAPORVIEW_SESSION_SDK_C_H

#include <stdint.h>

#if defined(VAPORVIEW_SESSION_SDK_STATIC)
#  define VV_SESSION_API
#elif defined(_WIN32)
#  if defined(VAPORVIEW_SESSION_SDK_BUILD)
#    define VV_SESSION_API __declspec(dllexport)
#  else
#    define VV_SESSION_API __declspec(dllimport)
#  endif
#else
#  define VV_SESSION_API __attribute__((visibility("default")))
#endif
#if defined(_WIN32)
#  define VV_SESSION_CALL __cdecl
#else
#  define VV_SESSION_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define VV_SESSION_ABI_VERSION 1u

typedef struct vv_session vv_session;
typedef struct vv_session_waveform vv_session_waveform;
typedef int32_t vv_session_status;

enum {
    VV_SESSION_OK = 0,
    VV_SESSION_MISSING = 1,
    VV_SESSION_INVALID_ARGUMENT = 2,
    VV_SESSION_CORRUPT = 3,
    VV_SESSION_IO_ERROR = 4,
    VV_SESSION_CANCELLED = 5,
    VV_SESSION_INTERNAL_ERROR = 6
};

typedef struct vv_session_record {
    uint64_t ordinal;
    uint64_t timestamp_us;
    uint64_t sequence;
    uint32_t has_sequence;
    const char *fields_json;
} vv_session_record;

typedef struct vv_session_waveform_view {
    vv_session_record record;
    const double *h1;
    uint64_t h1_count;
    const double *h2;
    uint64_t h2_count;
    const float *raw;
    uint64_t raw_count;
    const float *harmonic;
    uint64_t harmonic_count;
} vv_session_waveform_view;

/* All strings are NUL-terminated UTF-8. Non-NULL output slots are reset on entry.
 * Outputs from metadata/categories and optional message outputs belong to the caller
 * and must be released with vv_session_free_string. OK can carry a warning message.
 * Provide separate slots for JSON and message. A message is optional even on failure.
 * For a live handle, serialize operations, including close, on that handle. */
VV_SESSION_API uint32_t VV_SESSION_CALL vv_session_abi_version(void);
/* Static string valid until unloading the library. */
VV_SESSION_API const char *VV_SESSION_CALL vv_session_version(void);
VV_SESSION_API void VV_SESSION_CALL vv_session_free_string(char *text);

VV_SESSION_API vv_session_status VV_SESSION_CALL vv_session_open(
    const char *directory, vv_session **out, char **message);
VV_SESSION_API void VV_SESSION_CALL vv_session_close(vv_session *session);
VV_SESSION_API vv_session_status VV_SESSION_CALL vv_session_metadata(
    const vv_session *session, char **json, char **message);
/* JSON array of category names in Reader order. Use these names in visit/export. */
VV_SESSION_API vv_session_status VV_SESSION_CALL vv_session_categories(
    const vv_session *session, char **json, char **message);

/* Synchronous callback on the calling thread. Its record and strings are borrowed
 * until callback return. Return nonzero to continue, zero to cancel. Do not throw,
 * enter the API on the same session, or release it from a callback. */
typedef int32_t (VV_SESSION_CALL *vv_session_visitor)(const vv_session_record *record, void *user);
/* Inclusive Unix microsecond bounds. Use 0 and UINT64_MAX for the entire range.
 * Optional records counts callbacks that returned nonzero, including on failure. */
VV_SESSION_API vv_session_status VV_SESSION_CALL vv_session_visit(
    const vv_session *session, const char *category, uint64_t first_us, uint64_t last_us,
    vv_session_visitor visitor, void *user, uint64_t *records, char **message);

/* Release the returned waveform with vv_session_free_waveform. */
VV_SESSION_API vv_session_status VV_SESSION_CALL vv_session_read_waveform(
    const vv_session *session, uint64_t ordinal, vv_session_waveform **out, char **message);
/* View strings and samples are borrowed until free_waveform. A missing component
 * has a NULL pointer and zero count. The waveform may outlive the session. */
VV_SESSION_API vv_session_status VV_SESSION_CALL vv_session_waveform_data(
    const vv_session_waveform *waveform, vv_session_waveform_view *out);
VV_SESSION_API void VV_SESSION_CALL vv_session_free_waveform(vv_session_waveform *waveform);

VV_SESSION_API vv_session_status VV_SESSION_CALL vv_session_export(
    const vv_session *session, const char *category, uint64_t first_us, uint64_t last_us,
    const char *directory, uint64_t *records, char **message);

#ifdef __cplusplus
}
#endif

#endif
