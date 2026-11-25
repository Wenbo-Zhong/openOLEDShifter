# 共享框架与平台边界

Ubuntu 打包入口使用 `NativeApp/` 下的 **C++ + wxWidgets**。设置窗口通过 GIO 调用现有 GNOME 扩展的 D-Bus 接口；构建工具从 C++ 核心生成相同的轨迹、调度表和 GSettings 配置。GNOME 扩展继续负责壁纸位移。

两端的原生托盘共用 wxWidgets 设置窗口的命令队列。暂停／继续和立即位移与设置刷新串行执行，托盘操作保留窗口内尚未保存的编辑。Windows 退出时由原有进程清理流程还原桌面。Ubuntu 通过同一队列请求 GNOME 扩展暂停并还原后关闭窗口。

Windows 正式发布仍使用 **Avalonia 12 / .NET 8**。原生预览与 GNOME 共用 wxWidgets 设置窗口，经 `settings_backend.hpp` 连接各自后端。真实 Ubuntu 会话仍需验证状态读取、保存、暂停还原、手动位移和扩展重新启用后的连接恢复。

Windows 原生预览支持静态壁纸、桌面图标层和任务栏自动隐藏。三个目标可独立开关。壁纸可选择主屏、所有活动屏幕或指定屏幕，每块屏幕按 Windows 报告的设备标识单独保存恢复记录。屏幕断开时保留该记录；Windows 重新识别到同一设备后，先检查并恢复仍属于本程序的壁纸，再开始新的位移周期。

原生版也支持按进程名选择应用窗口，屏幕选择同时适用于这些窗口。普通全屏活动默认暂停。可另行启用已识别的 Xbox、Armoury Crate SE 和 Explorer 全屏界面位移，使用一物理像素的轨迹。实际是否识别取决于 Windows 报告的窗口类和应用身份。

会话暂停和退出时分别检查并恢复各目标仍拥有的状态。最小化窗口可在保持最小化的情况下恢复其还原位置。图标层通过 Explorer 子窗口的位置、大小和进程身份判断是否仍可还原；任务栏只还原本程序启用且仍保持原样的自动隐藏设置。新旧版本共用单实例锁；旧版本仍有待恢复状态时，预览版会暂停。预览设置存放在 `%LOCALAPPDATA%\OpenOLEDShifterNative\settings.ini`。

原生预览使用同一 C++ 工具链构建静态 wxWidgets 和应用。在 Windows x64 下从仓库根目录运行：

```powershell
cmake -S NativeApp -B artifacts/native-windows -DOPENOLED_BUILD_UI=ON -DOPENOLED_WX_SOURCE=C:/src/wxWidgets
cmake --build artifacts/native-windows --config Release --parallel
ctest --test-dir artifacts/native-windows -C Release --output-on-failure
```

wxWidgets 源目录需要包含子模块。用生成的 `OpenOLEDShifter.exe --smoke-test` 验证界面和托盘，测试会自行退出。日常运行时从托盘选择 **Exit and restore** 关闭程序。如发生异常退出，先重开同一个原生版本完成恢复，再用正式版。

下面记录现存 C# 架构，供迁移 Windows 其余功能时参考。

## 依赖方向

```text
Windows 入口 / Win32、COM、Registry 后端 ─┐
                                      ├─ Desktop（共用 Avalonia 库）─ Core（纯 net8.0）
Ubuntu 入口 / GJS、D-Bus 后端 ──────────┘
         │
         └─ GNOME Shell 扩展（compositor 内执行，保留 GJS）
```

两个入口都通过 ProjectReference 使用同一个 `OpenOLEDShifter.Desktop` 和 `OpenOLEDShifter.Core`，不再复制或链接一份共享源文件到每个可执行程序。

| 层 | 文件 | 职责 |
|---|---|---|
| Core | `ShiftPolicy.cs`、`ShiftSchedule.cs` | 默认参数、轨迹、往返、可复用计时/暂停策略 |
| Core | `DesktopBackend.cs` | 设置模型、输入校验、能力描述、后台接口、命令串行化、原子 JSON 写入 |
| Desktop | `DesktopApp.cs`、`SettingsWindow.cs`、`SettingsSession.cs` | 主题、图标、设置控件、轮询/重连、状态反馈、未保存编辑保护 |
| Windows | 根目录 `Program.cs`、`TrayApp.cs`、`Engine.cs`、`Native.cs`、`WindowsDisplay.cs` | 启动、托盘生命周期、系统事件、显示器、桌面变更、持久化恢复、开机启动 |
| Ubuntu | `Ubuntu/Desktop/GnomeBackend.cs` | 调用现有 GJS/D-Bus 桥，5 秒超时及取消清理 |
| Ubuntu | `Ubuntu/extension/` | GNOME Shell 内的壁纸 actor、锁屏/全屏事件和扩展生命周期 |

## 平台能力驱动界面

共享设置窗口只读取 `DesktopCapabilities`。Windows 后端声明图标、任务栏、FSE、显示器选择等能力；GNOME 后端声明当前实际支持范围。界面不通过“有没有显示器列表”猜操作系统。

新平台实现 `IDesktopBackend` 的状态、应用设置、暂停、单次位移四种命令即可接入同一套 UI。`BackendConnection` 将轮询和显式操作串行化，防止设置被一个较早的状态请求覆盖。刷新不会覆盖用户未保存的编辑；关闭窗口取消排队及正在进行的连接。

## 为什么 GNOME 扩展仍用 JavaScript

GNOME 的壁纸 actor 属于 Shell/compositor 进程，不能由普通 C# Wayland 客户端直接操控。扩展负责在该进程内执行，这是实际的平台差异，不为增加表面的 C# 代码比例而改成外部进程。

Ubuntu 构建工具直接引用 Core，为扩展生成相同的轨迹、调度规则、参数范围和 GSettings 默认值。Windows 直接读取核心的调度表，GNOME 读取构建生成的同一份规则。各后端提供时间和暂停原因，再按规则决定何时还原、位移或重置计时。两端恢复机制也不同：Windows 必须持久化恢复日志，GNOME actor 随 Shell 生命周期销毁。不要把它们强行合并为同一套系统调用。

GDI+ 图像渲染仅保留在 Windows 后端，使用 `System.Drawing.Common`；共享 Core 和 Desktop 不引用它或 Windows Desktop Framework。当前 GNOME 版平移原有壁纸 actor，不重绘图片。

## 验证

```sh
# 无 GUI、无平台依赖，可在 Windows / Ubuntu 上运行
dotnet run --project Tests/CoreTests.csproj -c Release

# Windows / Ubuntu 入口构建
dotnet build OpenOLEDShifter.csproj -c Release
dotnet build Ubuntu/Desktop/Desktop.csproj -c Release
```

Windows 桌面再执行 `release/windows-x64/OpenOLEDShifter.exe --self-test` 和 `--smoke-test`。测试夹具是本项目自己的 Win32 窗口，不接管用户桌面。

Ubuntu 必须另外在真实 GNOME 会话中验证 D-Bus、compositor actor、缩放、锁屏等行为。Windows 上构建 Ubuntu 入口和运行纯逻辑测试不代表完成 Linux 桌面验证。

2026-11-23：本次重构在 Windows 上通过两个入口的 Release 构建（零警告、零错误）、7 组共享核心测试、Windows 发布版 11 项自测及界面冒烟测试，并核对两端设置页截图。本次没有运行真实 Ubuntu/GNOME 桌面集成测试。
