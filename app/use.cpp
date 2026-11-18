//- use.cpp — --use 实现：下载 / 识别 / 兼容修正 / 验证 / 落盘 / 回滚

#include "use.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <unordered_map>

#include <curl/curl.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "api.hpp"
#include "common.hpp"
#include "dns.hpp"

namespace mihomo::cfg {

namespace {

struct LineInfo {
    int              indent = 0;
    std::string_view text; ///< 去掉缩进后的内容
};

LineInfo analyze(const std::string& line) {
    std::size_t i = 0;
    int         ind = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
        ind += (line[i] == '\t') ? 4 : 1;
        ++i;
    }
    return LineInfo{ind, trim(std::string_view(line).substr(i))};
}

// 取 YAML 标量值：去引号、去行尾注释。
std::string scalar_value(std::string_view raw) {
    raw = trim(raw);
    if (raw.empty()) return {};
    if (raw[0] == '"' || raw[0] == '\'') {
        char        q   = raw[0];
        std::size_t end = raw.find(q, 1);
        if (end == std::string_view::npos) return std::string(raw.substr(1));
        return std::string(raw.substr(1, end - 1));
    }
    std::size_t hash = raw.find('#');
    if (hash != std::string_view::npos) raw = raw.substr(0, hash);
    return std::string(trim(raw));
}

void handle_key(ProxyEntry& entry, std::string_view text, int indent,
                std::size_t line_index) {
    std::size_t colon = text.find(':');
    if (colon == std::string_view::npos) return;

    std::string_view key = trim(text.substr(0, colon));
    std::string      val = scalar_value(text.substr(colon + 1));

    if (key == "name" && entry.name_line == kNoLine) {
        entry.name_line = line_index;
        entry.name      = val;
    } else if (key == "server" && entry.server_line == kNoLine) {
        entry.server_line = line_index;
        entry.server      = val;
    } else if (key == "client-fingerprint") {
        entry.fp_line     = line_index;
        entry.fingerprint = val;
    } else if (key == "port") {
        entry.port = std::atoi(val.c_str());
    } else {
        return;
    }
    if (entry.key_indent == 0) entry.key_indent = static_cast<std::size_t>(indent);
}

// 流式映射（Clash 订阅的常见写法：一条节点就是一行 `- {name: ..., server: ...}`）。
// 下面两个函数只在 `{...}` 内部做「顶层逗号」切分，嵌套的 reality-opts: {...}
// 和带引号/转义的值都不会被误切。

bool find_flow_bounds(const std::string& line, std::size_t& open, std::size_t& close) {
    auto brace = line.find('{');
    if (brace == std::string::npos) return false;

    int  depth = 0;
    char quote = 0;
    for (std::size_t i = brace; i < line.size(); ++i) {
        char c = line[i];
        if (quote) {
            if (c == '\\' && quote == '"') { ++i; continue; }
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') {
            --depth;
            if (depth == 0) { open = brace; close = i; return true; }
        }
    }
    return false;
}

// 在 `{...}` 的顶层找 key，返回值的区间 [vb, ve)。
bool flow_find(const std::string& line, std::size_t open, std::size_t close,
               std::string_view key, std::size_t& vb, std::size_t& ve) {
    std::size_t item_start = open + 1;
    int         depth      = 0;
    char        quote      = 0;

    auto parse_item = [&](std::size_t end) -> bool {
        std::string_view item(line.data() + item_start, end - item_start);
        // 去掉首尾空白；注意 base 是「去空白之后」在 line 里的真实起点，
        // 否则 `{ name: x }` 这种带空格的写法会整体偏一格。
        while (!item.empty() && (item.front() == ' ' || item.front() == '\t'))
            item.remove_prefix(1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t'))
            item.remove_suffix(1);
        if (item.empty()) return false;

        const std::size_t base = static_cast<std::size_t>(item.data() - line.data());

        std::size_t colon = item.find(':');
        if (colon == std::string_view::npos) return false;
        if (trim(item.substr(0, colon)) != key) return false;

        std::string_view value = item.substr(colon + 1);
        std::size_t      lead  = 0;
        while (lead < value.size() && (value[lead] == ' ' || value[lead] == '\t'))
            ++lead;
        std::size_t trail = value.size();
        while (trail > lead && (value[trail - 1] == ' ' || value[trail - 1] == '\t'))
            --trail;

        vb = base + colon + 1 + lead;
        ve = base + colon + 1 + trail;
        return true;
    };

    for (std::size_t i = open + 1; i <= close; ++i) {
        char c = (i < close) ? line[i] : ',';
        if (quote) {
            if (c == '\\' && quote == '"') { ++i; continue; }
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; continue; }
        if (c == '{' || c == '[') { ++depth; continue; }
        if (c == '}' || c == ']') { --depth; continue; }
        if (c == ',' && depth == 0) {
            if (parse_item(i)) return true;
            item_start = i + 1;
        }
    }
    return false;
}

// 替换 [vb, ve) 的值，保留原有引号风格。
std::string flow_replace_value(const std::string& line, std::size_t vb,
                               std::size_t ve, std::string_view value) {
    char quote = 0;
    if (ve > vb && (line[vb] == '"' || line[vb] == '\'')) quote = line[vb];
    std::string v(value);
    if (quote) {
        v.insert(v.begin(), quote);
        v.push_back(quote);
    }
    return line.substr(0, vb) + v + line.substr(ve);
}

} // namespace

std::vector<std::string> split_lines(std::string_view content) {
    std::vector<std::string> lines;
    std::size_t              start = 0;
    while (start <= content.size()) {
        std::size_t nl = content.find('\n', start);
        if (nl == std::string_view::npos) {
            if (start < content.size())
                lines.emplace_back(content.substr(start));
            break;
        }
        std::string line(content.substr(start, nl - start));
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        start = nl + 1;
        if (start == content.size()) break;
    }
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& line : lines) {
        out += line;
        out += '\n';
    }
    return out;
}

