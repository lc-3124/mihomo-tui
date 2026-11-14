#ifndef MIHOMO_JOURNAL_HPP
#define MIHOMO_JOURNAL_HPP

//- mihomo::Journal — 跟随 systemd 用户单元日志
//
// fork/exec `journalctl --user -u mihomo -f -o cat -n 200`，用 poll 读管道，
// 按行拆分后交给回调。子进程退出（用户 stop 或单元消失）时线程自然结束。
//
// 回调在后台线程执行；调用方负责自己的线程安全（App 里直接 push 给 LogView，
// LogView 自身带锁）。

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <thread>

#include "vawk/widget/log_view.hpp"

namespace mihomo {

/** @brief 从一行 journal 文本猜日志级别。 */
vawk::widget::LogLevel classify_log_line(std::string_view line);

class Journal {
  public:
    using LineFn = std::function<void(vawk::widget::LogLevel, std::string)>;

    Journal() = default;
    ~Journal();

    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;

    /** @brief 启动跟随；unit 默认为 mihomo。已在运行时返回 false。 */
    bool start(LineFn on_line, std::string unit = "mihomo");
    /** @brief 停止跟随并回收子进程；可重复调用。 */
    void stop();

    bool running() const { return running_.load(); }

  private:
    void loop(std::string unit);

    LineFn             on_line_;
    std::thread        thread_;
    std::atomic<bool>  running_{false};
    std::atomic<bool>  wanted_{false};
    pid_t              pid_ = -1;
    int                fd_  = -1;
};

} // namespace mihomo

#endif // MIHOMO_JOURNAL_HPP
