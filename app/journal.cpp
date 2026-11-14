//- journal.cpp — journalctl 跟随线程

#include "journal.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.hpp"

namespace mihomo {

vawk::widget::LogLevel classify_log_line(std::string_view line) {
    // mihomo 的日志形如：time="..." level=error msg="..." / [TCP] ...
    // 也兼容 journalctl 偶尔混进来的 "ERROR" / "WARN" 前缀。
    const std::string lower = to_lower_ascii(line);

    auto has = [&](std::string_view needle) { return lower.find(needle) != std::string::npos; };

    if (has("level=error") || has("level=fatal") || has("level=panic")
        || has("[error]") || has(" error ") || starts_with(lower, "error")
        || has("panic:") || has("fatal:"))
        return vawk::widget::LogLevel::Error;

    if (has("level=warn") || has("level=warning") || has("[warn]")
        || has(" warning ") || starts_with(lower, "warn"))
        return vawk::widget::LogLevel::Warn;

    if (has("level=debug") || has("level=trace") || has("[debug]"))
        return vawk::widget::LogLevel::Debug;

    return vawk::widget::LogLevel::Info;
}

Journal::~Journal() { stop(); }

bool Journal::start(LineFn on_line, std::string unit) {
    if (running_.load()) return false;
    on_line_ = std::move(on_line);

    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) return false;

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        return false;
    }

    if (pid == 0) {
        // 子进程：stdout/stderr 都接到管道，避免 journalctl 的告警污染终端。
        ::dup2(fds[1], STDOUT_FILENO);
        ::dup2(fds[1], STDERR_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        const char* argv[] = {"journalctl", "--user", "-u", unit.c_str(),
                              "-f",         "-o",    "cat", "-n", "200",
                              nullptr};
        ::execvp("journalctl", const_cast<char* const*>(argv));
        ::_exit(127);
    }

    ::close(fds[1]);
    pid_ = pid;
    fd_  = fds[0];
    wanted_.store(true);
    running_.store(true);
    thread_ = std::thread(&Journal::loop, this, std::move(unit));
    return true;
}

void Journal::loop(std::string /*unit*/) {
    std::string pending;
    std::vector<char> buf(8192);

    while (wanted_.load()) {
        struct pollfd pfd{fd_, POLLIN, 0};
        int rc = ::poll(&pfd, 1, 200);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) continue;

        ssize_t n = ::read(fd_, buf.data(), buf.size());
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
        if (n == 0) break; // 子进程退出

        pending.append(buf.data(), static_cast<std::size_t>(n));

        std::size_t start = 0;
        while (true) {
            std::size_t nl = pending.find('\n', start);
            if (nl == std::string::npos) break;
            std::string line = pending.substr(start, nl - start);
            start = nl + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (on_line_) on_line_(classify_log_line(line), std::move(line));
        }
        pending.erase(0, start);
        if (pending.size() > 256 * 1024) pending.clear(); // 防呆：没有换行的巨量输出

        if (!wanted_.load()) break;
    }

    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    running_.store(false);
}

void Journal::stop() {
    wanted_.store(false);

    // 先礼后兵：journalctl 在管道写满且读端已经停下时，可能迟迟不响应
    // SIGTERM（它还要把积压输出刷出去），直接 waitpid 会死锁。
    // 所以：SIGTERM → 最多等 200ms → SIGKILL，然后再收尸。
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);

        bool reaped = false;
        for (int i = 0; i < 20; ++i) {
            int   status = 0;
            pid_t r      = ::waitpid(pid_, &status, WNOHANG);
            if (r == pid_) { reaped = true; pid_ = -1; break; }
            if (r < 0 && errno != EINTR) { reaped = true; pid_ = -1; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!reaped && pid_ > 0) ::kill(pid_, SIGKILL);
    }

    if (thread_.joinable()) thread_.join();

    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (pid_ > 0) {
        int status = 0;
        ::waitpid(pid_, &status, 0);
        pid_ = -1;
    }
    running_.store(false);
}

} // namespace mihomo
