//- app.cpp — 四宫格界面、线程编排、交互

#include "app.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <vector>

#include <dirent.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "common.hpp"
#include "logfmt.hpp"
#include "vaterm/term.hpp"
#include "vaterm/utf.hpp"

namespace mihomo {

using namespace vawk::widget;
using vaterm::InputData;
using vaterm::InputType;
using vaterm::KeyCode;
using vaterm::MouseAction;
using vaterm::MouseButton;

namespace {

volatile std::sig_atomic_t g_winch = 0;

extern "C" void on_sigwinch(int) { g_winch = 1; }

//-- 配色 --------------------------------------------------------------

struct Theme {
    vatui::Style label, value, dim, down, up, warn, error, ok;
    vatui::Style status, status_text, help_bg, accent, selected, head;
};

Theme make_theme() {
    auto fgc = [](int c) {
        return vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{static_cast<uint8_t>(c)}),
                            .bg_sgr = vatui::bg(vaterm::TRANS),
                            .effects_sgr = vatui::effects({})};
    };
    auto rgb = [](int r, int g, int b, bool bold = false) {
        return vatui::Style{
            .fg_sgr = vatui::fg(vaterm::Rgb{static_cast<uint8_t>(r),
                                            static_cast<uint8_t>(g),
                                            static_cast<uint8_t>(b)}),
            .bg_sgr = vatui::bg(vaterm::TRANS),
            .effects_sgr = vatui::effects(
                bold ? std::initializer_list<vaterm::TextEffect>{vaterm::TextEffect::BOLD}
                     : std::initializer_list<vaterm::TextEffect>{})};
    };

    Theme t;
    t.label   = rgb(150, 170, 205);
    t.value   = rgb(235, 242, 255);
    t.dim     = rgb(108, 122, 150);
    t.down    = rgb(90, 230, 255);
    t.up      = rgb(120, 255, 180);
    t.warn    = rgb(255, 215, 80);
    t.error   = rgb(255, 95, 120);
    t.ok      = rgb(120, 255, 160);
    t.accent  = rgb(255, 120, 220, true);
    t.head    = rgb(90, 240, 255, true);
    t.status = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{252}),
                            .bg_sgr = vatui::bg(vaterm::Color8{236}),
                            .effects_sgr = vatui::effects({})};
    t.status_text = rgb(255, 255, 255, true);
    t.help_bg = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{252}),
                             .bg_sgr = vatui::bg(vaterm::Color8{234}),
                             .effects_sgr = vatui::effects({})};
    t.selected = vatui::Style{.fg_sgr = vatui::fg(vaterm::Color8{231}),
                              .bg_sgr = vatui::bg(vaterm::Color8{54}),
                              .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    (void)fgc;
    return t;
}

const Theme& theme();

// 按显示宽度硬换行，保留原有换行符。
std::vector<std::string> wrap_text(std::string_view text, int width) {
    std::vector<std::string> out;
    if (width < 1) width = 1;

    std::string line;
    int         used = 0;
    for (std::size_t pos = 0; pos < text.size();) {
        if (text[pos] == '\n') {
            out.push_back(line);
            line.clear();
            used = 0;
            ++pos;
            continue;
        }
        std::size_t n = vaterm::utf::char_bytes(text.substr(pos));
        if (n == 0) { ++pos; continue; }
        if (pos + n > text.size()) break;

        int w = vaterm::utf::char_width(text.substr(pos));
        if (w < 1) w = 1;

        if (used + w > width) {
            out.push_back(line);
            line.clear();
            used = 0;
        }
        line.append(text.substr(pos, n));
        used += w;
        pos += n;
    }
    out.push_back(line);
    return out;
}

// 保留尾部（域名后缀更有信息量）的省略。
std::string elide_tail(std::string_view text, int width) {
    if (width <= 1 || vaterm::utf::width(text) <= width) return std::string(text);
    std::string_view view = text;
    while (!view.empty() && vaterm::utf::width(view) > width - 1) {
        std::size_t n = vaterm::utf::char_bytes(view);
        view.remove_prefix(n ? n : 1);
    }
    return "…" + std::string(view);
}

// 保留头部（日志摘要从开头读）。
std::string elide_head(std::string_view text, int width) {
    if (width <= 1 || vaterm::utf::width(text) <= width) return std::string(text);
    std::string out;
    int used = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t n = vaterm::utf::char_bytes(text.substr(pos));
        if (!n || pos + n > text.size()) break;
        int w = vaterm::utf::char_width(text.substr(pos));
        if (w < 1) w = 1;
        if (used + w > width - 1) break;
        out.append(text.substr(pos, n));
        used += w;
        pos += n;
    }
    return out + "…";
}

const vatui::Style& level_style(vawk::widget::LogLevel level) {
    const auto& t = theme();
    switch (level) {
    case vawk::widget::LogLevel::Error: return t.error;
    case vawk::widget::LogLevel::Warn:  return t.warn;
    case vawk::widget::LogLevel::Debug: return t.dim;
    case vawk::widget::LogLevel::Info:  break;
    }
    return t.value;
}

const char* level_name(vawk::widget::LogLevel level) {
    switch (level) {
    case vawk::widget::LogLevel::Error: return "ERR";
    case vawk::widget::LogLevel::Warn:  return "WRN";
    case vawk::widget::LogLevel::Debug: return "DBG";
    case vawk::widget::LogLevel::Info:  break;
    }
    return "INF";
}

const Theme& theme() {
    static Theme t = make_theme();
    return t;
}

//-- 小工具 ------------------------------------------------------------

int mode_index(const std::string& mode) {
    if (mode == "direct") return 0;
    if (mode == "global") return 2;
    return 1;
}

std::string mode_name(int index) {
    switch (index) {
    case 0: return "direct";
    case 2: return "global";
    default: return "rule";
    }
}

std::string mode_label(int index) {
    switch (index) {
    case 0: return "直连";
    case 2: return "全局";
    default: return "规则";
    }
}

// 从 /proc 里找 mihomo 主进程，用来算内核运行时长。
long long find_mihomo_pid() {
    DIR* dir = ::opendir("/proc");
    if (!dir) return -1;
    long long pid = -1;
    while (dirent* ent = ::readdir(dir)) {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
        std::string path = std::string("/proc/") + ent->d_name + "/comm";
        auto        comm = read_file(path);
        if (!comm) continue;
        std::string name = *comm;
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
            name.pop_back();
        if (name == "mihomo") {
            pid = std::strtoll(ent->d_name, nullptr, 10);
            break;
        }
    }
    ::closedir(dir);
    return pid;
}

std::vector<std::string> split_ws(std::string_view s) {
    std::vector<std::string> out;
    std::size_t              i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        std::size_t start = i;
        while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i > start) out.emplace_back(s.substr(start, i - start));
    }
    return out;
}

long long process_uptime(long long pid) {
    if (pid <= 0) return -1;

    auto uptime_file = read_file("/proc/uptime");
    auto stat_file   = read_file("/proc/" + std::to_string(pid) + "/stat");
    if (!uptime_file || !stat_file) return -1;

    double system_uptime = std::strtod(uptime_file->c_str(), nullptr);

    // comm 字段可能带空格，从最后一个 ')' 之后再切。
    auto rparen = stat_file->rfind(')');
    if (rparen == std::string::npos) return -1;
    auto fields = split_ws(std::string_view(*stat_file).substr(rparen + 1));
    // fields[0] 是 state（第 3 个字段），starttime 是第 22 个 → 下标 19。
    if (fields.size() < 20) return -1;
    double start_ticks = std::strtod(fields[19].c_str(), nullptr);
    long   hz          = ::sysconf(_SC_CLK_TCK);
    if (hz <= 0) return -1;

    double seconds = system_uptime - start_ticks / static_cast<double>(hz);
    return seconds > 0 ? static_cast<long long>(seconds) : 0;
}

// 速度面板里给 sparkline 留的左边距。
constexpr int kSpeedLabelW = 14;

} // namespace

// =========================================================================

App::App(AppOptions options) : opts_(std::move(options)), demo_(opts_.demo) {
    prepare_widgets();
}

App::~App() { stop_threads(); }

