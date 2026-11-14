#ifndef VAWK_WIDGET_LOG_VIEW_HPP
#define VAWK_WIDGET_LOG_VIEW_HPP

//- vawk::widget::LogView — 环形行缓冲 + follow + 滚动
//
// 线程模型：生产者（journal 读取线程）调 push()，UI 线程调 render()/滚动。
// 内部用一把 mutex 串起来，临界区只做拷贝/遍历，不做绘制。
//
// 暂停语义（对应规格「暂停时不滚动，但继续入队，恢复时跳到末尾」）：
//   follow = true   总是贴底；offset 恒为 0
//   follow = false  以「距底部的行数」为锚点；新行到达时 offset 自增，
//                   屏幕上的内容因此保持不动
//   恢复（Space / End）offset 归零，立刻跳到最新一行

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "vatui.hpp"
#include "vawk/widget/widget.hpp"

namespace vawk::widget {

enum class LogLevel { Debug, Info, Warn, Error };

/** @brief 一行日志内不同片段的语义色。 */
enum class LogTone {
    Default, ///< 正文
    Dim,     ///< 时间、箭头、次要信息
    Proto,   ///< TCP / UDP / DNS 协议标签
    Host,    ///< 目标主机
    Out,     ///< 出口节点
    Direct,  ///< DIRECT
    Reject,  ///< REJECT / DROP
    Warn,
    Error,
};

/** @brief 带语义色的文本片段。 */
struct LogSpan {
    std::string text;
    LogTone     tone = LogTone::Default;
};

struct LogLine {
    LogLevel                level = LogLevel::Info;
    std::string             text;  ///< 精简后的纯文本（过滤/复制用）
    std::string             raw;   ///< journal 原始行（--verbose / 排查用）
    std::vector<LogSpan>    spans; ///< 分色片段；空则按 level 单色绘制
};

class LogView : public Widget {
  public:
    explicit LogView(std::size_t capacity = 5000);

    /** @brief 追加一行；超出容量丢最旧的。 */
    void push(LogLine line);
    void push(LogLevel level, std::string text);
    /** @brief 清空所有行并复位滚动。 */
    void clear();

    std::size_t size() const;

    /** @brief follow 开关；打开时立刻贴底。 */
    void set_follow(bool follow);
    bool following() const;
    /** @brief 是否处于暂停（!follow）。 */
    bool paused() const { return !following(); }

    /** @brief 正数向旧（上）滚，负数向新（下）滚。 */
    void scroll(int delta);
    void page_up(int page);
    void page_down(int page);
    void to_top();
    void to_bottom();

    /** @brief 子串过滤（ASCII 大小写不敏感）；空串表示不过滤。 */
    void set_filter(std::string filter);
    const std::string& filter() const { return filter_; }

    /** @brief 一屏行数，由 render 自动更新；PageUp/PageDown 用它。 */
    int page_lines() const { return page_lines_; }

    /** @brief 原始日志模式：不显示精简结果，直接显示 journal 原文。 */
    void set_verbose(bool verbose);
    bool verbose() const;

    void render(Canvas& canvas) override;
    bool on_event(const vaterm::InputData& event) override;

    vatui::Style debug_style{};
    vatui::Style info_style{};
    vatui::Style warn_style{};
    vatui::Style error_style{};
    vatui::Style empty_style{};

    // 语义色
    vatui::Style dim_style{};
    vatui::Style proto_style{};
    vatui::Style host_style{};
    vatui::Style out_style{};
    vatui::Style direct_style{};
    vatui::Style reject_style{};

  private:
    bool matches(const LogLine& line) const;
    const vatui::Style& tone_style(LogTone tone) const;
    std::vector<const LogLine*> filtered_locked() const;

    mutable std::mutex  mutex_;
    std::deque<LogLine> lines_;
    std::size_t         capacity_;
    int                 offset_    = 0;     ///< 距底部的行数
    bool                follow_    = true;
    std::string         filter_;
    int                 page_lines_ = 10;
    bool                verbose_    = false;
};

} // namespace vawk::widget

#endif // VAWK_WIDGET_LOG_VIEW_HPP
