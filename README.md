# mihomo-tui

管理本机 [mihomo](https://github.com/MetaCubeX/mihomo) 内核的终端界面。四块主面板 + 一个详情窗：
实时日志、节点列表、实时网速、模式切换；外加一个 `--use` 子命令，用来安全地替换
内核配置。

内置了从 VAWK 抽出的 `vaterm`（颜色/输入/UTF-8/终端）和 `vatui`（帧缓冲）两层，
加上新写的 `vawk/widget` 控件层；应用层不依赖任何 UI 框架。

```
┌──────────────────────────────┬────────────────────┐
│ 日志  [follow]               │ 节点 · 🔰 节点选择 │
│ 16:54:31 TCP yt → 新加坡01   │ ▸ 🇸🇬 新加坡01 921ms│
│ ...                          │   🇯🇵 日本01 1004ms │
├ 详情 · 日志 ─────────────────┤   🇭🇰 香港01  168ms │
│ [INF] … → 新加坡01           │   🇺🇸 美国02 1350ms │
│ time="..." msg="[TCP] ..."   │   ...               │
├──────────────────────────────┼────────────────────┤
│ 网速                          │ 模式                │
│ ↓ 1.2 MB/s  ▁▂▅█▇▃▂          │ [直连][规则][全局]   │
│ ↑ 84 KB/s   ▁▁▂▁▃▂▁          │ 出口 新加坡01        │
└──────────────────────────────┴────────────────────┘
 Tab/h/l 切换  j/k 选择  Enter 应用  t 全测  ...
```

外观：

- 每个面板一条不同的**色相渐变边框**，聚焦时更亮、并随重绘缓慢流动；底栏是深色底 + 彩虹字；
- 网速曲线的方块按数值从青到紫渐变；
- 日志选中行整行铺同一条靛蓝底色（铺满到行尾，不留时间色块的直角边），
  时间文字换成青色粗体，既醒目又不破坏整行的一体感；
- 日志、节点、模式各自有主题色，整体比默认终端配色鲜艳得多。

## 详情窗

「详情」窗不占整条底边，而是从日志面板下方切出 4–6 行（`不要太多行`）：

- **日志面板聚焦**：显示当前选中日志的完整内容 —— 第一行是精简摘要，下面是
  未裁剪的 journal 原文（按面板宽度换行）。`j/k` 或 `↑↓` 选行，点击某行也行；
- **节点面板聚焦**：显示选中节点的名称、类型、延迟、是否当前出口、所属分组；
- 节点列表为空时退回显示实时连接概览。

焦点只在**日志**和**节点**两个面板之间轮转：`Tab`/`Shift-Tab`，或者 `h`（左，日志）
`l`（右，节点）。模式面板不参与键盘焦点，仍然可以用 `d/r/g` 或鼠标点击切换。

操作反馈（切换节点、切模式、测速结果……）不再长期占着底栏，而是作为一条带 `▸`
标记的日志写进日志面板跟着滚屏；底栏只在过滤输入或测速进度时短暂显示提示。

## 构建

依赖：C++23 编译器、libcurl 开发头、nlohmann/json 头文件。

```bash
cd ~/work/project/mihomo-tui
make            # 生成 build/mihomo-tui
make test       # 组件单测 + --use 单测 + snapshot golden 回归
make check      # 所有公共头逐个 -fsyntax-only
make demo       # 假数据跑界面，不连内核
```

## 运行

```bash
./build/mihomo-tui                 # 连本机 127.0.0.1:9090
./build/mihomo-tui --api http://127.0.0.1:9090 --fps
```

| 按键 | 作用 |
|---|---|
| `Tab` / `Shift-Tab` | 在日志与节点之间切换 |
| `h` / `l` | 切到左窗（日志）/ 右窗（节点） |
| `j` / `k` | 下移 / 上移（日志选行、节点选择） |
| `↑` `↓` | 日志里移动选中行（详情窗显示完整内容）；节点列表里移动 |
| `Enter` | 应用选中的节点（PUT 到当前分组） |
| `t` | 全局测速：32 路并发测所有叶子节点（`--test-jobs` 可调）；结束后若发现当前节点被自动切走，会切回测速前的选择 |
| `T` | 只测当前行（分组行会提示改用 `t`） |
| `v` | 日志在「精简 / 原始」之间切换 |
| `Space` | 暂停 / 恢复日志跟随（暂停时继续入队） |
| `[` `]` | 切换代理分组（`🔰 节点选择`、`♻️ 自动选择` …） |
| `d` / `r` / `g` | 直连 / 规则 / 全局 |
| `/` | 子串过滤当前面板（日志或节点），`Enter` 应用，`Esc` 取消 |
| `PgUp` `PgDn` `Home` `End` | 日志翻页 / 跳到最旧 / 最新 |
| 鼠标 | 点击聚焦并选中，再点一次应用节点；点击分段切模式；滚轮滚动 |
| `?` | 快捷键帮助浮层 |
| `q` / `Ctrl-C` | 退出 |

内核没起来时界面照常启动：节点/网速显示「内核未运行」，日志继续跟 journal。

全局测速（`t`）用 8 路并发测所有叶子节点，行内先显示 `…`，完成后填延迟。
它不会调用分组自己的测速接口（那会让内核重跑整组并改选最快），所以一般不会触发
自动切换；万一被测速期间的外部因素切走了，结束时会把 Selector 切回测速前的节点。
如果测速过程中你自己手动切了节点，则以你的手动操作为准，不会回切。

### 日志

日志在进环形缓冲之前就先加工过，不是原样堆屏幕：

```
17:20:01 TCP www.google.com → 🇸🇬 新加坡01[直连]
17:20:02 WRN TCP dial 日本01: connection timeout
17:20:03 ERR rule-set load failed: geosite-cn: no such file
```

- 从 `time="..." level=... msg="..."` 里抽出时间并压成 `HH:MM:SS`；
- 识别 `[TCP]/[UDP]/[DNS]` 协议标签，提取目标和出口，去掉默认端口、截断超长域名；
- 颜色跟着语义走：时间暗、协议青、出口按 `DIRECT`（绿）/ `REJECT`（红）/ 普通节点（蓝绿）区分，
  错误整行红、告警黄；
- 规则名（`DomainSuffix` 等）不塞进精简行，避免把出口挤掉；按 `v` 可切到原始行查看全部字段；
- `/` 过滤同时匹配精简文本和原始行。

## `--use`：替换内核配置

```bash
mihomo-tui --use /path/config.yaml
mihomo-tui --use 'https://.../config?token=...'
mihomo-tui --use URL                    # 默认自动做兼容修正
mihomo-tui --use URL --no-compat         # 原样落盘（本机不建议）
mihomo-tui --use URL --no-restart        # 只落盘
mihomo-tui --use URL --proxy http://127.0.0.1:7890   # 下载走代理
mihomo-tui --use URL --insecure          # 跳过证书校验（自签名订阅）
```

七步流程，逐行打印：

```
[1/7] 来源: URL
[2/7] 下载 ... HTTP 200, 492485 bytes (1.3 MB/s)
[3/7] 格式识别 ... Clash YAML / 29 节点
[4/7] 兼容修正 ... 10 个域名钉 IP，29 个节点改 safari
[5/7] 备份 ... config.yaml.bak-20261114-170233
[6/7] 临时文件加载验证 ... PUT /configs {"path":tmp,"force":true} -> ok
[7/7] 落盘 + 生效 ... ok（当前出口 新加坡01）
```

下载默认**直连**，不读 `http(s)_proxy` 环境变量——代理往往正是待修复的对象，
用坏代理去下载配置会直接 `SSL connect error`。需要走代理时用 `--proxy`。

另外会识别「已到期」的占位订阅（节点全指向 `127.0.0.1`、名字写着服务到期/续费）：
这种配置直接拒绝，退出码 2，不会覆盖现有配置。

安全约束：

- 请求固定带 `User-Agent: mihomo`（不带的话订阅返回 base64 `vless://` 列表）。
- 非 Clash YAML → 报错、**不覆盖**、退出码 2。
- 先在同目录写临时文件，让内核用 `PUT /configs?force=true` 加载验证；通过才
  `rename` 覆盖，权限 600。
- 失败自动回滚：校验失败时旧配置原样不动；落盘后热重载失败会把备份写回去。
- 备份文件名带时间戳；`--no-restart` 只落盘（也跳过在线验证，因为验证本身就是
  让内核加载一次）。
- 内核的路径白名单（`SAFE_PATHS`）只允许 `~/.config/mihomo`，所以目标配置要和
  内核同一目录，否则验证会返回 400 —— 这是 mihomo 的保护，不是本程序的 bug。

### 兼容修正：默认按需自动开启

本机有两个坑：

1. 节点域名的 A 记录在 **UDP/53** 上被丢，只有 **TCP/53** 能解析；
2. 只有 `client-fingerprint: safari` 能完成 REALITY 握手。

`--use` 落盘前会检查整份配置：只要发现**任何节点的 `server` 是域名**，或**任何节点的
`client-fingerprint` 不是 safari（含完全没写）**，就自动做兼容修正，并在 `[4/7]` 标注
「自动启用；--no-compat 可关闭」。

修正不只是「查一次 DNS 钉一个 IP」：

- **AliDNS 优先**（223.6.6.6 / 223.5.5.5），再问 119.28.28.28 / 114.114.114.114；
- **多轮查询收集候选 IP**——同一个节点域名会轮换返回多个 Anycast IP（例如
  `155.254.103.175` / `.188` / `.189`），其中有些 IP 的 relay 端口是死的；
- **按节点端口逐个 TCP 试连**，钉能连上的那个。只看第一条 A 记录会出现
  「配置看着没问题，但整批节点超时」。
- 如果一台解析器超时/无应答，本次会拉黑它，避免每个域名都白等一轮。
- 候选 IP 全都不通时仍然钉第一个（节点可能只是暂时抖动；留域名反而一定解析失败）。
- **DNS 也一起修**：`nameserver` / `fallback` / `proxy-server-nameserver` /
  `default-nameserver` 里只要全是 DoH/域名、没有一个 IP 字面量，就换成本机可用的
  `223.6.6.6 / 119.28.28.28 / 114.114.114.114 / 180.76.76.76`，保留 `fake-ip`、
  `fake-ip-filter`、`fallback-filter` 等其它设置。订阅自带不可达 DoH 时，症状是
  「github/google 能上，但 baidu/bilibili 全部 connection reset / dns resolve failed」，
  或者 SSH/长连接卡在 banner exchange。

`--dns <ip[,ip]>` 可覆盖解析服务器；`MIHOMO_TUI_DNS` 环境变量同效。

> 订阅自带的 `dns.nameserver` 若是一组在当前网络不可达的 DoH，会导致国内站
> `dns resolve failed`（浏览器表现是 `PR_END_OF_FILE_ERROR` / connection reset）。
> 上面的自动修正会处理这种情况；用 `--no-compat` 则会原样保留。

- `--compat`：强制开启（即使这份配置看起来不需要）。
- `--no-compat`：关闭自动修正，整包原样落盘。本机不建议——域名解析会超时、非 safari
  指纹握手会被无视，症状就是「节点看起来都在，但测速全超时」。
- 修正只改 `server` 值和 `client-fingerprint` 值，注释、缩进、节点顺序、
  `proxy-groups` / `rules` 全部原样保留；块式与单行流式（`- {name: ..., server: ...}`）
  两种写法都支持。

改写示例：

```yaml
# 块式
  - name: 新加坡01
    server: sg.example.com
    client-fingerprint: chrome
# 流式（订阅常见，一行一个节点）
  - {name: 新加坡01, server: sg.example.com, client-fingerprint: chrome}
```

退出码：`0` 成功 / `2` 格式不对 / `3` 下载或读写失败 / `4` 在线验证失败 /
`5` 热重载失败（已回滚）。

## 离屏渲染（给 diff / CI 用）

```bash
mihomo-tui --snapshot out.txt --cols 80 --rows 24          # 演示数据，纯文本
mihomo-tui --snapshot - --cols 120 --rows 40 --ansi        # 带 ANSI 打到 stdout
mihomo-tui --snapshot - --live                             # 抓一次真实数据
```

`--snapshot` 不进 raw 模式、不启动后台线程；默认用固定演示数据，保证同尺寸下输出
逐字节稳定，`test/golden/` 就是据此做回归的。

## 代码结构

```
include/vaterm/         搬来的终端基础层（颜色、输入状态机、UTF-8、termios）
include/vatui.hpp       帧缓冲（双缓冲 + SGR 池）
include/vawk/widget/    新写的控件层
  rect.hpp              Rect + inset/split_h/split_v/intersect
  canvas.hpp            带裁剪栈的绘制面；宽字符跨边界处理；每段绘制绝对定位
  widget.hpp            控件基类（render / on_event / bounds / focused）
  panel.hpp             边框 + 标题 + badge + 内容区
  log_view.hpp          环形行缓冲 + follow + 滚动 + 过滤（自带锁）
  list_view.hpp         条目 + 选中 + 视口滚动 + 过滤 + 确认回调
  sparkline.hpp         方块字符双序列曲线，窗口内归一化
  segmented.hpp         横向分段控件（键盘 + 鼠标）
  focus.hpp             焦点链 + Tab/Shift-Tab + 命中测试
app/
  main.cpp              参数分发
  api.{hpp,cpp}         libcurl：GET/PUT/PATCH + NDJSON 流
  journal.{hpp,cpp}     fork/exec journalctl，管道 + 行拆包 + 级别推断
  traffic.{hpp,cpp}     /traffic 流 → 采样回调，断流重连
  dns.{hpp,cpp}         最小 DNS over TCP 客户端（A 记录）
  use.{hpp,cpp}         格式识别 / 行级改写 / 验证 / 备份 / 回滚
  app.{hpp,cpp}         四宫格布局、三个后台线程、输入分发、单帧渲染
test/
  test_widgets.cpp      组件单测
  test_use.cpp          识别/改写/落盘/回滚单测
  run_snapshots.sh      golden 对比
```

线程模型：主线程渲染 + `input::waitInput(30)`；journal、traffic、stats 各一个后台
线程，只往带锁的共享结构里写，主线程按需取。日志视图的环形缓冲自带 mutex。
渲染是「数据变了才画」，不是固定帧率烧 CPU；`--fps` 可看帧耗时和写入单元格数。

帧缓冲的每个绘制段（文本、填充、边框）都会打一个 anchor：输出时强制重新定位光标。
这样即使某个 emoji/国旗的实际渲染宽度和估算差一格，也不会把同一行后面的右对齐延迟、
右侧边框带歪。国旗（两个 regional indicator）按 2 列计算，与终端渲染一致。

## 已知限制

- 只支持 Linux（journalctl / termios / `systemctl --user`）。
- 不做订阅自动更新、多内核、流量图表。
- `--use` 的在线校验依赖内核在跑；内核没起来时用 `--offline`（风险自负）。
- 组件层刻意没做：浮动窗/z 序、文本输入框、表格、主题系统 —— 需要时再加。

## License

GPL-3.0。`include/vaterm/`、`include/vatui.hpp` 与 `src/vaterm/`、`src/vatui.cpp`
来自 VAWK 项目，同样以 GPL-3.0 授权。
