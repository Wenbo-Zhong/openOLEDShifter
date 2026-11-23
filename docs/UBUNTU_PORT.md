# Ubuntu 移植与验证

## 用户目标

用户希望在 Ubuntu 没有 MyASUS 的情况下，逐步获得相似的 OLED 保护体验。第一步是本项目的像素位移和常驻控制；不是一次性重写所有 MyASUS 功能。当前已实现 GNOME 50 Wayland 单屏静态壁纸后端，安装步骤见 [README](../README.md#ubuntu-安装与使用)。

目标笔记本是 ASUS ProArt P16 H7606WI，3840×2400 内屏。Ubuntu 上的显示器、缩放、驱动及桌面会话必须现场探测，不能照搬 Windows 设备名 `\\.\DISPLAY1`。使用普通桌面用户运行。

## 当前实现与证据

原版 AsusOLEDShifter 1.0.21.0 是原生 Windows PE。REA 确认身份与元数据；本机未配置其原生反编译引擎。Craft Anything 的 built-in Deep 引擎提供了静态反汇编、字符串交叉引用和九点常量表。

已确认的默认值：200 秒；半径 1；九点依次为：

```text
(0,0), (1,0), (1,1), (0,1), (-1,1),
(-1,0), (-1,-1), (0,-1), (1,-1)
```

Windows 原型按端点往返方式使用这些点，壁纸每次从原图重绘，图标层从初始坐标计算偏移。没有证明原版所有机型/分支都使用完全相同的图像处理或 FSE 激活规则。细节与证据 ID 见 `analysis/BEHAVIOR.md`。

Windows 位移后端使用 Win32、COM、GDI+，监视器查询及测试夹具也直接使用 Win32；不再依赖 WinForms。桌面设置与托盘使用 Avalonia UI。两端通过项目引用使用相同的 Core / Desktop 库，接口与连接生命周期见 [架构说明](ARCHITECTURE.md)。Windows 的桌面自测不是 Ubuntu 的集成测试。

## 已实现

- `Core/` 保存共享默认参数、轨迹、设置契约、输入验证、连接串行化和原子保存。Windows 使用共享 C# 调度器，Ubuntu 的构建工具直接引用该库，为 GNOME 扩展生成轨迹和设置范围。Shell 内的 GLib 调度仍属平台实现。
- GNOME Shell 扩展只操作桌面壁纸 actor。物理偏移除以显示器的整数缩放系数后写入逻辑坐标。位移以零平移为基准。扩展只在实际平移仍匹配最后写入值时恢复，遇到其他组件改变平移时暂停该 actor。GNOME 更换壁纸时使用新 actor。
- 扩展在写入前记录恢复意图。该记录与图层状态都位于 Shell 进程内，退出或崩溃时由 compositor 一并销毁。壁纸设置和源文件无需持久化恢复。
- 顶部面板可按会话启停。两端共用 `Desktop/` 中的 Avalonia 设置窗口和输入校验。Ubuntu 窗口通过会话 D-Bus 读取状态并提交设置，GNOME 扩展负责调度与位移。扩展停用时释放 D-Bus 对象、定时器、监听器和图层平移。

## 验证环境

- 目标桌面为 Ubuntu 26.04.1 LTS、GNOME Shell 50.1、Wayland。Mutter 报告单个内屏 `eDP-1` 位于 `(0, 0)`，3840×2400 @ 60 Hz，200% 缩放。
- 使用同一系统上的独立 GNOME Shell Wayland 会话、私有 D-Bus 和临时设置目录，运行 1920×1200 虚拟显示器和合成壁纸。在 200% 下，对偏移前后截图的中央区域逐像素比较，确认向右移动 1 个输出像素，停止后区域恢复一致。还验证了换壁纸、停用扩展和设置窗口。测试会话退出后清理临时文件。
- Avalonia 迁移后，Ubuntu 自包含应用在本机 XWayland 会话的 200% 缩放下通过窗口渲染测试。隔离 GNOME 会话验证了安装、卸载、状态读取、设置提交、立即位移和暂停。
- 实体 OLED、实际锁屏和系统休眠、热插拔、分数缩放和其他桌面环境仍需验证。Windows 项目在 Ubuntu 上交叉编译通过。Windows 自测和托盘 smoke test 仍需 Windows 桌面。

## 后续范围

多屏和分数缩放需要单独验证不同缩放与坐标原点。桌面图标、面板和窗口各自属于不同桌面组件。X11 和 KDE 需要独立后端。现行扩展会限制到 GNOME 50，不会调整其他扩展或系统安全设置。

- [GNOME Shell 扩展开发与架构](https://gjs.guide/extensions/overview/architecture.html)
- [创建 GNOME Shell 扩展](https://gjs.guide/extensions/development/creating.html)
- [Wayland / xdg-shell 协议](https://wayland.app/protocols/xdg-shell)
