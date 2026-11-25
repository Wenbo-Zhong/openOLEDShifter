# OpenOLEDShifter

适用于 Windows 和 Ubuntu 的 OLED 像素位移工具，针对 ASUS ProArt P16 的 AsusOLEDShifter 1.0.21.0 做静态分析后独立实现。无需华硕软件、管理员权限或内核驱动。

**Windows x64** 提供托盘和桌面位移功能。**Ubuntu 26.04 的 GNOME 50 Wayland** 提供壁纸位移扩展，支持多屏及整数、125% 和 150% 缩放。两端默认使用 200 秒间隔和九点往返轨迹。

Ubuntu 打包入口和 Windows 原生预览均使用 **C++ + wxWidgets**。Windows 正式发布仍由 Avalonia 和 .NET 8 提供完整桌面功能。[架构与迁移状态](docs/ARCHITECTURE.md)

原生界面跟随系统语言和浅色／深色偏好。简体中文、繁体中文和英文可自动识别，其他语言使用英文。设置窗口支持高 DPI 和滚动布局。

翻译文案按语言保存在 [translations](translations/README.md)。新增语言只需提交对应的翻译文件。

![OpenOLEDShifter icon](assets/app-icon.png)

协议：[GNU GPL v3.0 only](LICENSE)。Ubuntu 上的 Codex 请先阅读 [AGENTS.md](AGENTS.md) 和 [Ubuntu 适配接手说明](docs/UBUNTU_PORT.md)。

## Linux 自动打包

