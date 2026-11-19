#include <vaporview/SessionSdkC.h>

#include <iostream>
#include <memory>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
using Session = std::unique_ptr<vv_session, decltype(&vv_session_close)>;
using SdkString = std::unique_ptr<char, decltype(&vv_session_free_string)>;

bool report(vv_session_status status, SdkString message)
{
    if (status != VV_SESSION_OK) {
        std::cerr << "Session SDK status " << status << '\n';
    }
    if (message && *message.get()) {
        if (status == VV_SESSION_OK) std::cerr << "Warning: ";
        std::cerr << message.get() << '\n';
    }
    return status == VV_SESSION_OK;
}

bool printJson(const char *name, const Session& session,
               vv_session_status (VV_SESSION_CALL *read)(const vv_session *, char **, char **))
{
    char *json = nullptr, *message = nullptr;
    const auto status = read(session.get(), &json, &message);
    const SdkString value(json, vv_session_free_string);
    if (!report(status, SdkString(message, vv_session_free_string))) return false;
    std::cout << name << '\n' << value.get() << '\n';
    return true;
}

int list(const std::string& path)
{
    if (vv_session_abi_version() != VV_SESSION_ABI_VERSION) {
        std::cerr << "Incompatible VaporView Session SDK ABI\n";
        return 1;
    }
    vv_session *handle = nullptr;
    char *message = nullptr;
    const auto status = vv_session_open(path.c_str(), &handle, &message);
    const Session session(handle, vv_session_close);
    if (!report(status, SdkString(message, vv_session_free_string))) return 1;
    std::cout << "SDK " << vv_session_version() << '\n';
    return printJson("Metadata", session, vv_session_metadata) &&
           printJson("Categories", session, vv_session_categories) ? 0 : 1;
}

int usage()
{
    std::cerr << "Usage: vaporview_session_cpp_example SESSION [list]\n";
    return 2;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
{
    if (argc != 2 && !(argc == 3 && std::wstring(argv[2]) == L"list")) return usage();
    const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[1], -1, nullptr, 0, nullptr, nullptr);
    if (bytes > 0) {
        std::string path(static_cast<std::size_t>(bytes), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[1], -1, &path[0], bytes, nullptr, nullptr)) {
            path.pop_back();
            return list(path);
        }
    }
    std::cerr << "Unable to convert the Session path to UTF-8\n";
    return 2;
}
#else
int main(int argc, char **argv)
{
    if (argc != 2 && !(argc == 3 && std::string(argv[2]) == "list")) return usage();
    return list(argv[1]);
}
#endif
