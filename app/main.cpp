//- main.cpp — 参数分发

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "app.hpp"
#include "common.hpp"
#include "use.hpp"

namespace {

constexpr int kExitUsage = 64;

void print_help(std::ostream& out) {
    out <<
R"(mihomo-tui — 管理本机 mihomo 内核的终端界面

用法:
  mihomo-tui [界面选项]
  mihomo-tui --use <本地路径|URL> [--compat|--no-compat] [--no-restart] [--offline]

界面:
  --demo-layout        用假数据跑界面（不连内核，便于调布局）
  --snapshot <文件>    不进 raw 模式，渲染一帧后退出；"-" 表示 stdout
  --cols N --rows N    snapshot 的画布尺寸（默认 80×24）
  --ansi               snapshot 输出带 ANSI 转义（默认纯文本）
  --live               snapshot 抓取真实内核数据（默认用演示数据）
  --fps                状态栏显示 fps / 帧耗时 / 写入单元格数
  --api <URL>          内核 API 地址（默认 http://127.0.0.1:9090）

--use:
  --compat             强制兼容修正：用 TCP/53 解析节点域名并钉成 IP，
                       同时把所有节点 client-fingerprint 改成 safari
                       （默认按需自动开启，一般不用手写）
  --no-compat          关闭自动兼容修正（整包原样落盘）
  --no-restart         只落盘，不热重载
  --offline            跳过内核在线验证（内核没运行时用）
  --dns <ip[,ip]>      覆盖节点域名的解析服务器（默认 223.6.6.6 优先；
                       不同 DNS 会返回不同节点 IP，选错会一片超时）
  --proxy <url>        下载配置时走指定代理（默认直连，不读 http(s)_proxy）
  --insecure           --use 下载时跳过 TLS 证书校验（自签名订阅用）
  --config <路径>      目标配置（默认 ~/.config/mihomo/config.yaml）

其它:
  -h, --help           显示本帮助
  --version            显示版本

退出码（--use）:
  0 成功   2 格式不对/不覆盖   3 下载或读写失败
  4 在线验证失败           5 热重载失败（已回滚）
)";
}

void die_usage(const std::string& message) {
    std::cerr << "mihomo-tui: " << message << "\n"
              << "用 --help 看用法。\n";
    std::exit(kExitUsage);
}

} // namespace

int main(int argc, char** argv) {
    mihomo::AppOptions app;
    mihomo::UseOptions use;
    std::string        use_source;
    bool               have_use = false;
    bool               snapshot_ansi = false;
    bool               live = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "mihomo-tui: " << name << " 需要一个参数\n";
                std::exit(kExitUsage);
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            print_help(std::cout);
            return 0;
        }
        if (arg == "--version") {
            std::cout << "mihomo-tui 0.1.0\n";
            return 0;
        }
        if (arg == "--use") {
            use_source = next("--use");
            have_use   = true;
        } else if (arg == "--compat") {
            use.compat = true;
        } else if (arg == "--no-compat") {
            use.no_compat = true;
        } else if (arg == "--no-restart") {
            use.restart = false;
        } else if (arg == "--offline") {
            use.offline = true;
        } else if (arg == "--proxy") {
            use.proxy = next("--proxy");
        } else if (arg == "--insecure") {
            use.insecure = true;
        } else if (arg == "--dns") {
            std::string spec = next("--dns");
            std::size_t start = 0;
            while (start <= spec.size()) {
                auto comma = spec.find(',', start);
                if (comma == std::string::npos) comma = spec.size();
                auto ip = spec.substr(start, comma - start);
                if (!ip.empty()) use.dns_servers.push_back(ip);
                start = comma + 1;
            }
        } else if (arg == "--config") {
            use.config_path = next("--config");
        } else if (arg == "--api") {
            use.api_base = next("--api");
            app.api_base = use.api_base;
        } else if (arg == "--demo-layout" || arg == "--demo") {
            app.demo = true;
        } else if (arg == "--snapshot") {
            app.snapshot_path = next("--snapshot");
        } else if (arg == "--cols") {
            app.cols = std::atoi(next("--cols").c_str());
        } else if (arg == "--rows") {
            app.rows = std::atoi(next("--rows").c_str());
        } else if (arg == "--ansi") {
            snapshot_ansi = true;
        } else if (arg == "--live") {
            live = true;
        } else if (arg == "--fps") {
            app.fps = true;
        } else {
            die_usage("未知参数: " + arg);
        }
    }

    if (app.cols < 0 || app.rows < 0) die_usage("--cols/--rows 不能为负");

    if (have_use) {
        use.source = use_source;
        if (use.config_path.empty()) use.config_path = mihomo::default_config_path();
        use.config_path = mihomo::expand_home(use.config_path);
        use.api_base    = app.api_base;
        return mihomo::run_use(use, std::cout);
    }

    if (!app.snapshot_path.empty()) {
        app.snapshot_ansi = snapshot_ansi;
        if (!live) app.demo = true;
        mihomo::App gui(app);
        return gui.snapshot();
    }

    mihomo::App gui(app);
    return gui.run();
}