向 `main` 推送、提交 Pull Request、推送 `vX.Y.Z` 标签或手动运行工作流时，GitHub Actions 自动构建 x64 的 DEB 和 RPM。标签构建使用标签中的版本号。在对应运行的 **Artifacts** 中下载 `openoledshifter-linux-x64`，解压即可得到两个安装包。[GitHub Actions](https://github.com/Wenbo-Zhong/openOLEDShifter/actions)

DEB 面向 Ubuntu 26.04。RPM 面向使用 GNOME 50 Wayland 的 Fedora 44，可用 `sudo dnf install ./openoledshifter-0.1.0-1.x86_64.rpm` 安装。两种包都注册 **OpenOLEDShifter** 应用菜单入口。系统加载扩展后，按下面的步骤启用壁纸位移。

本地可用 `./build-ubuntu.sh all` 构建两个包，或用 `./build-ubuntu.sh rpm` 单独构建 RPM。RPM 构建额外需要 `rpmbuild`（Ubuntu 的 `rpm` 包）。

## Ubuntu 安装与使用

用 `./build-ubuntu.sh` 构建。在 Ubuntu x64 上生成 `release/ubuntu/openoledshifter_0.1.0_amd64.deb`。构建前安装工具和头文件：

```sh
sudo apt install cmake g++ pkg-config libwxgtk3.2-dev libgtk-3-dev libglib2.0-dev libjson-glib-dev libayatana-appindicator3-dev gjs libglib2.0-bin desktop-file-utils
./build-ubuntu.sh
```

```bash
sudo apt install ./release/ubuntu/openoledshifter_0.1.0_amd64.deb
```

安装后，在 GNOME 应用面板搜索 **OpenOLEDShifter** 打开设置。首次使用时注销并重新登录，让 GNOME 加载扩展，然后以桌面用户身份启用它：

```bash
gnome-extensions enable openoledshifter@openoledshifter
```

从应用面板或 OpenOLEDShifter 托盘菜单打开设置，勾选 **启用定时位移** 并保存。Ubuntu 已提供 AppIndicator 托盘支持。关闭设置窗口会收起到系统托盘，托盘菜单可重新打开设置或退出。启用 OpenOLEDShifter 扩展后，托盘也可控制暂停、继续和立即位移。登录自动开始默认为关闭。勾选「登录时启动位移和托盘」并保存后，应用会在下次登录时在托盘启动，扩展会开始定时位移。从旧版升级时，已勾选过这一项的用户需要重新保存一次以登记托盘启动。

Ubuntu 版支持 GNOME 50 Wayland 多屏本地静态壁纸位移，支持整数、125% 或 150% 缩放。锁屏、休眠和活动概览期间暂停。全屏暂停默认开启，可在设置中关闭。桌面图标、Dock、应用窗口和其他分数缩放档暂不支持。

卸载前，在设置里关闭「登录时启动位移和托盘」并保存，再停用扩展以恢复原位并移除软件包：

```bash
gnome-extensions disable openoledshifter@openoledshifter
sudo apt remove openoledshifter
```

如需清除本扩展的个人设置，执行 `dconf reset -f /org/gnome/shell/extensions/openoledshifter/`。如曾通过旧版 `install-ubuntu.sh` 做用户级安装，先用旧版脚本的 `--uninstall` 选项移除，以免旧入口覆盖软件包提供的入口。

## Windows 使用

从 GitHub Actions 下载 **OpenOLEDShifter-win-x64** 并双击解压出的 `OpenOLEDShifter.exe` 打开设置。EXE 可单独移动，运行时已内置，无需另外安装 .NET。也可以通过托盘图标打开设置。关闭设置窗口后仍在托盘运行，右键选择“退出并恢复”结束。

原生预览由 **OpenOLEDShifter-native-win-x64** 构建产物提供。它使用静态链接的 wxWidgets，EXE 不含 .NET。新旧版本共用单实例锁。试用前请从旧版托盘选择退出并恢复，再打开预览版。预览版首次使用需手动启用位移并保存设置。

原生版的「登录 Windows 时自动启动」开关即时生效。开启时，程序会将 EXE 放到 `%LOCALAPPDATA%\Programs\OpenOLEDShifter` 并登记登录启动；关闭时移除登记项。要让位移也自动开始，请另行勾选「启用定时位移」并保存。

检测到 `AsusOLEDShifter.exe` 时会自动暂停，避免两套工具叠加。在 MyASUS 中关闭原版像素位移后，可通过本工具启用；本工具不会停止或禁用任何华硕服务。如果原版后台仍存在，会继续保持暂停。

默认每 200 秒切换一次位置、最大偏移 1 个物理像素，沿九点方形螺旋往返。可设置 10–86400 秒、1–8 像素。每次从原始位置计算，不累积偏移。

- 静态壁纸：选定屏幕或所有屏幕，保留原有居中、拉伸、适应、填充方式。对原图重新渲染、边缘环绕，不覆盖原始图片。
- 桌面图标：移动 Explorer 的图标视图层，不改动每个图标的位置。此层跨所有显示器，不能只对一块屏幕生效；多屏首次使用默认关闭该选项。
- 任务栏：可选自动隐藏，暂停和退出时恢复启动前状态。
- FSE：按原版静态白名单识别部分 Xbox / Armoury Crate SE / Explorer 全屏界面，识别成功时位移；不同 Windows 版本的窗口结构可能导致不匹配。
- 额外应用：可输入进程名，只移动普通可见窗口；不移动这些应用的最大化或全屏窗口。此项是增强功能，默认留空。
- 普通全屏应用、锁屏、休眠自动暂停并还原；恢复后重新计时。
- 退出还原；下次启动读取恢复记录，处理上次异常结束留下的位移。
- 恢复只针对仍属于本工具的状态。用户已经换过壁纸、移动/调整窗口时，不强行覆盖。
- 可选登录启动，默认关闭。配置、日志和恢复记录位于 `%LOCALAPPDATA%\OpenOLEDShifter`。

## 范围

这是相同用途和核心能力的独立实现，不声称与华硕闭源程序在所有机型/版本上逐条等效。分析来自静态反汇编，未运行原程序做动态挂钩，也未关闭本机原版组件。

不包含 OLED 面板硬件补偿、像素刷新屏保、亮度控制或校色；位移也不能保证消除烧屏。

目前不移动平铺、跨屏（Span）、幻灯片和第三方动态壁纸。位图重新渲染不是 HDR 色彩管线的逐位复现。FSE 的打包应用身份匹配依赖 Windows 实际返回的 AppUserModelID；此机没有对应界面可供完整实测。

任务栏、壁纸和 Explorer 图标层属于桌面共享状态。崩溃或直接强杀后可能暂留偏移，重新启动本工具将尝试还原。恢复记录中的原始壁纸文件需要继续存在。

## Windows 构建和测试

本地构建要求 Windows 10/11 x64 和 .NET 8 SDK。发布脚本生成 Windows x64 单文件 EXE。首次启动时，程序会将内置的本机库解包到系统临时目录。现有设置和恢复记录仍存放在用户目录。

```powershell
.\build.ps1
dotnet run --project Tests/CoreTests.csproj -c Release
.\release\windows-x64\OpenOLEDShifter.exe --self-test
.\release\windows-x64\OpenOLEDShifter.exe --smoke-test
```

`--self-test` 仅移动自身创建的测试窗口、用合成图片测试渲染，不修改用户壁纸、任务栏、桌面图标或华硕设置。结果写入当前目录的 `artifacts\tests.txt`。

`--smoke-test` 创建托盘和设置窗口，渲染 `artifacts\settings-preview.png` 后退出，不启用实际位移。

参见 [静态分析记录](analysis/BEHAVIOR.md)。Windows 初始验证为 11 项自测通过和一次发布包 UI smoke test；测试输出位于本地 `artifacts/tests.txt`，不提交到仓库。本项目独立编写，不依赖或分发华硕可执行文件。

## 开源与贡献

代码、图标及项目文档以 **GPL-3.0-only** 发布，完整条款见 `LICENSE`。Avalonia UI 及其运行时依赖、.NET 均保留各自许可证。图标生成脚本需要 Pillow，但生成的图标已提交，正常构建不需要安装 Python/Pillow。

欢迎提交 Ubuntu、其他 Linux 桌面和 Windows 改进。新增后端需要明确桌面环境、会话类型（X11/Wayland）、支持范围和还原行为，不能只在设置里增加开关而没有实际实现。
