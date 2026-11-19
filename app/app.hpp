#ifndef MIHOMO_APP_HPP
#define MIHOMO_APP_HPP

//- mihomo::App — 主界面
//
// 布局（80×24 起）：
//   ┌───────────────────────────┬──────────────────┐
//   │ 日志 [follow]             │ 节点 🔰 节点选择 │
//   ├───────────────────────────┼──────────────────┤
//   │ 网速                      │ 模式             │
//   └───────────────────────────┴──────────────────┘
//   Tab 切焦点  ↑↓ 选节点  Enter 应用  t 测速 ...
//
// 线程：
//   journal   → LogView（LogView 自带锁）
//   traffic   → 采样队列（本类持锁）
//   stats     → 每秒轮询 /version /configs /connections /proxies
//   主线程     → input::waitInput(30) + 按需渲染（数据变化才重画）

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "vaterm/input.hpp"
#include "vawk/widget/focus.hpp"
#include "vawk/widget/list_view.hpp"
#include "vawk/widget/log_view.hpp"
#include "vawk/widget/panel.hpp"
#include "vawk/widget/segmented.hpp"
#include "vawk/widget/sparkline.hpp"

#include "api.hpp"
#include "journal.hpp"
#include "traffic.hpp"

namespace mihomo {

struct AppOptions {
    bool        demo           = false; ///< 假数据，不连内核
    bool        fps            = false; ///< 状态栏显示帧耗时/脏格数
    bool        snapshot_ansi  = false; ///< snapshot 输出带 ANSI
    std::string snapshot_path;          ///< 非空 = 渲染一帧后退出
    int         cols           = 0;     ///< snapshot 尺寸；0 = 80
    int         rows           = 0;     ///< snapshot 尺寸；0 = 24
    std::string api_base       = "http://127.0.0.1:9090";
    int         test_jobs      = 32; ///< 全局测速并发数
};

class App {
  public:
    explicit App(AppOptions options);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /** @brief 进入 raw 模式跑 TUI；返回进程退出码。 */
    int run();
    /** @brief 不进 raw 模式，渲染一帧到文件（或 "-" 到 stdout）。 */
    int snapshot();

  private:
    struct NodeInfo {
        std::string name;
        std::string type;
        int         delay   = -1;
        bool        current = false;
    };

    struct ConnInfo {
        std::string host;
        std::string chain;
        std::string rule;
        long long   up   = 0;
        long long   down = 0;
    };

    struct Stats {
        bool                     api_ok = false;
        std::string              error;
        std::string              version;
        long long                uptime = -1;
        std::string              mode   = "rule";
        std::vector<std::string> groups;
        std::string              group;
        std::vector<NodeInfo>    nodes;
        std::string              now;
        long long                connections = 0;
        long long                up_total    = 0;
        long long                down_total  = 0;
        std::vector<ConnInfo>    top_connections;
    };

    //-- 生命周期
    void prepare_widgets();
    void prepare_demo();
    void start_threads();
    void stop_threads();
    void layout();
    void render();

    //-- 数据
    void stats_loop();
    void delay_loop();
    void poll_stats(Api& api, Stats& local, bool fetch_proxies);
    void apply_stats_to_widgets();
    void on_traffic(const TrafficSample& sample);
    void on_log_line(vawk::widget::LogLevel level, std::string line);
    void demo_tick();

    //-- 交互
    void handle(const vaterm::InputData& event);
    bool handle_global_key(const vaterm::InputData& event);
    void handle_mouse(const vaterm::InputData& event);
    void begin_filter();
    void end_filter(bool apply);
    void switch_node(const std::string& name);
    void cycle_group(int delta);
    void choose_group(int index);
    void open_dropdown();
    void close_dropdown(bool apply);
    bool handle_dropdown(const vaterm::InputData& event);
    void render_dropdown(vawk::widget::Canvas& canvas);
    void set_mode(std::string mode);
    void test_delay(const std::string& name);
    void test_all_delays();
    void run_global_test(const std::string& group, std::uint64_t epoch);
    int  query_delay(Api& api, const std::string& name);
    bool api_online() const;

