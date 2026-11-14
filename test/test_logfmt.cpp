//- test_logfmt.cpp — 日志精简/分色单测

#include <cstdio>
#include <string>

#include "logfmt.hpp"

using namespace mihomo;
using vawk::widget::LogLevel;
using vawk::widget::LogTone;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                         #cond);                                               \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        auto _a = (a);                                                         \
        auto _b = (b);                                                         \
        if (!(_a == _b)) {                                                     \
            std::fprintf(stderr, "  FAIL %s:%d: %s != %s\n", __FILE__,         \
                         __LINE__, #a, #b);                                    \
        }                                                                      \
    } while (0)

bool has_tone(const vawk::widget::LogLine& line, const std::string& text,
              LogTone tone) {
    for (const auto& span : line.spans)
        if (span.text.find(text) != std::string::npos && span.tone == tone)
            return true;
    return false;
}

void test_traffic() {
    std::puts("test_traffic");

    const std::string raw =
        "time=\"2026-11-14T17:20:01.123456789+08:00\" level=info "
        "msg=\"[TCP] 1.2.3.4:52344 --> www.google.com:443 "
        "match DomainSuffix(google) using 🇸🇬 新加坡01[直连]\"";

    auto line = format_log_line(LogLevel::Info, raw);
    CHECK_EQ(line.text,
             std::string("17:20:01 TCP www.google.com → 🇸🇬 新加坡01[直连]"));
    CHECK_EQ(line.spans.size(), std::size_t{5});
    CHECK(has_tone(line, "17:20:01", LogTone::Dim));
    CHECK(has_tone(line, "TCP", LogTone::Proto));
    CHECK(has_tone(line, "www.google.com", LogTone::Host));
    CHECK(has_tone(line, "新加坡01", LogTone::Out));
    CHECK(line.raw == raw);
    CHECK(line.raw.find("time=") != std::string::npos);
}

void test_udp_direct() {
    std::puts("test_udp_direct");

    auto line = format_log_line(
        LogLevel::Info,
        "[UDP] 8.8.8.8:53 --> 8.8.8.8:53 match GeoIP(private) using DIRECT");
    // 没有 time= 时用当前时间，只检查后半段。
    CHECK(line.text.find("UDP 8.8.8.8 → DIRECT") != std::string::npos);
    CHECK(has_tone(line, "DIRECT", LogTone::Direct));
    // 规则不进精简行（在 raw 里）。
    CHECK(line.text.find("GeoIP") == std::string::npos);
    CHECK(line.raw.find("GeoIP") != std::string::npos);
}

void test_reject() {
    std::puts("test_reject");
    auto line = format_log_line(
        LogLevel::Info,
        "[TCP] 1.2.3.4:1 --> ads.example.com:443 match RuleSet(ads) using REJECT");
    CHECK(line.text.find(" → REJECT") != std::string::npos);
    CHECK(has_tone(line, "REJECT", LogTone::Reject));
}

void test_error_warn() {
    std::puts("test_error_warn");

    auto err = format_log_line(
        LogLevel::Error,
        "time=\"2026-11-14T17:21:00.000+08:00\" level=error "
        "msg=\"rule-set load failed: geosite-cn: no such file\"");
    CHECK_EQ(err.text,
             std::string("17:21:00 ERR rule-set load failed: geosite-cn: no such file"));
    CHECK(has_tone(err, "ERR", LogTone::Error));
    CHECK(has_tone(err, "rule-set", LogTone::Error));

    auto warn = format_log_line(
        LogLevel::Warn,
        "time=\"2026-11-14T17:21:02.000+08:00\" level=warning "
        "msg=\"[TCP] dial 日本01: connection timeout\"");
    CHECK_EQ(warn.text, std::string("17:21:02 WRN TCP dial 日本01: connection timeout"));
    CHECK(has_tone(warn, "WRN", LogTone::Warn));

    // 级别是 info，但正文带 error: → 也要当错误处理。
    auto info_err = format_log_line(
        LogLevel::Info, "[TCP] dial DIRECT: error: connection refused");
    CHECK(info_err.text.find(" ERR ") != std::string::npos);
    CHECK(has_tone(info_err, "ERR", LogTone::Error));
}

void test_shorten() {
    std::puts("test_shorten");

    const std::string long_host(60, 'a');
    auto line = format_log_line(
        LogLevel::Info,
        "[TCP] 1.1.1.1:1 --> " + long_host + ".example.com:443 match A using B");
    CHECK(line.text.find("…") != std::string::npos);
    CHECK(line.text.find("example.com") != std::string::npos);

    // 非默认端口保留。
    auto port = format_log_line(LogLevel::Info, "[TCP] 1.1.1.1:1 --> x.com:8443 using B");
    CHECK(port.text.find("x.com:8443") != std::string::npos);

    // 默认端口去掉。
    auto p443 = format_log_line(LogLevel::Info, "[TCP] 1.1.1.1:1 --> x.com:443 using B");
    CHECK(p443.text.find("x.com:443") == std::string::npos);
    CHECK(p443.text.find("x.com") != std::string::npos);
}

void test_odd_lines() {
    std::puts("test_odd_lines");

    auto plain = format_log_line(LogLevel::Info, "Mihomo Meta v1.19.34 linux amd64");
    CHECK(plain.text.find("Mihomo Meta v1.19.34") != std::string::npos);
    CHECK_EQ(plain.spans.size(), std::size_t{2}); // 时间 + 正文

    // 带转义引号的 msg 不能把行拆坏。
    auto quoted = format_log_line(
        LogLevel::Info,
        "time=\"2026-11-14T17:22:00.000+08:00\" level=info "
        "msg=\"[TCP] 1.1.1.1:1 --> x.com:443 using node \\\"a b\\\"\"");
    CHECK(quoted.text.find("x.com") != std::string::npos);
    CHECK(quoted.text.find("node") != std::string::npos);

    // 空行 / 只有包装字段。
    auto empty = format_log_line(LogLevel::Info, "");
    CHECK(!empty.text.empty());

    // ANSI 要被剥掉。
    auto ansi = format_log_line(LogLevel::Info, "\033[31mred\033[0m text");
    CHECK(ansi.raw.find('\033') == std::string::npos);
    CHECK(ansi.text.find("red text") != std::string::npos);
}

} // namespace

int main() {
    test_traffic();
    test_udp_direct();
    test_reject();
    test_error_warn();
    test_shorten();
    test_odd_lines();

    if (g_failures == 0) {
        std::puts("test_logfmt: OK");
        return 0;
    }
    std::fprintf(stderr, "test_logfmt: %d failure(s)\n", g_failures);
    return 1;
}
