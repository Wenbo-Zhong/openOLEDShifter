# 共享框架与平台边界

Ubuntu 打包入口使用 `NativeApp/` 下的 **C++ + wxWidgets**。设置窗口通过 GIO 调用 GNOME 扩展或 Plasma 壁纸的 D-Bus 接口；构建工具从 C++ 核心生成桌面插件共用的轨迹、调度表和默认值，以及 GNOME 的 GSettings 配置。两个桌面插件各自负责壁纸位移。

各平台的原生托盘共用 wxWidgets 设置窗口的命令队列。暂停／继续和立即位移与设置刷新串行执行，托盘操作保留窗口内尚未保存的编辑。Windows 退出时由原有进程清理流程还原桌面。Linux 通过同一队列请求当前桌面后端暂停并还原后关闭窗口。

Windows 默认发布原生版，与 GNOME、Plasma 共用 wxWidgets 设置窗口，经 `NativeApp/app/settings_backend.hpp` 连接各自后端。真实 Ubuntu GNOME/Plasma 会话仍需分别验证状态读取、保存、暂停还原、手动位移和插件重新加载后的连接恢复。

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

源码按职责组织，CMake 和根目录构建脚本仍是统一入口：

```text
NativeApp/
├── core/                 位移策略、默认值、调度与翻译解析
├── app/                  设置契约、命令执行与队列、wxWidgets 设置窗口
├── platform/
│   ├── windows/          Windows 入口、托盘、配置、登录启动与桌面操作
│   └── linux/            Linux 入口、托盘、公共工具与 GNOME/Plasma 后端
├── tests/                无桌面核心/命令测试与 Windows 桌面测试
└── tools/                生成桌面插件使用的策略数据
Linux/
├── extension/            GNOME Shell 扩展
├── plasma-wallpaper/     Plasma 壁纸插件
├── shared/               两个插件共用的 JavaScript 位移运行时
├── packaging/            DEB/RPM 与插件打包资源
└── translation-test.js   插件翻译校验
translations/             各端共用的语言文件
```

`core/shift_core.hpp` 是轨迹、调度和默认值的唯一来源；`tools/generate_policy.cpp` 为桌面插件生成数据。`core/localization.hpp` 解析共享翻译，`app/ui_text.*` 负责系统语言选择和 wxWidgets 文案。

`app/settings_commands.*` 通过 `settings_backend.hpp` 串行执行命令、保存托盘请求并安排三秒后的状态刷新。Windows 后端在 UI 线程同步调用，Linux 后端使用工作线程。`app/settings_frame.cpp` 负责控件布局、状态展示和未保存编辑的合并；后端完成结果保留命令来源和退出标记。

`platform/windows/windows_backend.cpp` 负责位移调度、暂停与还原编排。`windows_config.*` 独立负责 INI 读取、默认值/范围处理和原子保存；`windows_startup.*` 负责登录登记和登录用 EXE 安装。壁纸、显示器、图标层、任务栏和应用窗口操作继续在各自 `windows_*` 模块内实现。

`platform/linux/linux_main.cpp` 是 GNOME 和 Plasma 共用的应用入口、单实例和托盘。`linux_backend.*` 选择桌面后端、查找插件、读写带所有权标记的登录启动文件，使用 `Desktop` 类型区分桌面。`gnome_backend.cpp` 负责扩展启用、D-Bus 命令和响应校验；`plasma_backend.cpp` 负责壁纸设置与多屏命令分发。

桌面插件各自负责合成器操作，共用 `Linux/shared/` 的位移运行时。平台特有的 D-Bus 协议和恢复逻辑留在各自实现中。源码目录从 `Ubuntu/` 改为 `Linux/`，构建脚本名、`release/ubuntu` 输出目录和系统安装路径保持兼容。

无桌面环境可运行共享核心、命令队列和翻译校验：

```sh
cmake -S NativeApp -B artifacts/native-core
cmake --build artifacts/native-core --parallel
ctest --test-dir artifacts/native-core --output-on-failure
```

命令测试使用独立后端替身覆盖串行执行、工作线程输入快照、托盘优先级、刷新间隔、错误和退出清理，不操作用户桌面。真实桌面后端仍按各平台的验收步骤验证。