    //-- 渲染细节
    void render_speed(vawk::widget::Canvas& canvas,
                      const vawk::widget::Rect& rect);
    void render_mode(vawk::widget::Canvas& canvas,
                     const vawk::widget::Rect& rect);
    void render_inspector(vawk::widget::Canvas& canvas,
                          const vawk::widget::Rect& rect);
    void render_status(vawk::widget::Canvas& canvas,
                       const vawk::widget::Rect& rect);
    void render_help(vawk::widget::Canvas& canvas,
                     const vawk::widget::Rect& rect);

    //-- 消息：status 写进日志，progress 短暂占用底栏
    void status(std::string message);
    void progress(std::string message);

    AppOptions opts_;
    bool       demo_ = false;

    // 控件
    vawk::widget::Panel       log_panel_{"日志"};
    vawk::widget::Panel       node_panel_{"节点"};
    vawk::widget::Panel       speed_panel_{"网速"};
    vawk::widget::Panel       mode_panel_{"模式"};
    vawk::widget::Panel       inspector_panel_{"详情"};
    vawk::widget::LogView     log_view_{5000};
    vawk::widget::ListView    node_list_;
    vawk::widget::Segmented   mode_seg_{{"直连", "规则", "全局"}, 1};
    vawk::widget::FocusChain  focus_;
    vawk::widget::Sparkline   up_spark_{60};
    vawk::widget::Sparkline   down_spark_{60};

    // 后台线程
    Journal                       journal_;
    TrafficStream                 traffic_;
    std::thread                   stats_thread_;
    std::thread                   delay_thread_;
    std::atomic<bool>             stop_{false};
    std::mutex                    delay_mutex_;
    std::condition_variable       delay_cv_;
    std::deque<std::string>       delay_queue_;
    bool                          delay_quit_ = false;
    bool                          delay_all_  = false; ///< 请求了一次全局测速
    std::string                   all_group_;
    std::uint64_t                 all_epoch_ = 0;

    std::atomic<bool>             testing_{false};      ///< 全局测速进行中
    std::atomic<bool>             delay_cancel_{false}; ///< 退出时中止所有测速请求
    std::atomic<std::uint64_t>    select_epoch_{0};     ///< 用户手动切换的代数

    // 共享数据
    mutable std::mutex stats_mutex_;
    Stats              stats_;
    std::string        last_names_sig_;

    mutable std::mutex traffic_mutex_;
    TrafficSample      last_traffic_{};
    std::deque<double> up_history_;
    std::deque<double> down_history_;

    std::mutex                        delays_mutex_;
    std::unordered_map<std::string, int> delays_;

    // 交互状态
    std::atomic<bool> dirty_{true};
    vawk::widget::Rect status_rect_{};
    vawk::widget::Rect inspector_rect_{};
    bool        running_   = true;
    bool        help_      = false;
    bool        dropdown_  = false;
    int         dropdown_sel_ = 0;
    vawk::widget::Rect dropdown_rect_{};
    bool        filtering_ = false;
    std::string filter_buffer_;
    std::string filter_panel_; ///< "log" 或 "node"

    std::mutex  status_mutex_;
    std::string hint_text_;
    std::chrono::steady_clock::time_point hint_until_{};

    // 渐变相位：让聚焦面板的边框缓慢流动
    double hue_phase_ = 0.0;
    std::chrono::steady_clock::time_point last_render_{};

    // 帧统计
    std::chrono::steady_clock::time_point demo_since_{};
    long long frame_count_   = 0;
    double    frame_ms_      = 0;
    size_t    frame_cells_   = 0;
    std::chrono::steady_clock::time_point fps_since_{};
    int       fps_value_     = 0;
};

} // namespace mihomo

#endif // MIHOMO_APP_HPP
