//- common.cpp — 格式化与文件小工具

#include "common.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mihomo {

std::string expand_home(std::string_view path) {
    if (path.empty()) return {};
    if (path[0] == '~') {
        const char* home = std::getenv("HOME");
        if (!home) home = "/";
        std::string out = home;
        std::string_view rest = path.substr(1);
        if (!rest.empty() && rest[0] == '/') out += rest;
        else out += rest;
        return out;
    }
    return std::string(path);
}

std::string default_config_path() {
    if (const char* env = std::getenv("MIHOMO_CONFIG"); env && *env)
        return expand_home(env);
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + "/.config/mihomo/config.yaml";
}

std::string dir_name(std::string_view path) {
    auto pos = path.rfind('/');
    if (pos == std::string_view::npos) return ".";
    if (pos == 0) return "/";
    return std::string(path.substr(0, pos));
}

std::string base_name(std::string_view path) {
    auto pos = path.rfind('/');
    if (pos == std::string_view::npos) return std::string(path);
    return std::string(path.substr(pos + 1));
}

bool file_exists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

std::optional<std::string> read_file(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::nullopt;
    std::string out;
    std::array<char, 65536> buf{};
    std::size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), f)) > 0) out.append(buf.data(), n);
    bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad) return std::nullopt;
    return out;
}

bool write_file_atomic(const std::string& path, std::string_view content,
                       std::string& error) {
    const std::string dir = dir_name(path);
    std::string tmpl = dir + "/." + base_name(path) + ".tmp-XXXXXX";
    std::vector<char> name(tmpl.begin(), tmpl.end());
    name.push_back('\0');

    int fd = ::mkstemp(name.data());
    if (fd < 0) {
        error = "mkstemp 失败: " + std::string(std::strerror(errno));
        return false;
    }
    const std::string tmp(name.data());

    auto fail = [&](const std::string& why) {
        error = why;
        ::close(fd);
        ::unlink(tmp.c_str());
        return false;
    };

    ::fchmod(fd, 0600);

    std::size_t off = 0;
    while (off < content.size()) {
        ssize_t n = ::write(fd, content.data() + off, content.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return fail("写入临时文件失败: " + std::string(std::strerror(errno)));
        }
        off += static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0) return fail("fsync 失败: " + std::string(std::strerror(errno)));

    // 关闭失败也要回滚，避免留下半写文件。
    if (::close(fd) != 0) {
        error = "关闭临时文件失败: " + std::string(std::strerror(errno));
        ::unlink(tmp.c_str());
        return false;
    }
    fd = -1;

    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        error = "rename 失败: " + std::string(std::strerror(errno));
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

namespace {

constexpr std::string_view kUnits[] = {"B", "KB", "MB", "GB", "TB", "PB"};

} // namespace

std::string format_bytes(double bytes) {
    if (!(bytes > 0)) return "0 B";
    int    idx = 0;
    double v   = bytes;
    while (v >= 1024.0 && idx < 5) {
        v /= 1024.0;
        ++idx;
    }
    char buf[64];
    if (idx == 0) std::snprintf(buf, sizeof buf, "%.0f %s", v, kUnits[idx].data());
    else if (v < 10.0) std::snprintf(buf, sizeof buf, "%.1f %s", v, kUnits[idx].data());
    else std::snprintf(buf, sizeof buf, "%.0f %s", v, kUnits[idx].data());
    return buf;
}

std::string format_bytes_short(double bytes) {
    if (!(bytes > 0)) return "0";
    int    idx = 0;
    double v   = bytes;
    while (v >= 1024.0 && idx < 5) {
        v /= 1024.0;
        ++idx;
    }
    char buf[32];
    if (idx == 0) std::snprintf(buf, sizeof buf, "%.0f", v);
    else if (v < 10.0) std::snprintf(buf, sizeof buf, "%.1f%c", v, kUnits[idx][0]);
    else std::snprintf(buf, sizeof buf, "%.0f%c", v, kUnits[idx][0]);
    return buf;
}

std::string format_rate(double bytes_per_sec) {
    if (!(bytes_per_sec > 0)) return "0 B/s";
    return format_bytes(bytes_per_sec) + "/s";
}

std::string format_delay(int ms) {
    if (ms == -3) return "…";               // 正在测速
    if (ms == 0 || ms == -2) return "超时"; // mihomo 的失败记录是 0，本程序用 -2
    if (ms < 0) return "—";                // 还没测过 / 未知
    return std::to_string(ms) + "ms";
}

std::string now_hms() {
    std::time_t t = std::time(nullptr);
    std::tm     tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M:%S", &tm);
    return buf;
}

std::string timestamp_compact() {
    std::time_t t = std::time(nullptr);
    std::tm     tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%d-%H%M%S", &tm);
    return buf;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'
                          || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'
                          || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string to_lower_ascii(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string format_duration(long long seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds < 60) return std::to_string(seconds) + "s";
    if (seconds < 3600) return std::to_string(seconds / 60) + "m";
    long long h = seconds / 3600;
    long long m = (seconds % 3600) / 60;
    if (m == 0) return std::to_string(h) + "h";
    return std::to_string(h) + "h" + std::to_string(m) + "m";
}

} // namespace mihomo
