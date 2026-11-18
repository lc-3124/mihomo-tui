//- test_use.cpp — 配置识别 / 兼容修正 / --use 落盘流程

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

#include "common.hpp"
#include "use.hpp"

using namespace mihomo;

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
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

int count_substr(const std::string& haystack, const std::string& needle) {
    int         n = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += needle.size();
    }
    return n;
}

const char* kYaml = R"(mixed-port: 7890
allow-lan: false
proxies:
  - name: "🇸🇬 新加坡01"
    type: vless
    server: sg.example.com
    port: 443
    alpn:
      - h2
      - http/1.1
    client-fingerprint: chrome
  - name: 日本01
    type: vless
    server: jp.example.com
    port: 443
    servername: jp.example.com
  - name: IP节点
    type: ss
    server: 10.0.0.1
    port: 8388
proxy-groups:
  - name: 🔰 节点选择
    type: select
    proxies:
      - DIRECT
rules:
  - MATCH,DIRECT
)";

void test_detect() {
    std::puts("test_detect");

    auto yaml = cfg::detect_format(kYaml);
    CHECK(yaml.format == cfg::Format::ClashYaml);
    CHECK_EQ(yaml.proxy_count, 3);

    // 没带 UA 时订阅返回的 base64 vless 列表
    auto b64 = cfg::detect_format("dmxlc3M6Ly9hYmNAMS4yLjMuNDo0NDM=");
    CHECK(b64.format == cfg::Format::Base64Nodes);
    CHECK(!b64.hint.empty());

    // 明文节点列表也要识别成「不是 YAML」而不是 Unknown
    auto plain = cfg::detect_format("vless://abc@1.2.3.4:443#node\n");
    CHECK(plain.format == cfg::Format::Base64Nodes);

    auto bad = cfg::detect_format("hello world\n");
    CHECK(bad.format == cfg::Format::Unknown);

    auto empty = cfg::detect_format("");
    CHECK(empty.format == cfg::Format::Unknown);

    // 顶层缩进的 proxies: 不算（避免误判嵌套结构）
    auto nested = cfg::detect_format("a:\n  proxies:\n    - x\n");
    CHECK(nested.format == cfg::Format::Unknown);
}

void test_base64() {
    std::puts("test_base64");
    CHECK_EQ(cfg::base64_decode("aGVsbG8="), "hello");
    CHECK_EQ(cfg::base64_decode("aGVs\nbG8="), "hello");
    CHECK_EQ(cfg::base64_decode("aGVsbG8"), "hello");
    CHECK_EQ(cfg::base64_decode("_-8="), "\xff\xef");
    CHECK_EQ(cfg::base64_decode("!!!!"), "");
}

void test_scan() {
    std::puts("test_scan");

    auto lines = cfg::split_lines(kYaml);
    auto proxies = cfg::scan_proxies(lines);
    CHECK_EQ(proxies.size(), std::size_t{3});

    CHECK_EQ(proxies[0].name, "🇸🇬 新加坡01");
    CHECK_EQ(proxies[0].server, "sg.example.com");
    CHECK_EQ(proxies[0].fingerprint, "chrome");
    CHECK(proxies[0].fp_line != cfg::kNoLine);
    CHECK_EQ(proxies[0].key_indent, std::size_t{4});

    CHECK_EQ(proxies[1].name, "日本01");
    CHECK(proxies[1].fp_line == cfg::kNoLine);

    // 嵌套列表 alpn 不能被当成新节点。
    CHECK_EQ(proxies[2].name, "IP节点");
    CHECK_EQ(proxies[2].server, "10.0.0.1");

    // 条目范围必须落在 proxies 段内。
    CHECK(proxies[0].begin < proxies[0].end);
    CHECK(proxies[2].end <= lines.size());
    CHECK(lines[proxies[2].end].find("proxy-groups") != std::string::npos);
}

void test_replace_value() {
    std::puts("test_replace_value");

    CHECK_EQ(cfg::replace_value("    server: a.com", "1.2.3.4"),
             "    server: 1.2.3.4");
    CHECK_EQ(cfg::replace_value("    server:   a.com  ", "1.2.3.4"),
             "    server:   1.2.3.4");
    CHECK_EQ(cfg::replace_value("    server: \"a.com\"", "1.2.3.4"),
             "    server: \"1.2.3.4\"");
    CHECK_EQ(cfg::replace_value("    server: a.com # cdn", "1.2.3.4"),
             "    server: 1.2.3.4  # cdn");
    CHECK_EQ(cfg::replace_value("    client-fingerprint:", "safari"),
             "    client-fingerprint: safari");
}

