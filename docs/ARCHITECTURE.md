# 共享框架与平台边界

Ubuntu 打包入口使用 `NativeApp/` 下的 **C++ + wxWidgets**。设置窗口通过 GIO 调用现有 GNOME 扩展的 D-Bus 接口；构建工具从 C++ 核心生成相同的轨迹、调度表和 GSettings 配置。GNOME 扩展继续负责壁纸位移。

两端的原生托盘共用 wxWidgets 设置窗口的命令队列。暂停／继续和立即位移与设置刷新串行执行，托盘操作保留窗口内尚未保存的编辑。Windows 退出时由原有进程清理流程还原桌面。Ubuntu 通过同一队列请求 GNOME 扩展暂停并还原后关闭窗口。

Windows 默认发布原生版，与 GNOME 共用 wxWidgets 设置窗口，经 `settings_backend.hpp` 连接各自后端。真实 Ubuntu 会话仍需验证状态读取、保存、暂停还原、手动位移和扩展重新启用后的连接恢复。

Windows 原生版支持静态壁纸、桌面图标层和任务栏自动隐藏。三个目标可独立开关。壁纸可选择主屏、所有活动屏幕或指定屏幕，每块屏幕按 Windows 报告的设备标识单独保存恢复记录。屏幕断开时保留该记录；Windows 重新识别到同一设备后，先检查并恢复仍属于本程序的壁纸，再开始新的位移周期。

原生版也支持按进程名选择应用窗口，屏幕选择同时适用于这些窗口。普通全屏活动默认暂停。可另行启用已识别的 Xbox、Armoury Crate SE 和 Explorer 全屏界面位移，使用一物理像素的轨迹。实际是否识别取决于 Windows 报告的窗口类和应用身份。

会话暂停和退出时分别检查并恢复各目标仍拥有的状态。最小化窗口可在保持最小化的情况下恢复其还原位置。图标层通过 Explorer 子窗口的位置、大小和进程身份判断是否仍可还原；任务栏只还原本程序启用且仍保持原样的自动隐藏设置。新旧版本共用单实例锁；旧版本仍有待恢复状态时，原生版会暂停。设置存放在 `%LOCALAPPDATA%\OpenOLEDShifterNative\settings.ini`。

原生版使用同一 C++ 工具链构建静态 wxWidgets 和应用。在 Windows x64 下从仓库根目录运行：

```powershell
cmake -S NativeApp -B artifacts/native-windows -DOPENOLED_BUILD_UI=ON -DOPENOLED_WX_SOURCE=C:/src/wxWidgets
cmake --build artifacts/native-windows --config Release --parallel
ctest --test-dir artifacts/native-windows -C Release --output-on-failure
```

wxWidgets 源目录需要包含子模块。用生成的 `OpenOLEDShifter.exe --smoke-test` 验证界面和托盘，测试会自行退出。日常运行时从托盘选择 **Exit and restore** 关闭程序。如发生异常退出，先重开同一个原生版本完成恢复，再用正式版。

Windows 和 Linux 构建都使用原生入口。Linux 软件包包含 GNOME 和 Plasma 插件。