std::string base64_decode(std::string_view text) {
    static constexpr int8_t kInvalid = -1;
    int8_t table[256];
    std::memset(table, kInvalid, sizeof table);
    const char* alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(alphabet[i])] = static_cast<int8_t>(i);
    // URL-safe 变体
    table[static_cast<unsigned char>('-')] = 62;
    table[static_cast<unsigned char>('_')] = 63;

    std::string out;
    out.reserve(text.size() * 3 / 4);

    uint32_t acc  = 0;
    int      bits = 0;
    for (unsigned char c : text) {
        if (c == '=' ) break;
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        int8_t v = table[c];
        if (v < 0) return {};
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

DetectResult detect_format(std::string_view content) {
    DetectResult result;

    if (starts_with(content, "\xEF\xBB\xBF")) content.remove_prefix(3);

    // 顶层 proxies:（允许前面有注释，但不允许缩进）
    bool              has_proxies = false;
    std::string_view  rest        = content;
    while (!rest.empty()) {
        std::size_t      nl   = rest.find('\n');
        std::string_view line = rest.substr(0, nl);
        if (nl == std::string_view::npos) rest = {};
        else rest = rest.substr(nl + 1);

        // 必须是顶格的 proxies:；缩进的同名键属于嵌套结构，不算。
        if (line.size() >= 8 && line.substr(0, 8) == "proxies:"
            && (line.size() == 8 || line[8] == ' ' || line[8] == '\t'
                || line[8] == '\r')) {
            has_proxies = true;
            break;
        }
    }

    if (has_proxies) {
        auto lines = split_lines(content);
        result.format      = Format::ClashYaml;
        result.proxy_count = static_cast<int>(scan_proxies(lines).size());
        return result;
    }

    // 不是 YAML：可能是 base64 的 vless:// 列表（订阅没带 UA 的典型产物）
    auto decoded = base64_decode(content);
    auto looks_like_nodes = [](std::string_view s) {
        return s.find("vless://") != std::string_view::npos
            || s.find("vmess://") != std::string_view::npos
            || s.find("trojan://") != std::string_view::npos
            || s.find("ss://") != std::string_view::npos
            || s.find("hysteria2://") != std::string_view::npos;
    };

    if (!decoded.empty() && looks_like_nodes(decoded)) {
        result.format = Format::Base64Nodes;
        result.hint   = "订阅返回了 base64 节点列表，不是 Clash YAML";
        return result;
    }
    if (looks_like_nodes(content)) {
        result.format = Format::Base64Nodes;
        result.hint   = "内容是节点链接列表，不是 Clash YAML";
        return result;
    }

    result.hint = "既不是 Clash YAML（缺 proxies:），也不是 base64 节点列表";
    return result;
}

std::vector<ProxyEntry> scan_proxies(const std::vector<std::string>& lines) {
    std::vector<ProxyEntry> out;
    std::size_t             start = kNoLine;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto info = analyze(lines[i]);
        if (info.indent == 0 && info.text == "proxies:") {
            start = i;
            break;
        }
    }
    if (start == kNoLine) return out;

    ProxyEntry  current;
    bool        in_item    = false;
    std::size_t item_indent = kNoLine;

    auto flush = [&](std::size_t end) {
        if (!in_item) return;
        current.end = end;
        if (current.begin != kNoLine) out.push_back(current);
        in_item = false;
    };

    for (std::size_t i = start + 1; i < lines.size(); ++i) {
        const auto info = analyze(lines[i]);
        if (info.text.empty() || info.text[0] == '#') continue;

        const bool is_dash = info.text == "-" || starts_with(info.text, "- ")
                          || (info.text.size() > 1 && info.text[0] == '-'
                              && (info.text[1] == '{' || info.text[1] == '['));

        // 只有与外层条目同缩进的 "-" 才是新节点；嵌套列表（alpn 等）缩进更深。
        if (is_dash && (item_indent == kNoLine
                        || info.indent == static_cast<int>(item_indent))) {
            if (item_indent == kNoLine)
                item_indent = static_cast<std::size_t>(info.indent);

            flush(i);
            current             = ProxyEntry{};
            current.begin       = i;
            current.dash_indent = item_indent;
            current.key_indent  = item_indent + 2;
            in_item             = true;

            std::string_view after = trim(info.text.substr(1));
            std::size_t      open = 0, close = 0;
            if (!after.empty() && after[0] == '{'
                && find_flow_bounds(lines[i], open, close)) {
                // 单行流式映射：直接从 {...} 里取字段。
                current.flow = true;
                auto grab = [&](std::string_view key, std::string& out) {
                    std::size_t vb = 0, ve = 0;
                    if (flow_find(lines[i], open, close, key, vb, ve))
                        out = scalar_value(std::string_view(lines[i]).substr(vb, ve - vb));
                };
                grab("name", current.name);
                grab("server", current.server);
                grab("client-fingerprint", current.fingerprint);
                std::string port_text;
                grab("port", port_text);
                if (!port_text.empty()) current.port = std::atoi(port_text.c_str());
            } else if (!after.empty()) {
                handle_key(current, after, info.indent + 2, i);
            }
            continue;
        }

        // 缩进回到顶层（或比条目更浅）：proxies 段结束。
        if (item_indent == kNoLine) continue;
        if (info.indent <= static_cast<int>(item_indent)) {
            flush(i);
            break;
        }

        if (in_item) handle_key(current, info.text, info.indent, i);
    }

    flush(lines.size());
    return out;
}


