# Ubuntu / Codex 接手说明

## 用户目标

用户希望在 Ubuntu 没有 MyASUS 的情况下，逐步获得相似的 OLED 保护体验。第一步是本项目的像素位移和常驻控制；不是一次性重写所有 MyASUS 功能。Windows 原型已完成，此次发布只准备 Ubuntu 接手材料，**尚未实现 Linux 后端**。

目标笔记本是 ASUS ProArt P16 H7606WI，3840×2400 内屏。Ubuntu 上的显示器、缩放、驱动及桌面会话必须现场探测，不能照搬 Windows 设备名 `\\.\DISPLAY1`。使用普通桌面用户运行。

## 当前实现与证据

原版 AsusOLEDShifter 1.0.21.0 是原生 Windows PE。REA 确认身份与元数据；本机未配置其原生反编译引擎。Craft Anything 的 built-in Deep 引擎提供了静态反汇编、字符串交叉引用和九点常量表。

已确认的默认值：200 秒；半径 1；九点依次为：

```text
(0,0), (1,0), (1,1), (0,1), (-1,1),
(-1,0), (-1,-1), (0,-1), (1,-1)
```

Windows 原型按端点往返方式使用这些点，壁纸每次从原图重绘，图标层从初始坐标计算偏移。没有证明原版所有机型/分支都使用完全相同的图像处理或 FSE 激活规则。细节与证据 ID 见 `analysis/BEHAVIOR.md`。

Windows 代码无第三方 NuGet 包；使用 WinForms、Win32、COM、GDI+。11 项自测覆盖轨迹、渲染、恢复、手动移动和最小化；这些不是 Ubuntu 的集成测试。

## 建议分阶段交付

### 1. 可在 Linux 构建的核心

提取轨迹、节流/暂停状态机、配置验证和恢复所有权策略。使用自己的整数坐标类型，避免 UI/绘图库侵入核心。新增 Ubuntu 可执行的 headless 测试；保持原 Windows 入口可构建。用平台接口区分壁纸、图标层、窗口、面板和会话事件。

### 2. Ubuntu 常驻入口

先检测 Ubuntu、桌面版本和 X11/Wayland；提供状态、启停、间隔/半径和诊断。选择实际可用的状态指示器/托盘或 GNOME 面板入口，保证没有托盘扩展时也能打开控制界面。配置使用 XDG 目录；登录启动 opt-in，提供卸载步骤。

### 3. 桌面后端

**GNOME / Wayland：** 不要把 Windows 的 `SetWindowPos` 翻译成 xdotool。标准 Wayland 客户端没有通用的全局窗口操控能力。可先评估独立 GNOME Shell 扩展：在 compositor 内对合适的背景/窗口 actor 做可逆变换，并按 GNOME 版本验证。GNOME 扩展会成为 Shell 的一部分，故需特别检查 disable、锁屏和崩溃后的还原。不能把整个 compositor framebuffer 的位移当作已获得的能力。若改用重绘壁纸的方案，明确只保护壁纸，并尊重暗色/亮色背景和幻灯片设置。

**X11：** 可以评估 X11/EWMH 对白名单窗口的有限位移，但要检查 WM 的限制、全屏/最大化、多屏及缩放。不要把所有窗口强制取消最大化。图标可能属于 GNOME Desktop Icons 扩展或其他桌面进程，不能假设存在 Windows 的统一图标层。

**KDE / 其他桌面：** 明确分开后端，探测 capabilities。不能让 GNOME 的设置键或 Shell API 在其他桌面上被静默调用。

任务栏在 Ubuntu 对应的可能是 Ubuntu Dock、GNOME 顶栏或其他面板。它们属于不同组件，自动隐藏应是可选能力，不应为本工具安装而覆盖用户配置。

### 4. 实机验证与发布

至少验证：缩放前后真实物理偏移、负坐标双屏、插拔屏幕、锁屏、休眠/恢复、退出、强杀后重启、用户主动更换背景、后台空闲资源、扩展 disable/uninstall 的完整清理。测试不得无提示改变用户的工作桌面；先用专门测试会话或明确选择的 fixture。

初期支持矩阵应明确写出“Ubuntu 版本 + GNOME/KDE 版本 + X11/Wayland”，未测试组合标为未验证。不要笼统宣称“Linux 全支持”或量化防烧屏效果。

## 上线前不变条件

1. 位移有界，不累积漂移，禁用/退出恢复。
2. 恢复记录只能还原本工具仍持有的状态，不覆盖用户后续操作。
3. 没有 root 常驻进程，不关闭 Wayland 安全约束，不批量修改其他服务。
4. 不改 ICC 配置、屏幕校色或 GPU 驱动；不声称提供面板硬件补偿。
5. 默认 200 秒/1 像素；调参和登录启动由用户选择。
6. 不支持的功能在界面和文档中明确显示。

## 可直接给 Ubuntu Codex 的请求

> 阅读 AGENTS.md、docs/UBUNTU_PORT.md 和 analysis/BEHAVIOR.md。检查我的 Ubuntu 桌面环境、版本、会话类型和显示缩放。在保留 Windows 实现的前提下，先提取可跨平台测试的核心，再实现本机可用的 Ubuntu 常驻界面和像素位移后端。对 Wayland、桌面图标和面板的限制如实报告，分阶段交付实际可运行的能力。保留 GPL-3.0-only。

## 参考

- [GNOME Shell 扩展开发与架构](https://gjs.guide/extensions/overview/architecture.html)
- [创建 GNOME Shell 扩展](https://gjs.guide/extensions/development/creating.html)
- [Wayland / xdg-shell 协议](https://wayland.app/protocols/xdg-shell)

这些是后续适配入口，不代表已验证某个特定 Shell actor 或扩展方案适用于用户的 Ubuntu 版本。