void test_compat() {
    std::puts("test_compat");

    auto lines = cfg::split_lines(kYaml);

    auto resolver = [](const std::string& host, int) -> std::string {
        if (host == "sg.example.com") return "203.0.113.7";
        return {};
    };

    auto report = cfg::apply_compat(lines, resolver);
    CHECK_EQ(report.pinned, 1);
    CHECK_EQ(report.unresolved, 1);
    CHECK_EQ(report.fingerprints, 3);

    const std::string out = cfg::join_lines(lines);
    CHECK(out.find("server: 203.0.113.7") != std::string::npos);
    CHECK(out.find("server: jp.example.com") != std::string::npos); // 解析失败保持原样
    CHECK_EQ(count_substr(out, "client-fingerprint: safari"), 3);
    CHECK(out.find("client-fingerprint: chrome") == std::string::npos);
    CHECK(out.find("name: \"🇸🇬 新加坡01\"") != std::string::npos);
    CHECK(out.find("alpn:") != std::string::npos);
    CHECK(out.find("proxy-groups:") != std::string::npos);
    CHECK(out.find("rules:") != std::string::npos);

    // 插入的 fingerprint 必须和相邻键同缩进，且在 proxy-groups 之前。
    CHECK(out.find("    client-fingerprint: safari\nproxy-groups:") != std::string::npos);
}

void test_needs_compat() {
    std::puts("test_needs_compat");

    // 域名 + chrome：需要修正。
    CHECK(cfg::config_needs_compat(cfg::split_lines(kYaml)));

    // 全 IP + safari：不需要。
    CHECK(!cfg::config_needs_compat(cfg::split_lines(
        "proxies:\n"
        "  - name: x\n"
        "    server: 1.2.3.4\n"
        "    client-fingerprint: safari\n")));

    // 缺指纹也要修正。
    CHECK(cfg::config_needs_compat(cfg::split_lines(
        "proxies:\n"
        "  - {name: x, server: 1.2.3.4}\n")));

    CHECK(!cfg::config_needs_compat(cfg::split_lines(
        "proxies:\n"
        "  - {name: x, server: 1.2.3.4, client-fingerprint: safari}\n")));
}