std::string replace_value(const std::string& line, std::string_view value) {
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) return line;

    std::string      head = line.substr(0, colon + 1);
    std::string_view rest(line.data() + colon + 1, line.size() - colon - 1);

    std::size_t ws = 0;
    while (ws < rest.size() && (rest[ws] == ' ' || rest[ws] == '\t')) ++ws;
    std::string      lead(rest.substr(0, ws));
    std::string_view body = rest.substr(ws);
    if (lead.empty() && body.empty()) lead = " "; // `key:` → `key: 值`

    char quote = 0;
    if (!body.empty() && (body[0] == '"' || body[0] == '\'')) quote = body[0];

    std::string comment;
    std::size_t hash = body.find('#');
    if (hash != std::string_view::npos) comment = std::string(trim(body.substr(hash)));

    std::string v(value);
    if (quote) {
        v.insert(v.begin(), quote);
        v.push_back(quote);
    }

    std::string out = head + lead + v;
    if (!comment.empty()) out += "  " + comment;
    return out;
}


namespace {

// 去掉 YAML 列表项/标量两端的引号与空白。
std::string unquote(std::string_view text) {
    text = trim(text);
    if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'')
        && text.back() == text.front())
        text = text.substr(1, text.size() - 2);
    return std::string(text);
}

// 拆 `[a, b, c]`；只在顶层逗号切。
std::vector<std::string> split_inline_list(std::string_view value) {
    if (!value.empty() && value.front() == '[') value.remove_prefix(1);
    if (!value.empty() && value.back() == ']') value.remove_suffix(1);

    std::vector<std::string> out;
    std::string              cur;
    char                     quote = 0;
    int                      depth = 0;
    for (char c : value) {
        if (quote) {
            cur.push_back(c);
            if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; cur.push_back(c); continue; }
        if (c == '[' || c == '{') ++depth;
        if (c == ']' || c == '}') --depth;
        if (c == ',' && depth == 0) {
            out.push_back(unquote(cur));
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    out.push_back(unquote(cur));
    return out;
}

struct DnsView {
    bool                     found  = false;
    bool                     inline_form = false;
    std::size_t              ns_line = kNoLine;
    std::size_t              ns_indent = 0;
    std::vector<std::size_t> item_lines;
    std::vector<std::string> values;
};

// 找 dns.<key>：支持 `key: [a, b]` 和块式 `key:\n  - a`。
DnsView inspect_dns_key(const std::vector<std::string>& lines, std::string_view key) {
    DnsView view;

    std::size_t dns_start = kNoLine;
    std::size_t dns_end   = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto info = analyze(lines[i]);
        if (info.indent == 0 && info.text == "dns:") { dns_start = i; break; }
    }
    if (dns_start == kNoLine) return view;

    for (std::size_t i = dns_start + 1; i < lines.size(); ++i) {
        auto info = analyze(lines[i]);
        if (info.indent == 0) { dns_end = i; break; }
    }

    for (std::size_t i = dns_start + 1; i < dns_end; ++i) {
        auto info = analyze(lines[i]);

        std::size_t colon = info.text.find(':');
        if (colon == std::string_view::npos) continue;
        if (trim(info.text.substr(0, colon)) != key) continue;

        view.found     = true;
        view.ns_line   = i;
        view.ns_indent = static_cast<std::size_t>(info.indent);

        std::string_view rest = trim(info.text.substr(colon + 1));
        if (!rest.empty() && rest.front() == '[') {
            view.inline_form = true;
            view.values      = split_inline_list(rest);
        } else if (rest.empty()) {
            for (std::size_t j = i + 1; j < dns_end; ++j) {
                auto sub = analyze(lines[j]);
                if (sub.indent <= info.indent) break;
                if (!starts_with(sub.text, "-")) break;
                std::string value = unquote(trim(sub.text.substr(1)));
                if (value.empty()) continue;
                view.item_lines.push_back(j);
                view.values.push_back(value);
            }
        } else {
            // fallback: null 之类，不当作列表。
            view.values = {std::string(rest)};
        }
        break;
    }
    return view;
}

bool has_plain_ip(const std::vector<std::string>& values) {
    for (const auto& value : values)
        if (is_ipv4(value)) return true;
    return false;
}

const std::vector<std::string>& local_dns_servers() {
    static const std::vector<std::string> kServers = {
        "223.6.6.6", "119.28.28.28", "114.114.114.114", "180.76.76.76"};
    return kServers;
}

const std::vector<std::string_view>& dns_list_keys() {
    static const std::vector<std::string_view> kKeys = {
        "nameserver", "fallback", "proxy-server-nameserver", "default-nameserver"};
    return kKeys;
}

// 只有「确实是一个列表且没有一个 IP」才需要修。
bool view_needs_fix(const DnsView& view) {
    if (!view.found || view.values.empty()) return false;
    return !has_plain_ip(view.values);
}

bool rewrite_dns_list(std::vector<std::string>& lines, const DnsView& view,
                      std::string_view key) {
    const auto& servers = local_dns_servers();

    if (view.inline_form) {
        std::string indent(static_cast<std::size_t>(view.ns_indent), ' ');
        std::string line = indent;
        line += key;
        line += ": [";
        for (std::size_t i = 0; i < servers.size(); ++i) {
            if (i) line += ", ";
            line += servers[i];
        }
        line += "]";
        lines[view.ns_line] = std::move(line);
        return true;
    }

    if (view.item_lines.empty()) return false;

    const std::string first_indent =
        std::string(static_cast<std::size_t>(analyze(lines[view.item_lines[0]]).indent), ' ');

    for (std::size_t i = 0; i < servers.size(); ++i) {
        if (i < view.item_lines.size())
            lines[view.item_lines[i]] = first_indent + "- " + servers[i];
        else
            lines.insert(lines.begin() + static_cast<long>(view.item_lines.back() + 1 + (i - view.item_lines.size())),
                         first_indent + "- " + servers[i]);
    }
    for (std::size_t i = view.item_lines.size(); i > servers.size(); --i)
        lines.erase(lines.begin() + static_cast<long>(view.item_lines[i - 1]));

    return true;
}

} // namespace

