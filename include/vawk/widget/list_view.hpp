#ifndef VAWK_WIDGET_LIST_VIEW_HPP
#define VAWK_WIDGET_LIST_VIEW_HPP

//- vawk::widget::ListView — 条目列表 + 选中 + 视口滚动 + 过滤
//
// selected() 是「过滤后视图」里的索引；selected_item() 直接给回条目，
// 调用方不用关心过滤造成的索引偏移。
//
// 鼠标：点中某行先选中；再点一次同一行触发 on_confirm（等同双击）。

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "vatui.hpp"
#include "vawk/widget/widget.hpp"

namespace vawk::widget {

struct ListItem {
    std::string id;     ///< 业务键（如节点名），用于 API 调用
    std::string label;  ///< 左侧显示文本
    std::string detail; ///< 右侧显示文本（延迟、类型等）
};

class ListView : public Widget {
  public:
    ListView();

    void set_items(std::vector<ListItem> items);
    const std::vector<ListItem>& items() const { return items_; }

    void set_selected(int index);
    int  selected() const { return selected_; }
    /** @brief 当前选中的条目；视图为空时 nullptr。 */
    const ListItem* selected_item() const;

    /** @brief 按业务 id 选中；找不到返回 false（选中状态不变）。 */
    bool select_id(const std::string& id);
    /** @brief 就地更新某条目的右侧文本，不动选中/滚动位置。 */
    bool set_detail(const std::string& id, std::string detail);

    void move(int delta);
    void to_top();
    void to_bottom();

    void set_filter(std::string filter);
    const std::string& filter() const { return filter_; }

    void set_empty_text(std::string text) { empty_text_ = std::move(text); }

    /** @brief 选中项变化（键盘/鼠标移动都会触发）。 */
    std::function<void(const ListItem&)> on_select;
    /** @brief Enter 或二次点击。 */
    std::function<void(const ListItem&)> on_confirm;

    void render(Canvas& canvas) override;
    bool on_event(const vaterm::InputData& event) override;

    vatui::Style normal_style{};
    vatui::Style selected_style{};
    vatui::Style unfocused_selected_style{};
    vatui::Style detail_style{};
    vatui::Style empty_style{};

  private:
    /** @brief 过滤后的视图（items_ 的下标）。 */
    std::vector<int> view() const;
    void             ensure_visible(int view_index, int rows);
    void             notify_select();

    std::vector<ListItem> items_;
    std::string           filter_;
    std::string           empty_text_ = "（空）";
    int                   selected_   = 0; ///< 视图内索引
    int                   top_        = 0; ///< 视图内首个可见行
    int                   last_click_ = -1;
};

} // namespace vawk::widget

#endif // VAWK_WIDGET_LIST_VIEW_HPP