void test_dns_fix() {
    std::puts("test_dns_fix");

    // 内联 DoH 列表：需要修。
    const char* inline_doh =
        "dns:\n"
        "  enable: true\n"
        "  nameserver: ['https://doh.pub/dns-query', 'https://dns.alidns.com/dns-query']\n"
        "  enhanced-mode: fake-ip\n"
        "proxies:\n"
        "  - name: x\n"
        "    server: 1.2.3.4\n"
        "    client-fingerprint: safari\n";
    auto inline_lines = cfg::split_lines(inline_doh);
    CHECK(cfg::dns_needs_fix(inline_lines));
    CHECK(cfg::fix_dns_servers(inline_lines));
    CHECK(!cfg::dns_needs_fix(inline_lines));
    const std::string inline_out = cfg::join_lines(inline_lines);
    CHECK(inline_out.find("nameserver: [223.6.6.6, 119.28.28.28") != std::string::npos);
    CHECK(inline_out.find("https://") == std::string::npos);
    CHECK(inline_out.find("enhanced-mode: fake-ip") != std::string::npos);

    // 块式列表：也要修，且 fake-ip-filter 等其它键不能动。
    const char* block_doh =
        "dns:\n"
        "  nameserver:\n"
        "    - https://a.example/dns-query\n"
        "    - https://b.example/dns-query\n"
        "    - https://c.example/dns-query\n"
        "    - https://d.example/dns-query\n"
        "    - https://e.example/dns-query\n"
        "  fake-ip-filter: ['*.lan']\n"
        "proxies: []\n";
    auto block_lines = cfg::split_lines(block_doh);
    CHECK(cfg::dns_needs_fix(block_lines));
    CHECK(cfg::fix_dns_servers(block_lines));
    const std::string block_out = cfg::join_lines(block_lines);
    CHECK(block_out.find("- 223.6.6.6") != std::string::npos);
    CHECK(block_out.find("https://") == std::string::npos);
    CHECK(block_out.find("fake-ip-filter") != std::string::npos);
    CHECK_EQ(count_substr(block_out, "\n    - "), 4);

    // fallback / proxy-server-nameserver 全是 DoH 时也要修。
    const char* multi_doh =
        "dns:\n"
        "  nameserver: [223.6.6.6]\n"
        "  proxy-server-nameserver: ['https://223.6.6.6/dns-query', 'https://doh.pub/dns-query']\n"
        "  fallback: ['https://223.5.5.5/dns-query']\n"
        "  fallback-filter: { geoip: true, geoip-code: CN }\n"
        "proxies: []\n";
    auto multi_lines = cfg::split_lines(multi_doh);
    CHECK(cfg::dns_needs_fix(multi_lines));
    CHECK(cfg::fix_dns_servers(multi_lines));
    const std::string multi_out = cfg::join_lines(multi_lines);
    CHECK(multi_out.find("proxy-server-nameserver: [223.6.6.6") != std::string::npos);
    CHECK(multi_out.find("fallback: [223.6.6.6") != std::string::npos);
    CHECK(multi_out.find("https://") == std::string::npos);
    CHECK(multi_out.find("fallback-filter") != std::string::npos);
    CHECK(!cfg::dns_needs_fix(multi_lines));

    // 已经有 IP 的不要动。
    auto plain = cfg::split_lines(
        "dns:\n  nameserver: [223.6.6.6]\n");
    CHECK(!cfg::dns_needs_fix(plain));
    CHECK(!cfg::fix_dns_servers(plain));

    // 有 DNS 问题的配置，即使节点已兼容，也必须触发兼容修正。
    auto mixed = cfg::split_lines(
        "dns:\n  nameserver: ['https://doh.pub/dns-query']\n"
        "proxies:\n  - name: x\n    server: 1.2.3.4\n    client-fingerprint: safari\n");
    CHECK(cfg::config_needs_compat(mixed));
}

void test_flow_compat() {
    std::puts("test_flow_compat");

    // 带空格、嵌套 reality-opts 的单行流式写法——真实订阅就是这种。
    const char* yaml =
        "proxies:\n"
        "    - { name: 'A', type: vless, server: sg.example.com, port: 443, "
        "reality-opts: { public-key: x, short-id: y }, client-fingerprint: chrome }\n"
        "    - { name: 'B', type: vless, server: 1.2.3.4, port: 443 }\n"
        "proxy-groups:\n"
        "    - { name: sel, type: select, proxies: [A] }\n";

    auto lines   = cfg::split_lines(yaml);
    auto entries = cfg::scan_proxies(lines);
    CHECK_EQ(entries.size(), std::size_t{2});
    CHECK_EQ(entries[0].server, "sg.example.com");
    CHECK_EQ(entries[0].fingerprint, "chrome");
    CHECK_EQ(entries[1].server, "1.2.3.4");

    auto resolver = [](const std::string& host, int) -> std::string {
        return host == "sg.example.com" ? "203.0.113.7" : std::string{};
    };
    auto report = cfg::apply_compat(lines, resolver);
    CHECK_EQ(report.pinned, 1);
    CHECK_EQ(report.fingerprints, 2);
    CHECK_EQ(report.unresolved, 0);

    const std::string out = cfg::join_lines(lines);
    CHECK(out.find("server: 203.0.113.7") != std::string::npos);
    CHECK(out.find("client-fingerprint: safari") != std::string::npos);
    // 不能出现「safari + 旧值残留」这种脏数据。
    CHECK(out.find("safarie") == std::string::npos);
    CHECK(out.find("safariq") == std::string::npos);
    CHECK(out.find("safarii") == std::string::npos);
    // 追加项不能吃掉花括号：结构符号数量必须和原来一致。
    CHECK_EQ(count_substr(out, "{"), count_substr(std::string(yaml), "{"));
    CHECK_EQ(count_substr(out, "}"), count_substr(std::string(yaml), "}"));
}