bool dns_needs_fix(const std::vector<std::string>& lines) {
    for (auto key : dns_list_keys())
        if (view_needs_fix(inspect_dns_key(lines, key))) return true;
    return false;
}

bool fix_dns_servers(std::vector<std::string>& lines) {
    bool changed = false;
    for (auto key : dns_list_keys()) {
        // 每次重新 inspect：前面的改写可能移动行号。
        auto view = inspect_dns_key(lines, key);
        if (!view_needs_fix(view)) continue;
        changed |= rewrite_dns_list(lines, view, key);
    }
    return changed;
}

bool config_needs_compat(const std::vector<std::string>& lines) {
    for (const auto& entry : scan_proxies(lines)) {
        if (!entry.server.empty() && !is_ipv4(entry.server)) return true;
        if (entry.fingerprint != "safari") return true;
    }
    // DNS 全是 DoH/域名、没有一个可用 IP 的，也要修，否则国内站会解析失败。
    return dns_needs_fix(lines);
}

CompatReport apply_compat(std::vector<std::string>& lines, const Resolver& resolver) {
    CompatReport report;
    auto         entries = scan_proxies(lines);

    std::unordered_map<std::string, std::string> cache;

    for (auto& entry : entries) {
        if (entry.server.empty() || is_ipv4(entry.server)) continue;
        if (!entry.flow && entry.server_line == kNoLine) continue;

        std::string ip;
        auto        it = cache.find(entry.server);
        if (it != cache.end()) {
            ip = it->second;
        } else {
            ip = resolver ? resolver(entry.server, entry.port) : std::string{};
            cache.emplace(entry.server, ip);
        }

        if (ip.empty()) {
            ++report.unresolved;
            report.unresolved_hosts.push_back(entry.server);
            continue;
        }

        if (entry.flow) {
            std::size_t open = 0, close = 0, vb = 0, ve = 0;
            if (!find_flow_bounds(lines[entry.begin], open, close)
                || !flow_find(lines[entry.begin], open, close, "server", vb, ve)) {
                ++report.unresolved;
                report.unresolved_hosts.push_back(entry.server);
                continue;
            }
            lines[entry.begin] = flow_replace_value(lines[entry.begin], vb, ve, ip);
            ++report.pinned;
        } else {
            lines[entry.server_line] = replace_value(lines[entry.server_line], ip);
            ++report.pinned;
        }
    }

    struct Insert {
        std::size_t at;
        std::size_t indent;
    };
    std::vector<Insert> inserts;

    for (const auto& entry : entries) {
        if (entry.flow) {
            std::size_t open = 0, close = 0, vb = 0, ve = 0;
            if (!find_flow_bounds(lines[entry.begin], open, close)) continue;
            if (flow_find(lines[entry.begin], open, close, "client-fingerprint", vb, ve)) {
                std::string current =
                    scalar_value(std::string_view(lines[entry.begin]).substr(vb, ve - vb));
                if (current != "safari") {
                    lines[entry.begin] = flow_replace_value(lines[entry.begin], vb, ve, "safari");
                    ++report.fingerprints;
                }
            } else {
                // 没有就在右花括号前补一项；流式映射里这是合法的。
                lines[entry.begin].insert(close, ", client-fingerprint: safari");
                ++report.fingerprints;
            }
            continue;
        }

        if (entry.fp_line != kNoLine) {
            if (entry.fingerprint != "safari") {
                lines[entry.fp_line] = replace_value(lines[entry.fp_line], "safari");
                ++report.fingerprints;
            }
        } else {
            inserts.push_back(Insert{entry.end, entry.key_indent});
        }
    }

    // 从后往前插，避免行号失效。
    std::sort(inserts.begin(), inserts.end(),
              [](const Insert& a, const Insert& b) { return a.at > b.at; });
    for (const auto& ins : inserts) {
        if (ins.at > lines.size()) continue;
        std::string line(ins.indent, ' ');
        line += "client-fingerprint: safari";
        lines.insert(lines.begin() + static_cast<long>(ins.at), std::move(line));
        ++report.fingerprints;
    }

    return report;
}

} // namespace mihomo::cfg