void App::prepare_widgets() {
    log_panel_.border_gradient       = Gradient{185, 235, 0.9, 1.0};
    node_panel_.border_gradient      = Gradient{275, 325, 0.9, 1.0};
    speed_panel_.border_gradient     = Gradient{140, 180, 0.9, 1.0};
    mode_panel_.border_gradient      = Gradient{25, 60, 0.9, 1.0};
    inspector_panel_.border_gradient = Gradient{300, 345, 0.9, 1.0};
    inspector_panel_.set_content(nullptr);

    // 选中行用高饱和纯色底（渐变底会被文字背景打断，反而花）。
    // 选中行：整行同一条靛蓝底色铺满（避免时间块出现直角边），
    // 时间只换文字颜色，不再换背景。
    log_view_.selected_style = vatui::Style{
        .fg_sgr = vatui::fg(vaterm::Rgb{238, 242, 255}),
        .bg_sgr = vatui::bg(vaterm::Rgb{46, 48, 92}),
        .effects_sgr = vatui::effects({})};
    log_view_.selected_time_style = vatui::Style{
        .fg_sgr = vatui::fg(vaterm::Rgb{110, 235, 255}),
        .bg_sgr = vatui::bg(vaterm::Rgb{46, 48, 92}),
        .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};

    node_list_.gradient_enabled = false;
    node_list_.selected_style = vatui::Style{
        .fg_sgr = vatui::fg(vaterm::Color8{231}),
        .bg_sgr = vatui::bg(vaterm::Rgb{72, 40, 125}),
        .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    node_list_.unfocused_selected_style = vatui::Style{
        .fg_sgr = vatui::fg(vaterm::Color8{252}),
        .bg_sgr = vatui::bg(vaterm::Rgb{45, 45, 62}),
        .effects_sgr = vatui::effects({})};

    mode_seg_.gradient_enabled = false;
    mode_seg_.selected_style = vatui::Style{
        .fg_sgr = vatui::fg(vaterm::Color8{231}),
        .bg_sgr = vatui::bg(vaterm::Rgb{200, 80, 30}),
        .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};
    mode_seg_.focus_style = vatui::Style{
        .fg_sgr = vatui::fg(vaterm::Color8{231}),
        .bg_sgr = vatui::bg(vaterm::Rgb{240, 120, 40}),
        .effects_sgr = vatui::effects({vaterm::TextEffect::BOLD})};

    log_panel_.set_content(&log_view_);
    node_panel_.set_content(&node_list_);
    mode_panel_.set_content(&mode_seg_);

    // 网速面板不放可聚焦控件，标题直接写死。
    speed_panel_.set_content(nullptr);

    node_list_.on_confirm = [this](const ListItem& item) { switch_node(item.id); };
    mode_seg_.on_change  = [this](int index) { set_mode(mode_name(index)); };

    focus_.add(&log_panel_);
    focus_.add(&node_panel_);

    log_view_.set_follow(true);
}

//-- 演示数据 -----------------------------------------------------------

void App::prepare_demo() {
    {
        std::lock_guard lock(stats_mutex_);
        stats_ = Stats{};
        stats_.api_ok     = true;
        stats_.version    = "v1.19.34";
        stats_.uptime     = 9 * 60;
        stats_.mode       = "rule";
        stats_.groups     = {"🔰 节点选择", "♻️ 自动选择"};
        stats_.group      = stats_.groups.front();
        stats_.now        = "🇸🇬 新加坡01";
        stats_.connections = 11;
        stats_.up_total    = 88LL * 1024 * 1024;
        stats_.down_total  = 17LL * 1024 * 1024;
        stats_.nodes = {
            {"🇸🇬 新加坡01", "vless", 921, true},
            {"🇯🇵 日本01", "vless", 1004, false},
            {"🇭🇰 香港01", "vless", 168, false},
            {"🇺🇸 美国02", "vless", 1350, false},
            {"🇩🇪 德国01", "vless", -1, false},
            {"♻️ 自动选择", "urltest", 921, false},
            {"DIRECT", "direct", 0, false},
        };
        stats_.top_connections = {
            {"www.youtube.com", "🇸🇬 新加坡01[直连]", "DomainSuffix", 1843200, 92160},
            {"api.github.com", "🇯🇵 日本01[直连]", "GeoSite", 524288, 40960},
            {"registry.npmjs.org", "🇭🇰 香港01[直连]", "DomainSuffix", 262144, 8192},
            {"cdn.jsdelivr.net", "DIRECT", "DomainSuffix", 131072, 4096},
        };
    }

    log_view_.clear();
    const char* demo_logs[] = {
        "time=\"2026-11-14T16:54:28.101+08:00\" level=info msg=\"[TCP] 142.250.72.14:52344 --> www.youtube.com:443 match DomainSuffix(google) using 🇸🇬 新加坡01[直连]\"",
        "time=\"2026-11-14T16:54:29.204+08:00\" level=info msg=\"[TCP] 140.82.112.4:52345 --> api.github.com:443 match GeoSite(github) using 🇯🇵 日本01[直连]\"",
        "time=\"2026-11-14T16:54:29.512+08:00\" level=debug msg=\"[DNS] cache hit: cdn.jsdelivr.net\"",
        "time=\"2026-11-14T16:54:30.733+08:00\" level=warning msg=\"[TCP] dial 日本01: connection timeout, retry in 200ms\"",
        "time=\"2026-11-14T16:54:31.001+08:00\" level=info msg=\"[TCP] 142.250.72.14:52346 --> yt3.ggpht.com:443 match DomainSuffix(google) using 🇸🇬 新加坡01[直连]\"",
        "time=\"2026-11-14T16:54:32.300+08:00\" level=error msg=\"rule-set load failed: geosite-cn: no such file\"",
        "time=\"2026-11-14T16:54:32.455+08:00\" level=info msg=\"[UDP] 8.8.8.8:53 --> 8.8.8.8:53 match GeoIP(private) using DIRECT\"",
        "time=\"2026-11-14T16:54:33.120+08:00\" level=info msg=\"[TCP] 104.16.24.34:52347 --> registry.npmjs.org:443 match DomainSuffix(npmjs) using 🇭🇰 香港01[直连]\"",
    };
    for (const char* raw : demo_logs)
        log_view_.push(format_log_line(classify_log_line(raw), raw));

    {
        std::lock_guard lock(traffic_mutex_);
        up_history_.clear();
        down_history_.clear();
        last_traffic_ = TrafficSample{};
        const double down_kb[] = {120, 340, 980, 1220, 760, 410, 1500};
        const double up_kb[]   = {30, 44, 81, 68, 39, 22, 84};
        for (int i = 0; i < 7; ++i) {
            down_history_.push_back(down_kb[i] * 1024);
            up_history_.push_back(up_kb[i] * 1024);
        }
        last_traffic_.down = static_cast<long long>(1500 * 1024);
        last_traffic_.up   = static_cast<long long>(84 * 1024);
        last_traffic_.down_total = 17LL * 1024 * 1024;
        last_traffic_.up_total   = 88LL * 1024 * 1024;
        last_traffic_.valid      = true;
    }

    // 固定时间戳，保证 --snapshot 输出可复现。
    LogLine demo_note;
    demo_note.level = LogLevel::Info;
    demo_note.raw   = "演示模式：数据全部是假的，按 q 退出";
    demo_note.text  = "22:00:00 " + demo_note.raw;
    demo_note.spans = {{"22:00:00 ", LogTone::Time},
                       {"▸ ", LogTone::Accent},
                       {demo_note.raw, LogTone::Default}};
    log_view_.push(std::move(demo_note));
}

void App::demo_tick() {
    using namespace std::chrono;
    auto now = steady_clock::now();
    if (demo_since_.time_since_epoch().count() == 0) demo_since_ = now;

    static auto last_traffic_tick = now;
    static int  log_seq = 0;
    if (now - last_traffic_tick < seconds(1)) return;
    last_traffic_tick = now;

    const double t = duration<double>(now - demo_since_).count();
    const double down = (900 + 600 * std::sin(t * 1.3) + 300 * std::sin(t * 3.1)) * 1024;
    const double up   = (60 + 40 * std::sin(t * 2.0 + 1.0)) * 1024;

    TrafficSample s;
    {
        std::lock_guard lock(traffic_mutex_);
        s = last_traffic_;
        s.down = static_cast<long long>(std::max(0.0, down));
        s.up   = static_cast<long long>(std::max(0.0, up));
        s.up_total   += s.up;
        s.down_total += s.down;
        s.valid = true;
        last_traffic_ = s;
        up_history_.push_back(static_cast<double>(s.up));
        down_history_.push_back(static_cast<double>(s.down));
        while (up_history_.size() > 60) up_history_.pop_front();
        while (down_history_.size() > 60) down_history_.pop_front();
    }
    {
        std::lock_guard lock(stats_mutex_);
        stats_.up_total   = s.up_total;
        stats_.down_total = s.down_total;
    }

    if (++log_seq % 3 == 0) {
        static const char* fake[] = {
            "[TCP] www.google.com:443 --> 日本01",
            "[TCP] 104.18.32.7:443 --> 新加坡01",
            "[UDP] 1.1.1.1:53 --> DIRECT",
            "level=warn msg=\"dial timeout, fallback\"",
        };
        log_view_.push(vawk::widget::LogLevel::Info,
                       now_hms() + " " + fake[log_seq / 3 % 4]);
    }
    dirty_ = true;
}

//-- 后台线程 -----------------------------------------------------------

void App::start_threads() {
    stop_.store(false);

    journal_.start([this](vawk::widget::LogLevel level, std::string line) {
        on_log_line(level, std::move(line));
    });

    traffic_.start(opts_.api_base, [this](const TrafficSample& sample) {
        on_traffic(sample);
    });

    stats_thread_ = std::thread(&App::stats_loop, this);
    delay_thread_ = std::thread(&App::delay_loop, this);
}

void App::stop_threads() {
    stop_.store(true);
    delay_cancel_.store(true);

    journal_.stop();
    traffic_.stop();

    {
        std::lock_guard lock(delay_mutex_);
        delay_quit_ = true;
    }
    delay_cv_.notify_all();

    if (stats_thread_.joinable()) stats_thread_.join();
    if (delay_thread_.joinable()) delay_thread_.join();
    stop_.store(false);
}

void App::on_log_line(vawk::widget::LogLevel level, std::string line) {
    // 入库前先压成「时间 协议 目标 → 出口」，颜色跟着语义走。
    log_view_.push(format_log_line(level, std::move(line)));
    dirty_ = true;
}

void App::on_traffic(const TrafficSample& sample) {
    {
        std::lock_guard lock(traffic_mutex_);
        last_traffic_ = sample;
        up_history_.push_back(static_cast<double>(sample.up));
        down_history_.push_back(static_cast<double>(sample.down));
        while (up_history_.size() > 60) up_history_.pop_front();
        while (down_history_.size() > 60) down_history_.pop_front();
    }
    dirty_ = true;
}

void App::stats_loop() {
    Api api(opts_.api_base);
    api.set_timeout_ms(1200);

    Stats     local;
    int       tick = 0;

    while (!stop_.load()) {
        poll_stats(api, local, tick % 2 == 0);

        {
            std::lock_guard lock(stats_mutex_);
            stats_ = local;
        }
        dirty_ = true;

        ++tick;
        for (int i = 0; i < 10 && !stop_.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void App::poll_stats(Api& api, Stats& local, bool fetch_proxies) {
    auto version = api.get("/version");
    if (!version.ok()) {
        local.api_ok = false;
        local.error  = version.reachable()
                           ? "HTTP " + std::to_string(version.status)
                           : version.error;
        return;
    }
    local.api_ok = true;
    local.error.clear();

    try {
        auto j = nlohmann::json::parse(version.body);
        local.version = j.value("version", std::string{});
    } catch (const nlohmann::json::exception&) {
    }

    if (long long pid = find_mihomo_pid(); pid > 0)
        local.uptime = process_uptime(pid);

    if (auto cfg = api.get("/configs"); cfg.ok()) {
        try {
            auto j = nlohmann::json::parse(cfg.body);
            local.mode = j.value("mode", local.mode);
        } catch (const nlohmann::json::exception&) {
        }
    }

    if (auto conn = api.get("/connections"); conn.ok()) {
        try {
            auto j = nlohmann::json::parse(conn.body);
            local.down_total  = j.value("downloadTotal", 0LL);
            local.up_total    = j.value("uploadTotal", 0LL);
            local.connections = j.contains("connections")
                                    ? static_cast<long long>(j["connections"].size())
                                    : 0LL;

            std::vector<ConnInfo> top;
            if (j.contains("connections") && j["connections"].is_array()) {
                for (const auto& item : j["connections"]) {
                    ConnInfo info;
                    if (item.contains("metadata")) {
                        const auto& md = item["metadata"];
                        info.host = md.value("host", std::string{});
                        if (info.host.empty())
                            info.host = md.value("destinationIP", std::string{});
                    }
                    info.down = item.value("download", 0LL);
                    info.up   = item.value("upload", 0LL);
                    info.rule = item.value("rule", std::string{});
                    if (item.contains("chains") && item["chains"].is_array()
                        && !item["chains"].empty()
                        && item["chains"].front().is_string())
                        info.chain = item["chains"].front().get<std::string>();
                    top.push_back(std::move(info));
                }
            }
            std::sort(top.begin(), top.end(), [](const ConnInfo& a, const ConnInfo& b) {
                return (a.down + a.up) > (b.down + b.up);
            });
            if (top.size() > 8) top.resize(8);
            local.top_connections = std::move(top);
        } catch (const nlohmann::json::exception&) {
        }
    }

    if (!fetch_proxies) return;

    auto proxies_response = api.get("/proxies");
    if (!proxies_response.ok()) return;

    try {
        auto j = nlohmann::json::parse(proxies_response.body);
        const auto& proxies = j.at("proxies");

        struct GroupRef {
            std::string name;
            std::string type;
        };
        std::vector<GroupRef> group_refs;
        for (auto it = proxies.begin(); it != proxies.end(); ++it) {
            const std::string type = it.value().value("type", "");
            if ((type == "Selector" || type == "URLTest")
                && it.key() != "GLOBAL") {
                group_refs.push_back({it.key(), type});
            }
        }
        // 默认分组排最前；不同机场的主选择组名字不一样，这里给常见名字和
        // Selector 类型更高优先级，避免默认落到 URLTest 上（不能手动切换）。
        auto score = [](const GroupRef& g) {
            if (g.name == kDefaultGroup) return 0;
            if (g.name == "🚀 节点选择") return 1;
            if (g.type == "Selector") return 2;
            return 3;
        };
        std::stable_sort(group_refs.begin(), group_refs.end(),
                         [&](const GroupRef& a, const GroupRef& b) {
                             return score(a) < score(b);
                         });
        std::vector<std::string> groups;
        groups.reserve(group_refs.size());
        for (const auto& g : group_refs) groups.push_back(g.name);
        local.groups = groups;

        if (local.group.empty()
            || std::find(groups.begin(), groups.end(), local.group) == groups.end()) {
            local.group = groups.empty() ? std::string{} : groups.front();
        }

        auto group_it = proxies.find(local.group);
        if (group_it == proxies.end()) return;

        local.now = group_it->value("now", std::string{});

        std::vector<NodeInfo> nodes;
        if (group_it->contains("all")) {
            for (const auto& name : (*group_it)["all"]) {
                if (!name.is_string()) continue;
                NodeInfo node;
                node.name = name.get<std::string>();
                auto pit  = proxies.find(node.name);
                if (pit != proxies.end()) {
                    node.type = pit->value("type", "");
                    if (pit->contains("history") && (*pit)["history"].is_array()
                        && !(*pit)["history"].empty()) {
                        const auto& last = (*pit)["history"].back();
                        node.delay = last.value("delay", -1);
                    }
                }
                node.current = (node.name == local.now);
                nodes.push_back(std::move(node));
            }
        }
        local.nodes = std::move(nodes);
    } catch (const nlohmann::json::exception&) {
    }
}

int App::query_delay(Api& api, const std::string& name) {
    const std::string path = "/proxies/" + url_encode(name)
                           + "/delay?timeout=5000&url=" + url_encode(kDelayTestUrl);

    api.set_timeout_ms(7000);
    auto response = api.get(path);
    if (!response.ok()) return -2;
    try {
        int delay = nlohmann::json::parse(response.body).value("delay", -2);
        return delay > 0 ? delay : -2;
    } catch (const nlohmann::json::exception&) {
        return -2;
    }
}

void App::delay_loop() {
    while (true) {
        std::string name;
        bool        do_all = false;

        {
            std::unique_lock lock(delay_mutex_);
            delay_cv_.wait(lock, [this] {
                return delay_quit_ || !delay_queue_.empty() || delay_all_;
            });
            if (delay_quit_ && delay_queue_.empty() && !delay_all_) break;

            if (delay_all_) {
                delay_all_ = false;
                do_all     = true;
                name       = all_group_;
            } else {
                name = std::move(delay_queue_.front());
                delay_queue_.pop_front();
            }
        }

        if (do_all) {
            run_global_test(name, all_epoch_);
            continue;
        }

        // 单节点：先标成「测速中」，行内立刻能看到 …
        {
            std::lock_guard lock(delays_mutex_);
            delays_[name] = -3;
        }
        dirty_ = true;

        Api api(opts_.api_base);
        api.set_abort(&delay_cancel_);
        const int delay = query_delay(api, name);
        if (delay_cancel_.load() || stop_.load()) break;

        {
            std::lock_guard lock(delays_mutex_);
            delays_[name] = delay;
        }
        status(name + "  " + format_delay(delay) + "（只测速，不切换）");
        dirty_ = true;
    }
}

void App::test_all_delays() {
    if (demo_) {
        status("全局测速完成（演示数据）");
        dirty_ = true;
        return;
    }
    if (testing_.load()) {
        progress("全局测速进行中…");
        return;
    }

    Stats s;
    {
        std::lock_guard lock(stats_mutex_);
        s = stats_;
    }
    if (!s.api_ok) {
        status("内核未运行，无法测速");
        return;
    }
    if (s.group.empty()) {
        status("当前没有可测速的分组");
        return;
    }

    {
        std::lock_guard lock(delay_mutex_);
        delay_all_ = true;
        all_group_ = s.group;
        all_epoch_ = select_epoch_.load();
    }
    delay_cv_.notify_one();
    status("开始全局测速…");
    progress("全局测速准备中…");
    dirty_ = true;
}

void App::run_global_test(const std::string& group, std::uint64_t epoch) {
    if (testing_.exchange(true)) return;

    struct Guard {
        std::atomic<bool>& flag;
        ~Guard() { flag.store(false); }
    } guard{testing_};

    Api api(opts_.api_base);
    api.set_timeout_ms(7000);
    api.set_abort(&delay_cancel_);

    auto response = api.get("/proxies");
    if (!response.ok()) {
        status("全局测速失败：内核 API 不可达");
        return;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(response.body);
    } catch (const nlohmann::json::exception&) {
        status("全局测速失败：/proxies 返回的不是 JSON");
        return;
    }

    const auto& proxies = root.at("proxies");
    auto        group_it = proxies.find(group);
    if (group_it == proxies.end()) {
        status("全局测速失败：找不到分组 " + group);
        return;
    }

    const std::string previous = group_it->value("now", std::string{});
    const std::string group_type = group_it->value("type", std::string{});

    // 递归展开分组，只测叶子节点。分组行本身绝不调 /delay ——
    // 那正是会触发 URLTest 重新选最快的入口。
    std::vector<std::string>      leaves;
    std::set<std::string>         seen;
    std::function<void(const std::string&)> walk = [&](const std::string& name) {
        if (!seen.insert(name).second) return;
        auto it = proxies.find(name);
        if (it == proxies.end()) return;

        const std::string type = it->value("type", "");
        // 任何带 all 的代理都是分组（Selector / URLTest / Fallback / LoadBalance
        // / Relay …）。分组本身绝不测，避免内核重跑整组后改选。
        if (it->contains("all") || type == "Selector" || type == "URLTest"
            || type == "Fallback" || type == "LoadBalance" || type == "Relay") {
            if (it->contains("all"))
                for (const auto& member : (*it)["all"])
                    if (member.is_string()) walk(member.get<std::string>());
            return;
        }
        if (type == "Direct" || type == "Reject" || type == "RejectDrop"
            || type == "Pass" || type == "Compatible" || type == "DNS")
            return;
        // 注意：/proxies 不返回 server 字段，不能靠它判断可测性。
        leaves.push_back(name);
    };

    if (group_it->contains("all"))
        for (const auto& member : (*group_it)["all"])
            if (member.is_string()) walk(member.get<std::string>());

    if (leaves.empty()) {
        status("这个分组里没有可测速的节点");
        return;
    }

    const int total = static_cast<int>(leaves.size());
    {
        std::lock_guard lock(delays_mutex_);
        for (const auto& name : leaves) delays_[name] = -3;
    }
    dirty_ = true;
    progress("全局测速 0/" + std::to_string(total));

    // 8 路并发：60 个节点也不会串成 5 分钟。
    std::atomic<std::size_t> next{0};
    std::atomic<int>         done{0};
    std::atomic<int>         failed{0};

    const int workers = std::min(8, total);
    auto      work    = [&] {
        Api local(opts_.api_base);
        local.set_timeout_ms(7000);
        local.set_abort(&delay_cancel_);

        while (!stop_.load() && !delay_cancel_.load()) {
            std::size_t index = next.fetch_add(1);
            if (index >= leaves.size()) break;

            int delay = query_delay(local, leaves[index]);
            if (delay < 0 && !delay_cancel_.load()) {
                // 机场对连续测速有节流，失败节点补测一次再判死刑。
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                delay = query_delay(local, leaves[index]);
            }
            if (delay_cancel_.load()) break;

            {
                std::lock_guard lock(delays_mutex_);
                delays_[leaves[index]] = delay;
            }
            if (delay < 0) ++failed;

            const int finished = ++done;
            if (finished == total || finished % 2 == 0)
                progress("全局测速 " + std::to_string(finished) + "/"
                         + std::to_string(total));
            dirty_ = true;
        }
    };

    std::vector<std::thread> pool;
    pool.reserve(static_cast<std::size_t>(workers));
    for (int i = 0; i < workers; ++i) pool.emplace_back(work);
    for (auto& thread : pool) thread.join();

    if (stop_.load() || delay_cancel_.load()) {
        status("全局测速已取消");
        return;
    }

    // 测完检查是否被自动切走：只回切 Selector（URLTest 的选择是它自己的事）。
    std::string now;
    auto single = api.get("/proxies/" + url_encode(group));
    if (single.ok()) {
        try {
            now = nlohmann::json::parse(single.body).value("now", std::string{});
        } catch (const nlohmann::json::exception&) {
        }
    }

    std::string summary = "全局测速完成：" + std::to_string(total) + " 个节点"
                        + (failed.load() ? "，" + std::to_string(failed.load()) + " 个失败"
                                         : std::string{});
    if (failed.load() == total)
        summary += "；全部超时，通常是节点域名没钉 IP 或指纹不是 safari，用 --use 重装（会自动修正）";

    if (now.empty() || now == previous || previous.empty()) {
        status(summary);
        return;
    }

    if (group_type != "Selector") {
        status(summary + "（分组自动选择已更新）");
        return;
    }

    if (select_epoch_.load() != epoch) {
        status(summary + "（检测到手动切换，未回切）");
        return;
    }

    auto restore = api.put("/proxies/" + url_encode(group),
                           "{\"name\":" + json_quote(previous) + "}");
    if (restore.ok()) {
        {
            std::lock_guard lock(stats_mutex_);
            stats_.now = previous;
            for (auto& node : stats_.nodes) node.current = (node.name == previous);
        }
        last_names_sig_.clear();
        status(summary + "；检测到自动切换，已切回 " + previous);
    } else {
        status(summary + "；切回 " + previous + " 失败");
    }
    dirty_ = true;
}

//-- 数据 → 控件 --------------------------------------------------------

bool App::api_online() const {
    std::lock_guard lock(stats_mutex_);
    return stats_.api_ok;
}

void App::apply_stats_to_widgets() {
    Stats s;
    {
        std::lock_guard lock(stats_mutex_);
        s = stats_;
    }

    std::string names_sig = s.group + "|";
    for (const auto& node : s.nodes) names_sig += node.name + ",";

    if (names_sig != last_names_sig_) {
        std::string keep;
        if (auto* item = node_list_.selected_item()) keep = item->id;

        std::vector<ListItem> items;
        items.reserve(s.nodes.size());

        std::unordered_map<std::string, int> overrides;
        {
            std::lock_guard lock(delays_mutex_);
            overrides = delays_;
        }

        for (const auto& node : s.nodes) {
            ListItem item;
            item.id    = node.name;
            item.label = node.name;

            int delay = node.delay;
            if (auto it = overrides.find(node.name); it != overrides.end())
                delay = it->second;

            item.detail = node.current ? "● " : "  ";
            item.detail += (node.type == "direct" || node.type == "reject")
                               ? "—"
                               : format_delay(delay);
            items.push_back(std::move(item));
        }

        node_list_.set_items(std::move(items));
        if (!keep.empty()) node_list_.select_id(keep);
        last_names_sig_ = names_sig;
    } else {
        std::unordered_map<std::string, int> overrides;
        {
            std::lock_guard lock(delays_mutex_);
            overrides = delays_;
        }
        for (const auto& node : s.nodes) {
            int delay = node.delay;
            if (auto it = overrides.find(node.name); it != overrides.end())
                delay = it->second;

            std::string detail = node.current ? "● " : "  ";
            detail += (node.type == "direct" || node.type == "reject")
                          ? "—"
                          : format_delay(delay);
            node_list_.set_detail(node.name, std::move(detail));
        }
    }

    mode_seg_.set_selected(mode_index(s.mode), false);
    log_panel_.set_badge(log_view_.following() ? "follow" : "PAUSED");
    node_list_.set_empty_text(s.api_ok ? "（没有节点）" : "（内核未运行）");

    std::string title = "节点";
    if (!s.group.empty()) title += " · " + s.group;
    if (s.groups.size() > 1) title += " ▾";
    if (!s.api_ok) title += "（离线）";
    node_panel_.set_title(std::move(title));
}

//-- 交互 ---------------------------------------------------------------

void App::status(std::string message) {
    // 操作反馈不再长期占用底栏，而是作为一条日志跟着滚屏。
    LogLine line;
    const bool bad = message.find("失败") != std::string::npos
                  || message.find("错误") != std::string::npos
                  || message.find("不可达") != std::string::npos;
    line.level = bad ? LogLevel::Warn : LogLevel::Info;
    line.raw   = message;
    const std::string time = now_hms();
    line.text  = time + " " + message;
    line.spans = {{time + " ", LogTone::Time},
                  {"▸ ", LogTone::Accent},
                  {message, LogTone::Default}};
    log_view_.push(std::move(line));
    dirty_ = true;
}

void App::progress(std::string message) {
    std::lock_guard lock(status_mutex_);
    hint_text_  = std::move(message);
    hint_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
}

void App::begin_filter() {
    auto* current = focus_.current();
    if (current == &log_panel_) {
        filtering_    = true;
        filter_panel_ = "log";
        filter_buffer_ = log_view_.filter();
    } else if (current == &node_panel_) {
        filtering_    = true;
        filter_panel_ = "node";
        filter_buffer_ = node_list_.filter();
    } else {
        status("先 Tab 到「日志」或「节点」再按 /");
    }
}

void App::end_filter(bool apply) {
    if (apply) {
        if (filter_panel_ == "log") {
            log_view_.set_filter(filter_buffer_);
            status(filter_buffer_.empty() ? "已清除日志过滤"
                                          : "日志过滤: " + filter_buffer_);
        } else if (filter_panel_ == "node") {
            node_list_.set_filter(filter_buffer_);
            status(filter_buffer_.empty() ? "已清除节点过滤"
                                          : "节点过滤: " + filter_buffer_);
        }
    }
    filtering_ = false;
    filter_buffer_.clear();
    dirty_    = true;
}

void App::switch_node(const std::string& name) {
    Stats s;
    {
        std::lock_guard lock(stats_mutex_);
        s = stats_;
    }

    if (demo_) {
        std::lock_guard lock(stats_mutex_);
        stats_.now = name;
        for (auto& node : stats_.nodes) node.current = (node.name == name);
        last_names_sig_.clear();
        select_epoch_.fetch_add(1);
        status("已切换（演示）: " + name);
        dirty_ = true;
        return;
    }

    if (!s.api_ok) {
        status("内核未运行，无法切换");
        return;
    }
    if (s.group.empty()) {
        status("当前分组为空");
        return;
    }

    Api api(opts_.api_base);
    auto response = api.put("/proxies/" + url_encode(s.group),
                            "{\"name\":" + json_quote(name) + "}");
    if (response.ok()) {
        select_epoch_.fetch_add(1);
        {
            std::lock_guard lock(stats_mutex_);
            stats_.now = name;
            for (auto& node : stats_.nodes) node.current = (node.name == name);
        }
        last_names_sig_.clear();
        status("出口已切到 " + name);
    } else {
        status("切换失败: " + (response.reachable()
                                  ? "HTTP " + std::to_string(response.status)
                                  : response.error));
    }
    dirty_ = true;
}

void App::choose_group(int index) {
    Stats s;
    {
        std::lock_guard lock(stats_mutex_);
        s = stats_;
    }
    if (index < 0 || index >= static_cast<int>(s.groups.size())) return;

    if (demo_) {
        // 演示数据只有一套：切组时保留节点，只换分组名，避免列表突然空掉。
        std::lock_guard lock(stats_mutex_);
        stats_.group = s.groups[static_cast<std::size_t>(index)];
        last_names_sig_.clear();
        status("分组: " + stats_.group + "（演示）");
        dirty_ = true;
        return;
    }

    {
        std::lock_guard lock(stats_mutex_);
        stats_.group = s.groups[static_cast<std::size_t>(index)];
        stats_.nodes.clear();
    }
    select_epoch_.fetch_add(1);
    last_names_sig_.clear();
    node_list_.set_filter("");
    status("分组: " + s.groups[static_cast<std::size_t>(index)]);
    dirty_ = true;
}

void App::cycle_group(int delta) {
    Stats s;
    {
        std::lock_guard lock(stats_mutex_);
        s = stats_;
    }
    if (s.groups.size() < 2) {
        status("只有一个分组");
        return;
    }
    int index = 0;
    for (std::size_t i = 0; i < s.groups.size(); ++i)
        if (s.groups[i] == s.group) index = static_cast<int>(i);

    index = (index + delta + static_cast<int>(s.groups.size()))
            % static_cast<int>(s.groups.size());
    choose_group(index);
}

void App::open_dropdown() {
    Stats s;
    {
        std::lock_guard lock(stats_mutex_);
        s = stats_;
    }
    if (s.groups.empty()) {
        status(s.api_ok ? "没有可切换的分组" : "内核未运行");
        return;
    }
    dropdown_     = true;
    dropdown_sel_ = 0;
    for (std::size_t i = 0; i < s.groups.size(); ++i)
        if (s.groups[i] == s.group) dropdown_sel_ = static_cast<int>(i);
    dirty_ = true;
}

void App::close_dropdown(bool apply) {
    if (apply) {
        std::string group;
        {
            std::lock_guard lock(stats_mutex_);
            if (dropdown_sel_ >= 0
                && dropdown_sel_ < static_cast<int>(stats_.groups.size()))
                group = stats_.groups[static_cast<std::size_t>(dropdown_sel_)];
        }
        if (!group.empty()) choose_group(dropdown_sel_);
    }
    dropdown_ = false;
    dirty_    = true;
}

bool App::handle_dropdown(const vaterm::InputData& event) {
    if (event.inputType == InputType::KEYBD) {
        const auto& key = event.value.keybd;
        int count = 0;
        {
            std::lock_guard lock(stats_mutex_);
            count = static_cast<int>(stats_.groups.size());
        }
        if (key.code == KeyCode::ESC) { close_dropdown(false); return true; }
        if (key.code == KeyCode::ENTER) { close_dropdown(true); return true; }
        if (key.code == KeyCode::UP && count > 0) {
            dropdown_sel_ = (dropdown_sel_ - 1 + count) % count;
            dirty_ = true;
            return true;
        }
        if (key.code == KeyCode::DOWN && count > 0) {
            dropdown_sel_ = (dropdown_sel_ + 1) % count;
            dirty_ = true;
            return true;
        }
        if (key.code == KeyCode::HOME) { dropdown_sel_ = 0; dirty_ = true; return true; }
        if (key.code == KeyCode::END) {
            dropdown_sel_ = std::max(0, count - 1);
            dirty_ = true;
            return true;
        }
        return true; // 下拉打开时吞掉其它按键
    }

    if (event.inputType == InputType::MOUSE) {
        const auto& mouse = event.value.mouse;
        if (mouse.action == MouseAction::PRESS && mouse.button == MouseButton::LEFT) {
            if (!dropdown_rect_.contains(mouse.col, mouse.row)) {
                close_dropdown(false);
                return true;
            }
            int index = mouse.row - dropdown_rect_.y - 1;
            int count = 0;
            {
                std::lock_guard lock(stats_mutex_);
                count = static_cast<int>(stats_.groups.size());
            }
            if (index >= 0 && index < count) {
                dropdown_sel_ = index;
                close_dropdown(true);
            }
            return true;
        }
        if (mouse.action == MouseAction::SCROLL_UP) {
            dropdown_sel_ = std::max(0, dropdown_sel_ - 1);
            dirty_ = true;
            return true;
        }
        if (mouse.action == MouseAction::SCROLL_DOWN) {
            std::lock_guard lock(stats_mutex_);
            dropdown_sel_ = std::min(static_cast<int>(stats_.groups.size()) - 1,
                                     dropdown_sel_ + 1);
            if (dropdown_sel_ < 0) dropdown_sel_ = 0;
            dirty_ = true;
            return true;
        }
    }
    return true;
}

void App::set_mode(std::string mode) {
    int index = mode_index(mode);
    mode_seg_.set_selected(index, false);

    if (demo_) {
        std::lock_guard lock(stats_mutex_);
        stats_.mode = mode;
        status("模式: " + mode_label(index) + "（演示）");
        dirty_ = true;
        return;
    }

    Api api(opts_.api_base);
    auto response = api.patch("/configs",
                              "{\"mode\":" + json_quote(mode) + "}");
    if (response.ok()) {
        {
            std::lock_guard lock(stats_mutex_);
            stats_.mode = mode;
        }
        status("模式: " + mode_label(index));
    } else {
        status("切换模式失败: " + (response.reachable()
                                       ? "HTTP " + std::to_string(response.status)
                                       : response.error));
    }
    dirty_ = true;
}

void App::test_delay(const std::string& name) {
    if (demo_) {
        status("测速: " + name + " 921ms（演示，不切换）");
        dirty_ = true;
        return;
    }

    // 分组行不能直接测：/delay 会让内核重跑整组并改选最快节点。
    {
        Stats s;
        {
            std::lock_guard lock(stats_mutex_);
            s = stats_;
        }
        if (std::find(s.groups.begin(), s.groups.end(), name) != s.groups.end()) {
            status("分组请用 t 全局测速（只测叶子节点）");
            return;
        }
        for (const auto& node : s.nodes) {
            if (node.name != name) continue;
            if (node.type == "Selector" || node.type == "URLTest") {
                status("分组请用 t 全局测速（只测叶子节点）");
                return;
            }
            break;
        }
    }

    {
        std::lock_guard lock(delay_mutex_);
        delay_queue_.push_back(name);
    }
    delay_cv_.notify_one();
    progress("测速中: " + name + " …（只测速，不切换）");
    dirty_ = true;
}

bool App::handle_global_key(const vaterm::InputData& event) {
    const auto& key = event.value.keybd;

    if (key.cp == 3) { running_ = false; return true; } // Ctrl-C
    if (help_) { help_ = false; dirty_ = true; return true; }
    if (key.cp == '?') { help_ = true; dirty_ = true; return true; }
    if (key.cp == '/') { begin_filter(); return true; }

    if (key.cp == 'q') { running_ = false; return true; }

    if (key.cp == 'h') { focus_.focus(&log_panel_); dirty_ = true; return true; }
    if (key.cp == 'l') { focus_.focus(&node_panel_); dirty_ = true; return true; }
    if (key.cp == 'j' || key.cp == 'k') {
        InputData move = InputData::makeKeybd(
            {.code = key.cp == 'j' ? KeyCode::DOWN : KeyCode::UP});
        return focus_.route(move);
    }

    if (key.cp == ' ') {
        bool follow = log_view_.following();
        log_view_.set_follow(!follow);
        status(follow ? "日志已暂停（继续入队）" : "日志跟随最新");
        dirty_ = true;
        return true;
    }
    if (key.cp == 'v') {
        bool verbose = !log_view_.verbose();
        log_view_.set_verbose(verbose);
        status(verbose ? "日志: 原始格式（v 切回精简）" : "日志: 精简格式");
        dirty_ = true;
        return true;
    }
    if (key.cp == 't') { test_all_delays(); return true; }
    if (key.cp == 'T') {
        if (auto* item = node_list_.selected_item()) test_delay(item->id);
        else status("没有选中节点");
        return true;
    }
    if (key.cp == 'd') { set_mode("direct"); return true; }
    if (key.cp == 'r') { set_mode("rule"); return true; }
    if (key.cp == 'g') { set_mode("global"); return true; }
    if (key.cp == '[') { cycle_group(-1); return true; }
    if (key.cp == ']') { cycle_group(1); return true; }
    if (key.cp == 'G') { open_dropdown(); return true; }
    return false;
}

void App::handle_mouse(const vaterm::InputData& event) {
    const auto& mouse = event.value.mouse;

    if (mouse.action == MouseAction::PRESS && mouse.button == MouseButton::LEFT) {
        // 模式面板不在焦点链里，点击直接交给分段控件。
        if (mode_seg_.bounds().contains(mouse.col, mouse.row)) {
            mode_seg_.on_event(event);
            dirty_ = true;
            return;
        }

        // 点节点面板的顶栏 = 展开分组下拉（标题里的 ▾）。
        if (node_panel_.bounds().contains(mouse.col, mouse.row)
            && mouse.row == node_panel_.bounds().y) {
            open_dropdown();
            focus_.focus(&node_panel_);
            return;
        }
        if (auto* widget = focus_.widget_at(mouse.col, mouse.row)) {
            focus_.focus(widget);
            widget->on_event(event);
        }
        dirty_ = true;
        return;
    }

    if (mouse.action == MouseAction::SCROLL_UP
        || mouse.action == MouseAction::SCROLL_DOWN) {
        if (auto* widget = focus_.widget_at(mouse.col, mouse.row)) {
            widget->on_event(event);
            dirty_ = true;
        }
    }
}

void App::handle(const vaterm::InputData& event) {
    if (dropdown_) {
        if (event.inputType == InputType::MOUSE) { handle_dropdown(event); return; }
        if (event.inputType == InputType::KEYBD) { handle_dropdown(event); return; }
    }

    if (event.inputType == InputType::MOUSE) {
        handle_mouse(event);
        return;
    }
    if (event.inputType != InputType::KEYBD) return;

    const auto& key = event.value.keybd;

    if (filtering_) {
        if (key.code == KeyCode::ESC) { end_filter(false); return; }
        if (key.code == KeyCode::ENTER) { end_filter(true); return; }
        if (key.code == KeyCode::BACKSPACE) {
            vaterm::utf::pop_back(filter_buffer_);
            dirty_ = true;
            return;
        }
        if (key.cp >= 0x20 && key.cp != 0x7F) {
            filter_buffer_ += vaterm::encode_utf8(key.cp);
            dirty_ = true;
        }
        return;
    }

    if (handle_global_key(event)) return;

    if (focus_.route(event)) {
        dirty_ = true;
        return;
    }
    dirty_ = true;
}

//-- 布局与渲染 ---------------------------------------------------------

void App::layout() {
    auto& fb = vatui::VaTui::instance().buffer();
    const int w = fb.getColMax();
    const int h = fb.getRowMax();
    if (w <= 0 || h <= 0) return;

    const int content_h = std::max(1, h - 1);

    int left_w = (w >= 72) ? std::clamp(w * 2 / 3, 44, std::max(44, w - 24))
                           : w / 2;
    left_w = std::clamp(left_w, 10, std::max(10, w - 10));

    // 模式/网速一行放底部，剩余高度给「日志 + 详情」和节点。
    int bottom_h = std::clamp(content_h / 3, 5, 8);
    if (content_h - bottom_h < 10) bottom_h = std::max(3, content_h - 10);

    auto [upper, bottom_area] = split_v(Rect{0, 0, w, content_h},
                                        content_h - bottom_h);
    auto [left_area, node_rect] = split_h(upper, left_w);
    auto [speed_rect, mode_rect] = split_h(bottom_area, left_w);

    // 详情窗从日志面板里切出来，只占几行。
    int inspector_h = std::clamp(upper.h / 3, 4, 6);
    if (upper.h - inspector_h < 6) inspector_h = std::max(3, upper.h - 6);

    Rect log_rect{left_area.x, left_area.y, left_area.w,
                  std::max(3, left_area.h - inspector_h)};
    Rect inspector{left_area.x, log_rect.bottom(), left_area.w, inspector_h};

    log_panel_.set_bounds(log_rect);
    node_panel_.set_bounds(node_rect);
    speed_panel_.set_bounds(speed_rect);
    mode_panel_.set_bounds(mode_rect);
    inspector_panel_.set_bounds(inspector);
    mode_seg_.set_bounds(mode_panel_.content_rect());

    inspector_rect_ = inspector;
    status_rect_    = Rect{0, content_h, w, h - content_h};
}

void App::render() {
    auto& fb = vatui::VaTui::instance().buffer();
    const Rect screen{0, 0, fb.getColMax(), fb.getRowMax()};
    Canvas canvas(fb, screen);

    canvas.clear(screen);

    // 每个面板一个色相区间，聚焦面板跟着相位流动。
    log_panel_.hue_shift       = hue_phase_;
    node_list_.hue_shift       = hue_phase_ + 18;
    mode_seg_.hue_shift        = hue_phase_ + 54;
    up_spark_.hue_shift        = hue_phase_ + 36;
    down_spark_.hue_shift      = hue_phase_ + 36;
    node_panel_.hue_shift      = hue_phase_ + 18;
    speed_panel_.hue_shift     = hue_phase_ + 36;
    mode_panel_.hue_shift      = hue_phase_ + 54;
    inspector_panel_.hue_shift = hue_phase_ + 72;

    log_panel_.render(canvas);
    node_panel_.render(canvas);
    speed_panel_.render(canvas);
    mode_panel_.render(canvas);
    inspector_panel_.render(canvas);

    {
        Rect content = speed_panel_.content_rect();
        canvas.push_clip(content);
        render_speed(canvas, content);
        canvas.pop_clip();
    }
    {
        Rect content = mode_panel_.content_rect();
        canvas.push_clip(content);
        render_mode(canvas, content);
        canvas.pop_clip();
    }
    {
        Rect content = inspector_panel_.content_rect();
        canvas.push_clip(content);
        render_inspector(canvas, content);
        canvas.pop_clip();
    }

    render_status(canvas, status_rect_);
    if (dropdown_) render_dropdown(canvas);
    if (help_) render_help(canvas, screen);

    // 每次重绘让色相走一小步：只在有数据/操作时流动，不额外烧帧。
    hue_phase_ = std::fmod(hue_phase_ + 6.0, 360.0);
}

void App::render_speed(Canvas& canvas, const Rect& rect) {
    if (rect.empty()) return;

    TrafficSample sample;
    std::deque<double> up_values, down_values;
    {
        std::lock_guard lock(traffic_mutex_);
        sample      = last_traffic_;
        up_values   = up_history_;
        down_values = down_history_;
    }
    up_spark_.reset();
    down_spark_.reset();
    for (double v : up_values) up_spark_.push(v);
    for (double v : down_values) down_spark_.push(v);

    const auto& t = theme();
    int y = rect.y;

    Stats stats_now;
    {
        std::lock_guard lock(stats_mutex_);
        stats_now = stats_;
    }
    if (!stats_now.api_ok && !sample.valid) {
        canvas.put_text(rect.x, y, "内核未运行", t.error);
        if (rect.h >= 2)
            canvas.put_text(rect.x, y + 1, "等待 mihomo 启动…", t.dim);
        return;
    }

    std::string down = "↓ " + format_rate(static_cast<double>(sample.down));
    std::string up   = "↑ " + format_rate(static_cast<double>(sample.up));

    canvas.put_text(rect.x, y, down, t.down);
    canvas.put_text(rect.x, y + 1, up, t.up);

    if (rect.w > kSpeedLabelW + 4) {
        Rect spark{rect.x + kSpeedLabelW, y, rect.w - kSpeedLabelW - 1, 1};
        down_spark_.render(canvas, spark, t.down);
        spark.y = y + 1;
        up_spark_.render(canvas, spark, t.up);
    }

    if (rect.h >= 3) {
        const Stats& s = stats_now;
        std::string totals = "累计 ↑" + format_bytes_short(static_cast<double>(s.up_total))
                           + " ↓" + format_bytes_short(static_cast<double>(s.down_total));
        canvas.put_text(rect.x, y + 2, totals, t.label);

        std::string conn = "连接 " + std::to_string(s.connections);
        if (rect.w > vaterm::utf::width(totals) + 10)
            canvas.put_text_right(rect.right() - 1, y + 2, conn, t.label);
    }
    if (rect.h >= 4) {
        std::string fps = demo_ ? "演示数据" : "实时";
        canvas.put_text(rect.x, y + 3, fps, t.dim);
    }
}

void App::render_mode(Canvas& canvas, const Rect& rect) {
    if (rect.empty()) return;

    // 第 0 行的分段控件由 Panel::render 负责（它是 mode_panel_ 的内容控件），
    // 这里只补下面两行说明文字。
    Stats s;
    {
        std::lock_guard lock(stats_mutex_);
        s = stats_;
    }
    const auto& t = theme();

    if (rect.h >= 2) {
        std::string now = s.now.empty() ? "—" : s.now;
        canvas.put_text(rect.x, rect.y + 1, "出口 ", t.label);
        int offset = vaterm::utf::width("出口 ");
        canvas.put_text(rect.x + offset, rect.y + 1, now, t.value);
    }
    if (rect.h >= 3) {
        std::string info;
        if (!s.api_ok) {
            info = "内核未运行";
            canvas.put_text(rect.x, rect.y + 2, info, t.error);
        } else {
            canvas.put_text(rect.x, rect.y + 2, "内核 " + s.version, t.label);
            if (s.uptime >= 0) {
                int offset = vaterm::utf::width("内核 " + s.version) + 1;
                canvas.put_text(rect.x + offset, rect.y + 2,
                                format_duration(s.uptime), t.dim);
            }
        }
    }
}

void App::render_inspector(Canvas& canvas, const Rect& rect) {
    if (rect.empty()) return;
    const auto& t = theme();

    Stats s;
    {
        std::lock_guard lock(stats_mutex_);
        s = stats_;
    }

    Widget* focus = focus_.current();

    //-- 日志详情：聚焦日志面板时显示选中条目的完整内容
    if (focus == &log_panel_) {
        inspector_panel_.set_title("详情 · 日志");
        auto line = log_view_.selected_line();
        if (!line) {
            canvas.put_text(rect.x, rect.y, "（还没有日志）", t.dim);
            return;
        }

        std::string head = std::string("[") + level_name(line->level) + "] " + line->text;
        int y = rect.y;
        canvas.put_text(rect.x, y, elide_head(head, rect.w), level_style(line->level));
        ++y;

        if (rect.h >= 5 && y < rect.bottom()) {
            canvas.put_text(rect.x, y, "── 原始 ──", t.dim);
            ++y;
        }
        for (const auto& row : wrap_text(line->raw, rect.w)) {
            if (y >= rect.bottom()) break;
            canvas.put_text(rect.x, y, row, t.label);
            ++y;
        }
        return;
    }

    //-- 节点详情
    if (focus == &node_panel_) {
        inspector_panel_.set_title("详情 · 节点");
        const auto* item = node_list_.selected_item();
        if (!item) {
            canvas.put_text(rect.x, rect.y, "（没有选中节点）", t.dim);
            return;
        }

        canvas.put_text(rect.x, rect.y, item->label, t.value);
        int y = rect.y + 1;

        std::string info;
        for (const auto& node : s.nodes) {
            if (node.name != item->id) continue;
            info = "类型 " + (node.type.empty() ? std::string("—") : node.type)
                 + " · 延迟 " + format_delay(node.delay);
            if (node.current) info += " · 当前出口";
            break;
        }
        if (info.empty()) info = item->detail;
        if (y < rect.bottom()) {
            canvas.put_text(rect.x, y, info, t.label);
            ++y;
        }
        if (y < rect.bottom()) {
            canvas.put_text(rect.x, y,
                            "分组 " + (s.group.empty() ? "—" : s.group)
                                + " · Enter 切换 · T 单测延迟",
                            t.dim);
        }
        return;
    }

    //-- 其他焦点：连接概览
    inspector_panel_.set_title("连接 · 实时");
    if (!s.api_ok) {
        canvas.put_text(rect.x, rect.y, "内核未运行", t.error);
        return;
    }

    std::string summary = "连接 " + std::to_string(s.connections)
                        + "  ·  累计 ↑" + format_bytes_short(static_cast<double>(s.up_total))
                        + " ↓" + format_bytes_short(static_cast<double>(s.down_total));
    canvas.put_text(rect.x, rect.y, summary, t.head);

    int y = rect.y + 1;
    if (s.top_connections.empty()) {
        if (y < rect.bottom()) canvas.put_text(rect.x, y, "（没有活动连接）", t.dim);
        return;
    }

    for (const auto& c : s.top_connections) {
        if (y >= rect.bottom()) break;

        std::string rate = "↓" + format_bytes_short(static_cast<double>(c.down))
                         + " ↑" + format_bytes_short(static_cast<double>(c.up));
        int rate_w = vaterm::utf::width(rate);
        int host_w = std::max(8, rect.w - rate_w - 2);
        std::string host = c.host.empty() ? "—" : c.host;

        canvas.put_text(rect.x, y, elide_tail(host, host_w), t.value);
        canvas.put_text_right(rect.right() - 1, y, rate, t.down);
        ++y;
    }
}

void App::render_status(Canvas& canvas, const Rect& rect) {
    if (rect.empty()) return;
    const auto& t = theme();

    // 底栏：深色底 + 彩虹字，既鲜艳又保证可读。
    const vatui::Style bar_bg{
        .fg_sgr = vatui::fg(vaterm::Color8{252}),
        .bg_sgr = vatui::bg(vaterm::Rgb{18, 20, 34}),
        .effects_sgr = vatui::effects({})};
    canvas.fill(rect, ' ', bar_bg);

    std::string text;
    if (filtering_) {
        text = "过滤: " + filter_buffer_ + "▏  (Enter 应用 · Esc 取消)";
    } else {
        {
            std::lock_guard lock(status_mutex_);
            if (!hint_text_.empty() && std::chrono::steady_clock::now() < hint_until_)
                text = hint_text_;
        }
        if (text.empty()) {
            if (rect.w >= 100)
                text = "Tab/h/l 切换  j/k 选择  Enter 应用  t 全测  Space 暂停  "
                       "v 格式  [ ] 分组  d/r/g 模式  ? 帮助  q 退出";
            else if (rect.w >= 76)
                text = "Tab/h/l 切换 j/k 选择 Enter 应用 t 全测 Space 暂停 "
                       "v 格式 ? 帮助 q 退出";
            else
                text = "Tab/h/l 切换 j/k 选择 t 全测 ? 帮助 q 退出";
        }
    }

    canvas.put_text_rainbow(rect.x + 1, rect.y, text,
                            Gradient{185 + hue_phase_, 345 + hue_phase_, 0.78, 1.0},
                            bar_bg);

    if (opts_.fps) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "fps %d  %.1fms  %zuc", fps_value_,
                      frame_ms_, frame_cells_);
        vatui::Style fps_style = t.status_text;
        fps_style.bg_sgr = bar_bg.bg_sgr;
        canvas.put_text_right(rect.right() - 1, rect.y, buf, fps_style);
    }
}

void App::render_dropdown(Canvas& canvas) {
    Stats s;
    {
        std::lock_guard lock(stats_mutex_);
        s = stats_;
    }
    if (s.groups.empty()) { dropdown_ = false; return; }

    int max_w = 0;
    for (const auto& group : s.groups)
        max_w = std::max(max_w, vaterm::utf::width(group));
    const int w = std::clamp(max_w + 4, 16, std::max(16, node_panel_.bounds().w));
    const int h = std::min(static_cast<int>(s.groups.size()) + 2, 16);
    const int x = std::max(0, node_panel_.bounds().right() - w - 1);
    const int y = node_panel_.bounds().y + 1;
    dropdown_rect_ = Rect{x, y, w, h};

    const auto& t = theme();
    canvas.fill(dropdown_rect_, ' ', t.help_bg);
    Gradient g{280 + hue_phase_, 340 + hue_phase_, 0.9, 0.95};
    canvas.border_gradient(dropdown_rect_, BoxStyle::Round, g, "分组", g);

    const int rows = h - 2;
    int       top  = 0;
    if (dropdown_sel_ >= rows) top = dropdown_sel_ - rows + 1;

    for (int i = 0; i < rows; ++i) {
        int index = top + i;
        if (index >= static_cast<int>(s.groups.size())) break;
        bool selected = (index == dropdown_sel_);
        std::string line = selected ? "▸ " : "  ";
        line += s.groups[static_cast<std::size_t>(index)];
        if (selected)
            canvas.fill(Rect{x + 1, y + 1 + i, w - 2, 1}, ' ', t.selected);
        canvas.put_text_padded(x + 1, y + 1 + i, line, w - 2,
                               selected ? t.selected : t.help_bg);
    }
}

void App::render_help(Canvas& canvas, const Rect& screen) {
    const auto& t = theme();
    const int   w = std::min(56, std::max(20, screen.w - 4));
    const int   h = std::min(18, std::max(8, screen.h - 2));
    Rect box{(screen.w - w) / 2, (screen.h - h) / 2, w, h};

    canvas.fill(box, ' ', t.help_bg);
    Gradient help_gradient{280 + hue_phase_, 360 + hue_phase_, 0.9, 0.95};
    Gradient help_title = help_gradient;
    help_title.from += 40;
    help_title.to += 40;
    canvas.border_gradient(box, BoxStyle::Double, help_gradient, "快捷键", help_title);

    const char* lines[] = {
        "Tab / Shift-Tab   在日志与节点之间切换",
        "h / l             切到左窗（日志）/ 右窗（节点）",
        "j / k             下移 / 上移（日志选行、节点选节点）",
        "↑ ↓               日志选行（下方看原文）/ 节点移动",
        "Enter             应用选中的节点",
        "t                 全部节点测速（自动切回原节点）",
        "T                 只测当前行",
        "Space             暂停 / 恢复日志跟随",
        "v                 日志：精简 / 原始格式",
        "[ ] / G           切换 / 展开代理分组",
        "d / r / g         直连 / 规则 / 全局",
        "/                 过滤当前面板（子串）",
        "PgUp PgDn Home End 日志翻页 / 跳转",
        "鼠标              点日志行看详情；点节点选中，再点应用；滚轮滚动",
        "?                 打开 / 关闭本帮助",
        "q / Ctrl-C        退出",
    };

    int y = box.y + 1;
    for (const char* line : lines) {
        if (y >= box.bottom() - 1) break;
        canvas.put_text(box.x + 2, y, line, t.help_bg);
        ++y;
    }
}

//-- 主循环 -------------------------------------------------------------

int App::run() {
    auto& tui = vatui::VaTui::instance();
    tui.init();

    auto& fb = tui.buffer();
    if (fb.getColMax() <= 0 || fb.getRowMax() <= 0) {
        std::fprintf(stderr, "mihomo-tui: 需要终端；无 tty 时请用 --snapshot\n");
        return 1;
    }

    vaterm::input input;
    if (!input.start()) {
        std::fprintf(stderr, "mihomo-tui: 输入模块启动失败（raw 模式未生效）\n");
        return 1;
    }
    input.enableMouse();

    // 必须用 dumper 而不是 waitInput()：waitInput 只返回「最新一条」，
    // 一次读进来多个字节（快速输入、输入法、粘贴）会丢中间的事件。
    vaterm::input::dumper dumper(input);

    struct sigaction sa {};
    sa.sa_handler = on_sigwinch;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, nullptr);

    if (demo_) {
        prepare_demo();
        demo_since_ = std::chrono::steady_clock::now();
    } else {
        start_threads();
    }

    layout();
    render();
    fb.swap();

    dirty_    = false;
    fps_since_ = std::chrono::steady_clock::now();

    while (running_) {
        if (g_winch) {
            g_winch = 0;
            uint16_t rows = 0, cols = 0;
            if (vaterm::terminal::size(rows, cols) && cols > 0 && rows > 0)
                fb.setSize({static_cast<int>(cols), static_cast<int>(rows)});
            vaterm::terminal::write(vaterm::terminal::clear_screen());
            layout();
            dirty_ = true;
        }

        // 先把积压的输入全部消费掉，再进入渲染。
        auto event = dumper.dump(30);
        if (event.inputType != InputType::NON) {
            handle(event);
            while (true) {
                auto more = dumper.dump(0);
                if (more.inputType == InputType::NON) break;
                handle(more);
            }
        }

        if (demo_) demo_tick();

        const bool input_force = (event.inputType != InputType::NON);
        apply_stats_to_widgets();

        auto now = std::chrono::steady_clock::now();
        const bool due = input_force
                      || now - last_render_ >= std::chrono::milliseconds(150);

        // 数据驱动的重绘限速到 ~6fps，避免日志刷屏时整屏狂刷；按键立即出帧。
        if (dirty_.load() && due) {
            dirty_ = false;
            auto begin = std::chrono::steady_clock::now();
            render();
            fb.swap();
            auto end = std::chrono::steady_clock::now();
            last_render_ = end;

            frame_ms_    = std::chrono::duration<double, std::milli>(end - begin).count();
            frame_cells_ = fb.last_composed_cells();
            ++frame_count_;
        }

        if (now - fps_since_ >= std::chrono::seconds(1)) {
            fps_value_ = static_cast<int>(frame_count_);
            frame_count_ = 0;
            fps_since_   = now;
            if (opts_.fps) dirty_ = true;
        }
    }

    input.stop();
    stop_threads();
    return 0;
}

//-- 单帧渲染 -----------------------------------------------------------

int App::snapshot() {
    auto& tui = vatui::VaTui::instance();
    auto& fb  = tui.buffer();

    hue_phase_ = 0.0;
    const int w = opts_.cols > 0 ? opts_.cols : 80;
    const int h = opts_.rows > 0 ? opts_.rows : 24;
    fb.setSize({w, h});
    fb.setPosition({0, 0});

    if (demo_) {
        prepare_demo();
    } else {
        // --live：同步抓一次真实数据，方便人工截屏。
        Api   api(opts_.api_base);
        Stats local;
        poll_stats(api, local, true);
        std::lock_guard lock(stats_mutex_);
        stats_ = std::move(local);
    }

    {
        std::lock_guard lock(status_mutex_);
        hint_text_.clear();
    }
    last_names_sig_.clear();
    apply_stats_to_widgets();
    layout();
    render();

    const std::string frame = opts_.snapshot_ansi ? fb.compose() : fb.compose_plain();

    if (opts_.snapshot_path == "-") {
        std::cout << frame;
        return 0;
    }

    std::ofstream file(opts_.snapshot_path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "mihomo-tui: 无法写入 %s\n", opts_.snapshot_path.c_str());
        return 1;
    }
    file.write(frame.data(), static_cast<std::streamsize>(frame.size()));
    return file ? 0 : 1;
}

} // namespace mihomo