void test_roundtrip() {
    std::puts("test_roundtrip");

    auto lines = cfg::split_lines(kYaml);
    auto joined = cfg::join_lines(lines);
    auto again = cfg::split_lines(joined);
    CHECK_EQ(lines.size(), again.size());
    CHECK(joined.find("proxies:\n") != std::string::npos);

    auto crlf = cfg::split_lines("a\r\nb\r\n");
    CHECK_EQ(crlf.size(), std::size_t{2});
    CHECK_EQ(crlf[0], "a");

    auto no_trailing = cfg::split_lines("a\nb");
    CHECK_EQ(no_trailing.size(), std::size_t{2});
    CHECK_EQ(no_trailing[1], "b");
}

void test_format() {
    std::puts("test_format");

    CHECK_EQ(format_delay(-3), "…");
    CHECK_EQ(format_delay(-2), "超时");
    CHECK_EQ(format_delay(0), "超时");
    CHECK_EQ(format_delay(-1), "—");
    CHECK_EQ(format_delay(921), "921ms");
    CHECK_EQ(format_bytes(1536), "1.5 KB");
    CHECK_EQ(format_bytes(88.0 * 1024 * 1024), "88 MB");
    CHECK_EQ(format_bytes_short(88.0 * 1024 * 1024), "88M");
}

std::string make_temp_dir() {
    char tmpl[] = "/tmp/mihomo-tui-test-XXXXXX";
    char* dir   = ::mkdtemp(tmpl);
    return dir ? std::string(dir) : std::string{};
}

void test_cli_write() {
    std::puts("test_cli_write");

    const std::string dir = make_temp_dir();
    CHECK(!dir.empty());
    if (dir.empty()) return;

    const std::string source = dir + "/source.yaml";
    const std::string target = dir + "/config.yaml";

    std::string error;
    CHECK(write_file_atomic(source, kYaml, error));

    UseOptions options;
    options.source      = source;
    options.config_path = target;
    options.compat      = true;
    options.restart     = false; // 只落盘，不碰内核
    options.offline     = false;

    std::ostringstream out;
    int code = run_use(options, out);
    CHECK_EQ(code, 0);
    CHECK(file_exists(target));

    struct stat st{};
    CHECK_EQ(::stat(target.c_str(), &st), 0);
    CHECK_EQ(st.st_mode & 0777, mode_t{0600});

    auto written = read_file(target);
    CHECK(written.has_value());
    if (written) {
        CHECK(written->find("client-fingerprint: safari") != std::string::npos);
        CHECK(written->find("proxy-groups:") != std::string::npos);
    }

    CHECK(out.str().find("[1/7] 来源: 本地文件") != std::string::npos);
    CHECK(out.str().find("[6/7] 临时文件加载验证 ... 跳过（--no-restart 只落盘）")
          != std::string::npos);
    CHECK(out.str().find("[7/7] 落盘 + 生效 ... 已落盘，未热重载") != std::string::npos);

    // 第二次运行会先备份。
    std::ostringstream out2;
    CHECK_EQ(run_use(options, out2), 0);
    CHECK(out2.str().find("config.yaml.bak-") != std::string::npos);

    // 坏文件不能覆盖目标。
    const std::string bad = dir + "/bad.yaml";
    CHECK(write_file_atomic(bad, "not a config\n", error));
    auto before = read_file(target);

    UseOptions bad_opts = options;
    bad_opts.source = bad;
    std::ostringstream out3;
    CHECK_EQ(run_use(bad_opts, out3), 2);
    CHECK(out3.str().find("格式识别 ... 失败") != std::string::npos);

    auto after = read_file(target);
    CHECK(before == after);

    // 目标不存在时不再创建。
    const std::string nonexistent = dir + "/nope.yaml";
    bad_opts.config_path = nonexistent;
    std::ostringstream out4;
    CHECK_EQ(run_use(bad_opts, out4), 2);
    CHECK(!file_exists(nonexistent));
}