namespace mihomo {

namespace {

using cfg::split_lines;
using cfg::join_lines;

bool is_url(std::string_view source) {
    return starts_with(source, "http://") || starts_with(source, "https://");
}

struct DownloadOutcome {
    bool        ok = false;
    long        status = 0;
    std::string body;
    std::string error;
    double      seconds = 0;
    bool        live_progress = false;
};

std::size_t write_body(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct ProgressCtx {
    std::ostream* out = nullptr;
    bool          live = false;
    long long     total = 0;
    int           last_percent = -1;
    std::chrono::steady_clock::time_point start;
};

int progress_cb(void* userdata, curl_off_t dltotal, curl_off_t dlnow,
                curl_off_t, curl_off_t) {
    auto* ctx = static_cast<ProgressCtx*>(userdata);
    if (!ctx->live || !ctx->out) return 0;
    long long total = static_cast<long long>(dltotal);
    long long now   = static_cast<long long>(dlnow);
    if (total <= 0) {
        if (now > 0 && ctx->last_percent < 0) {
            *ctx->out << "\r[2/7] 下载 ... " << format_bytes(static_cast<double>(now));
            ctx->out->flush();
            ctx->last_percent = 0;
        }
        return 0;
    }
    int percent = static_cast<int>(now * 100 / total);
    if (percent != ctx->last_percent) {
        ctx->last_percent = percent;
        *ctx->out << "\r[2/7] 下载 ... " << percent << "% ("
                  << format_bytes(static_cast<double>(now)) << " / "
                  << format_bytes(static_cast<double>(total)) << ")";
        ctx->out->flush();
    }
    return 0;
}

DownloadOutcome download(const std::string& url, std::ostream& out, bool live,
                         const std::string& proxy, bool insecure) {
    DownloadOutcome result;
    auto            start = std::chrono::steady_clock::now();

    CURL* handle = curl_easy_init();
    if (!handle) {
        result.error = "curl_easy_init 失败";
        return result;
    }

    ProgressCtx ctx{&out, live, 0, -1, start};

    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "mihomo"); // 关键：拿 Clash YAML

    // 默认直连：libcurl 默认会读取 http(s)_proxy 环境变量，而本机代理
    // 往往正处在「待修复」状态，用坏代理去下载配置会直接 SSL connect error。
    if (proxy.empty()) {
        curl_easy_setopt(handle, CURLOPT_PROXY, "");
        curl_easy_setopt(handle, CURLOPT_NOPROXY, "*");
    } else {
        curl_easy_setopt(handle, CURLOPT_PROXY, proxy.c_str());
    }
    if (insecure) {
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &ctx);

    CURLcode code = curl_easy_perform(handle);
    if (live) out << "\r\033[K";

    if (code != CURLE_OK) {
        result.error = curl_easy_strerror(code);
    } else {
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &result.status);
        result.ok = result.status >= 200 && result.status < 300;
        if (!result.ok) result.error = "HTTP " + std::to_string(result.status);
    }

