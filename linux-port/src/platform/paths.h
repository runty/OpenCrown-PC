#pragma once
#include <cstdlib>
#include <pwd.h>
#include <string>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace dcp::platform {
inline std::string home_directory() {
    if (const char* h = std::getenv("HOME"); h && *h) return h;
    const passwd* p = getpwuid(getuid());
    return p && p->pw_dir ? p->pw_dir : ".";
}
inline std::string default_user_directory() {
#ifdef __APPLE__
    return home_directory() + "/Library/Application Support/OpenCrown";
#else
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x == '/') return std::string(x) + "/OpenCrown";
    return home_directory() + "/.local/share/OpenCrown";
#endif
}
inline std::string log_directory(const std::string& user) {
#ifdef __APPLE__
    if (const char* u = std::getenv("DCP_USER_DIR"); !u || !*u)
        return home_directory() + "/Library/Logs/OpenCrown";
#endif
    return user + "/logs";
}
inline std::string executable_directory() {
    std::string path;
#ifdef __APPLE__
    uint32_t size = 4096;
    std::vector<char> buf(size);
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        buf.resize(size);
        if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    }
    if (char* resolved = realpath(buf.data(), nullptr)) {
        path = resolved;
        std::free(resolved);
    } else path = buf.data();
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf);
    if (n <= 0 || size_t(n) == sizeof buf) return {};
    path.assign(buf, size_t(n));
#endif
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string{} : slash == 0 ? "/" : path.substr(0, slash);
}
}
