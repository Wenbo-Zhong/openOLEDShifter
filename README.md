# OpenOLEDShifter

Windows 托盘式 OLED 像素位移工具，针对 ASUS ProArt P16 的 AsusOLEDShifter 1.0.21.0 做静态分析后独立实现。无需华硕软件、管理员权限或内核驱动。

**当前可运行平台：Windows x64。Ubuntu 适配尚未实现。** 项目希望逐步补齐 Linux 上没有 MyASUS 时的 OLED 保护体验；本仓库先聚焦像素位移，不把其他硬件管理功能混在一起。

![OpenOLEDShifter icon](assets/app-icon.png)

协议：[GNU GPL v3.0 only](LICENSE)。Ubuntu 上的 Codex 请先阅读 [AGENTS.md](AGENTS.md) 和 [Ubuntu 适配接手说明](docs/UBUNTU_PORT.md)。

## 使用

运行 `release\OpenOLEDShifter.exe`，通过托盘图标打开设置。关闭设置窗口后仍在托盘运行，右键选择“退出并恢复”结束。

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

## 构建和测试

要求 Windows 10/11 x64、.NET 8 SDK；运行发布包要求 .NET 8 Desktop Runtime（本机已有）。

```powershell
.\build.ps1
.\release\OpenOLEDShifter.exe --self-test
.\release\OpenOLEDShifter.exe --smoke-test
```

`--self-test` 仅移动自身创建的测试窗口、用合成图片测试渲染，不修改用户壁纸、任务栏、桌面图标或华硕设置。结果写入当前目录的 `artifacts\tests.txt`。

`--smoke-test` 创建托盘和设置窗口，渲染 `artifacts\settings-preview.png` 后退出，不启用实际位移。

参见 [静态分析记录](analysis/BEHAVIOR.md)。Windows 初始验证为 11 项自测通过和一次发布包 UI smoke test；测试输出位于本地 `artifacts/tests.txt`，不提交到仓库。本项目独立编写，不依赖或分发华硕可执行文件。

## 开源与贡献

代码、图标及项目文档以 **GPL-3.0-only** 发布，完整条款见 `LICENSE`。项目当前没有引入第三方运行时 NuGet 依赖；.NET 本身保留其各自许可证。图标生成脚本需要 Pillow，但生成的图标已提交，正常构建不需要安装 Python/Pillow。

欢迎提交 Ubuntu、其他 Linux 桌面和 Windows 改进。新增后端需要明确桌面环境、会话类型（X11/Wayland）、支持范围和还原行为，不能只在设置里增加开关而没有实际实现。