    curl_easy_cleanup(handle);
    result.seconds = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - start).count();
    return result;
}

bool make_temp(const std::string& dir, std::string_view content,
               std::string& path, std::string& error) {
    std::string tmpl = dir + "/.config.yaml.validate-XXXXXX";
    std::vector<char> name(tmpl.begin(), tmpl.end());
    name.push_back('\0');
    int fd = ::mkstemp(name.data());
    if (fd < 0) {
        error = "无法创建临时文件: " + std::string(std::strerror(errno));
        return false;
    }
    path = name.data();
    ::fchmod(fd, 0600);

    std::size_t off = 0;
    while (off < content.size()) {
        ssize_t n = ::write(fd, content.data() + off, content.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            error = "写临时文件失败: " + std::string(std::strerror(errno));
            ::close(fd);
            ::unlink(path.c_str());
            return false;
        }
        off += static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0) {
        error = "fsync 失败: " + std::string(std::strerror(errno));
        ::close(fd);
        ::unlink(path.c_str());
        return false;
    }
    ::close(fd);
    return true;
}

HttpResult load_config(Api& api, const std::string& path) {
    const std::string body = "{\"path\":" + json_quote(path) + ",\"force\":true}";
    return api.put("/configs?force=true", body);
}

std::string current_exit(Api& api) {
    auto response = api.get("/proxies");
    if (!response.ok()) return {};
    try {
        auto j = nlohmann::json::parse(response.body);
        const auto& proxies = j.at("proxies");
        const auto* group = static_cast<const nlohmann::json*>(nullptr);
        if (proxies.contains(kDefaultGroup)) group = &proxies.at(kDefaultGroup);
        if (!group) {
            for (auto it = proxies.begin(); it != proxies.end(); ++it) {
                if (it.value().value("type", "") == "Selector") {
                    group = &it.value();
                    break;
                }
            }
        }
        if (!group) return {};
        return group->value("now", std::string{});
    } catch (const nlohmann::json::exception&) {
        return {};
    }
}

} // namespace