void test_cli_auto_compat() {
    std::puts("test_cli_auto_compat");

    const std::string dir = make_temp_dir();
    CHECK(!dir.empty());
    if (dir.empty()) return;

    const std::string source = dir + "/source.yaml";
    const std::string target = dir + "/config.yaml";
    std::string error;
    CHECK(write_file_atomic(source, kYaml, error));

    // 不加 --compat：检测到域名/非 safari 指纹后应自动修正。
    UseOptions auto_opts;
    auto_opts.source      = source;
    auto_opts.config_path = target;
    auto_opts.restart     = false;

    std::ostringstream out;
    CHECK_EQ(run_use(auto_opts, out), 0);
    CHECK(out.str().find("自动启用") != std::string::npos);
    auto written = read_file(target);
    CHECK(written.has_value());
    if (written) CHECK(written->find("client-fingerprint: safari") != std::string::npos);

    // --no-compat：原样落盘，不改指纹。
    const std::string target2 = dir + "/config2.yaml";
    UseOptions     raw_opts   = auto_opts;
    raw_opts.config_path      = target2;
    raw_opts.no_compat        = true;

    std::ostringstream out2;
    CHECK_EQ(run_use(raw_opts, out2), 0);
    CHECK(out2.str().find("跳过（--no-compat）") != std::string::npos);
    auto raw = read_file(target2);
    CHECK(raw.has_value());
    if (raw) CHECK(raw->find("client-fingerprint: chrome") != std::string::npos);
}

void test_cli_validate_failure() {
    std::puts("test_cli_validate_failure");

    const std::string dir = make_temp_dir();
    CHECK(!dir.empty());
    if (dir.empty()) return;

    const std::string source = dir + "/source.yaml";
    const std::string target = dir + "/config.yaml";
    std::string error;
    CHECK(write_file_atomic(source, kYaml, error));
    CHECK(write_file_atomic(target, "old-config: true\n", error));

    UseOptions options;
    options.source      = source;
    options.config_path = target;
    options.compat      = false;
    options.restart     = true;
    options.api_base    = "http://127.0.0.1:1"; // 必然连不上

    std::ostringstream out;
    CHECK_EQ(run_use(options, out), 4);
    CHECK(out.str().find("内核 API 不可达") != std::string::npos);

    // 校验失败：目标文件保持原样。
    auto content = read_file(target);
    CHECK(content.has_value());
    CHECK_EQ(*content, "old-config: true\n");
}

void test_placeholder() {
    std::puts("test_placeholder");

    const std::string dir = make_temp_dir();
    CHECK(!dir.empty());
    if (dir.empty()) return;

    const std::string source = dir + "/placeholder.yaml";
    const std::string target = dir + "/config.yaml";
    std::string error;
    CHECK(write_file_atomic(source,
        "proxies:\n"
        "  - {name: 您的服务已到期, type: ss, server: 127.0.0.1, port: 443}\n"
        "  - {name: 请登入去续费, type: ss, server: 127.0.0.1, port: 443}\n",
        error));

    UseOptions options;
    options.source      = source;
    options.config_path = target;
    options.restart     = false;

    std::ostringstream out;
    CHECK_EQ(run_use(options, out), 2);
    CHECK(out.str().find("占位节点") != std::string::npos);
    CHECK(!file_exists(target));
}

void test_cli_offline() {
    std::puts("test_cli_offline");

    const std::string dir = make_temp_dir();
    CHECK(!dir.empty());
    if (dir.empty()) return;

    const std::string source = dir + "/source.yaml";
    const std::string target = dir + "/config.yaml";
    std::string error;
    CHECK(write_file_atomic(source, kYaml, error));

    UseOptions options;
    options.source      = source;
    options.config_path = target;
    options.offline     = true;
    options.restart     = false; // 只落盘；避免把测试配置喂给真内核

    std::ostringstream out;
    CHECK_EQ(run_use(options, out), 0);
    CHECK(out.str().find("跳过（--offline）") != std::string::npos);
    CHECK(file_exists(target));
}

} // namespace

int main() {
    test_detect();
    test_base64();
    test_scan();
    test_replace_value();
    test_compat();
    test_needs_compat();
    test_dns_fix();
    test_flow_compat();
    test_roundtrip();
    test_format();
    test_cli_write();
    test_cli_auto_compat();
    test_cli_validate_failure();
    test_cli_offline();
    test_placeholder();

    if (g_failures == 0) {
        std::puts("test_use: OK");
        return 0;
    }
    std::fprintf(stderr, "test_use: %d failure(s)\n", g_failures);
    return 1;
}