int run_use(const UseOptions& options, std::ostream& out) {
    UseOptions opts = options;
    if (opts.config_path.empty()) opts.config_path = default_config_path();
    opts.config_path = expand_home(opts.config_path);

    const std::string dir = dir_name(opts.config_path);
    const bool        url_source = is_url(opts.source);

    //-- [1/7] 来源 ---------------------------------------------------------
    out << "[1/7] 来源: " << (url_source ? "URL" : "本地文件") << " " << opts.source << "\n";

    //-- [2/7] 下载 / 读取 ---------------------------------------------------
    std::string content;
    if (url_source) {
        const bool live = (&out == &std::cout) && ::isatty(STDOUT_FILENO);
        auto       dl   = download(opts.source, out, live, opts.proxy, opts.insecure);
        if (!dl.ok) {
            out << "[2/7] 下载 ... 失败: " << dl.error << "\n";
            return 3;
        }
        const double rate = dl.seconds > 0
                                ? static_cast<double>(dl.body.size()) / dl.seconds
                                : 0.0;
        out << "[2/7] 下载 ... HTTP " << dl.status << ", " << dl.body.size()
            << " bytes (" << format_rate(rate) << ")\n";
        content = std::move(dl.body);
    } else {
        auto read = read_file(opts.source);
        if (!read) {
            out << "[2/7] 读取 ... 失败: 无法读取 " << opts.source << "\n";
            return 3;
        }
        out << "[2/7] 读取 ... " << read->size() << " bytes\n";
        content = std::move(*read);
    }

    if (content.empty()) {
        out << "配置内容为空，已放弃（现有配置未改动）\n";
        return 2;
    }

    //-- [3/7] 格式识别 ------------------------------------------------------
    const auto detected = cfg::detect_format(content);
    if (detected.format == cfg::Format::Unknown) {
        out << "[3/7] 格式识别 ... 失败: " << detected.hint << "\n";
        out << "已放弃，现有配置未改动。\n";
        return 2;
    }
    if (detected.format == cfg::Format::Base64Nodes) {
        out << "[3/7] 格式识别 ... 失败: " << detected.hint << "\n";
        out << "提示：请求必须带 User-Agent: mihomo 才会返回 Clash YAML。\n";
        out << "已放弃，现有配置未改动。\n";
        return 2;
    }
    out << "[3/7] 格式识别 ... Clash YAML / " << detected.proxy_count << " 节点\n";

    auto lines = cfg::split_lines(content);

    // 有些机场在到期后会返回一份「占位配置」：节点全指向 127.0.0.1，
    // 名字写着「服务已到期 / 请续费」。这种配置装进去只会把现有配置换死。
    {
        const auto entries = cfg::scan_proxies(lines);
        int        loopback = 0;
        for (const auto& entry : entries) {
            const std::string& host = entry.server;
            if (starts_with(host, "127.") || host == "::1" || host == "localhost")
                ++loopback;
        }
        if (!entries.empty() && loopback == static_cast<int>(entries.size())) {
            out << "订阅返回的是占位节点（服务已到期或需要续费），未覆盖现有配置。\n";
            return 2;
        }
    }

    //-- [4/7] 兼容修正 ------------------------------------------------------
    // `lines` 已在格式检查阶段创建（见上）。

    // 默认「按需自动开启」：域名 server 在 UDP/53 被掐的本机会解析失败，
    // 非 safari 指纹完不成 REALITY 握手。两者都会表现为「全部超时」。
    bool compat = opts.compat;
    if (!compat && !opts.no_compat && cfg::config_needs_compat(lines)) {
        compat = true;
    }

    if (compat) {
        cfg::CompatReport report;

        // 节点域名专用解析器：AliDNS 优先（不同 DNS 会给出不同的 Anycast IP），
        // 并且多查几次收集候选，再按节点端口探测，避开「解析到了但端口不通」的 IP。
        std::vector<std::string> servers;
        if (!opts.dns_servers.empty()) {
            servers = opts.dns_servers;
        } else {
            servers = {"223.6.6.6", "223.5.5.5", "119.28.28.28", "114.114.114.114"};
        }

        std::unordered_map<std::string, std::string>              picked;
        std::unordered_map<std::string, std::vector<std::string>> candidates;

        auto resolver = [&](const std::string& host, int port) -> std::string {
            if (auto it = picked.find(host); it != picked.end()) return it->second;

            auto cit = candidates.find(host);
            if (cit == candidates.end()) {
                auto answers = resolve_a_all_tcp(host, servers, 700, 2);
                cit = candidates.emplace(host, std::move(answers.ips)).first;
            }
            const auto& ips = cit->second;
            if (ips.empty()) return {};

            for (const auto& ip : ips) {
                if (port <= 0 || tcp_reachable(ip, port, 800)) {
                    picked[host] = ip;
                    return ip;
                }
            }
            // 候选端口全不通：仍然钉第一个。节点可能只是暂时抖，
            // 留域名反而一定会因为 UDP/53 解析不了而直接失败。
            picked[host] = ips.front();
            return ips.front();
        };

        const bool dns_fixed = cfg::fix_dns_servers(lines);

        report = cfg::apply_compat(lines, resolver);
        report.dns_fixed = dns_fixed ? 1 : 0;

        out << "[4/7] 兼容修正 ... " << report.pinned << " 个域名钉 IP，"
            << report.fingerprints << " 个节点改 safari";
        if (report.dns_fixed) out << "，DNS 换成本机可用服务器";
        if (!opts.compat && compat) out << "（自动启用；--no-compat 可关闭）";
        if (report.unresolved > 0) out << "（" << report.unresolved << " 个未钉 IP）";
        out << "\n";
        if (!report.unresolved_hosts.empty()) {
            out << "      未钉 IP（端口不可达）: ";
            std::set<std::string> unique(report.unresolved_hosts.begin(),
                                         report.unresolved_hosts.end());
            std::size_t n = std::min<std::size_t>(unique.size(), 5);
            std::size_t i = 0;
            for (const auto& host : unique) {
                if (i++ >= n) break;
                if (i > 1) out << ", ";
                out << host;
            }
            if (unique.size() > n) out << " …";
            out << "\n";
        }
    } else {
        // 显式 --no-compat：不改配置，但必须把风险说清楚。
        int domain_servers = 0;
        for (const auto& entry : cfg::scan_proxies(lines))
            if (!entry.server.empty() && !is_ipv4(entry.server)) ++domain_servers;

        out << "[4/7] 兼容修正 ... 跳过"
            << (opts.no_compat ? "（--no-compat）" : "（配置已是 IP + safari）");
        if (domain_servers > 0) out << "；" << domain_servers << " 个节点 server 是域名";
        out << "\n";
        if (domain_servers > 0 || cfg::config_needs_compat(lines)) {
            out << "      注意：本机 UDP/53 解析节点域名会超时、且只有 safari 指纹能握手；"
                   "去掉 --no-compat 才会自动修正。\n";
        }
    }

    // 目录不存在就先建（0700），否则备份/落盘都无从谈起。
    if (!file_exists(dir)) {
        if (::mkdir(dir.c_str(), 0700) != 0) {
            out << "无法创建配置目录 " << dir << ": " << std::strerror(errno) << "\n";
            return 3;
        }
    }

    //-- [5/7] 备份 ----------------------------------------------------------
    std::string backup_path;
    bool        had_backup = false;
    const auto  original   = file_exists(opts.config_path)
                                 ? read_file(opts.config_path)
                                 : std::nullopt;

    if (original) {
        backup_path = opts.config_path + ".bak-" + timestamp_compact();
        for (int n = 1; file_exists(backup_path); ++n)
            backup_path = opts.config_path + ".bak-" + timestamp_compact() + "-" + std::to_string(n);

        std::string error;
        if (!write_file_atomic(backup_path, *original, error)) {
            out << "[5/7] 备份 ... 失败: " << error << "\n";
            return 3;
        }
        had_backup = true;
        out << "[5/7] 备份 ... " << backup_path << "\n";
    } else {
        out << "[5/7] 备份 ... 跳过（原配置不存在）\n";
    }

    const std::string new_content = cfg::join_lines(lines);

    //-- [6/7] 临时文件加载验证 ----------------------------------------------
    std::string temp_path;
    if (opts.offline) {
        out << "[6/7] 临时文件加载验证 ... 跳过（--offline）\n";
    } else if (!opts.restart) {
        // 验证本身就是「让内核加载一次」；既然要求不热重载，就不能偷偷加载。
        out << "[6/7] 临时文件加载验证 ... 跳过（--no-restart 只落盘）\n";
    } else {
        std::string error;
        if (!make_temp(dir, new_content, temp_path, error)) {
            out << "[6/7] 临时文件加载验证 ... 失败: " << error << "\n";
            return 3;
        }

        Api api(opts.api_base);
        auto response = load_config(api, temp_path);
        if (!response.reachable()) {
            ::unlink(temp_path.c_str());
            out << "[6/7] 临时文件加载验证 ... 失败: 内核 API 不可达（"
                << response.error << "）\n";
            out << "      内核没运行时无法验证配置；确认要直接落盘请加 --offline。\n";
            return 4;
        }
        if (!response.ok()) {
            ::unlink(temp_path.c_str());
            out << "[6/7] 临时文件加载验证 ... 失败: HTTP " << response.status << "\n";
            if (!response.body.empty()) out << "      " << response.body << "\n";
            out << "      现有配置未改动。\n";
            return 4;
        }
        out << "[6/7] 临时文件加载验证 ... PUT /configs {\"path\":\"" << temp_path
            << "\",\"force\":true} -> ok\n";
    }

    //-- [7/7] 落盘 + 热重载 --------------------------------------------------
    // 先把内容写到正式路径（原子 rename），再让内核重新读。
    std::string write_error;
    if (!write_file_atomic(opts.config_path, new_content, write_error)) {
        if (!temp_path.empty()) ::unlink(temp_path.c_str());
        out << "[7/7] 落盘 ... 失败: " << write_error << "\n";
        // 验证阶段内核已经加载过临时配置，这里必须把它拉回旧配置。
        if (had_backup) {
            if (auto backup = read_file(backup_path))
                write_file_atomic(opts.config_path, *backup, write_error);
        } else if (original) {
            write_file_atomic(opts.config_path, *original, write_error);
        }
        out << "      已恢复原配置文件（内核可能仍持有刚才验证过的配置，请重启 mihomo）。\n";
        return 3;
    }
    if (!temp_path.empty()) ::unlink(temp_path.c_str());

    if (!opts.restart) {
        out << "[7/7] 落盘 + 生效 ... 已落盘，未热重载（--no-restart）\n";
        return 0;
    }

    Api api(opts.api_base);
    auto reload = load_config(api, opts.config_path);
    if (reload.ok()) {
        std::string exit_node = current_exit(api);
        out << "[7/7] 落盘 + 生效 ... ok";
        if (!exit_node.empty()) out << "（当前出口 " << exit_node << "）";
        out << "\n";
        return 0;
    }

    // 热重载失败：把备份写回去，再让内核读回旧配置。
    if (had_backup) {
        auto backup = read_file(backup_path);
        if (backup) write_file_atomic(opts.config_path, *backup, write_error);
        load_config(api, opts.config_path);
    } else if (original) {
        write_file_atomic(opts.config_path, *original, write_error);
        load_config(api, opts.config_path);
    }

    out << "[7/7] 落盘 + 生效 ... 失败: HTTP " << reload.status;
    if (!reload.body.empty()) out << " " << reload.body;
    out << "\n      已回滚" << (had_backup ? "到备份" : "到原内容") << "。\n";
    return 5;
}

} // namespace mihomo
