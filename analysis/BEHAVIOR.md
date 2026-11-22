# AsusOLEDShifter 1.0.21.0 静态分析与实现对照

分析日期：2026-11-22。目标是本机 MyASUS 4.0.82.0 安装包中的 `ModuleDll/HWSettings/AsusOLEDShifter.exe`。

SHA-256：`017ecf78ea961946df4c90c12a3f2a28ae8c714c1e0454f8175823e1e09efb60`，456112 字节，PE x86-64。

## 使用的工具

- REA：`open_binary`、`inspect_managed_artifact`。确认文件身份及其为原生 PE，无 CLI/.NET 元数据。完整证据见 `rea-evidence.json`。本机没有 Ghidra/IDA，REA 的原生搜索返回 provider_unavailable；没有将该失败当成分析成功。
- Craft Anything 0.6.1：用户指定程序使用 per-call `--deep`，built-in 引擎读取字符串、交叉引用、反汇编和常量表。没有修改全局 Clean room 设置，没有执行被分析程序。项目状态保存在 `asus.cany`；导出证据见 `craft-evidence.json`。
- 没有仅根据字符串给出精确算法：进一步追踪了配置读取、SetTimer 参数、默认值初始化、路径生成器和 SetWindowPos 调用。

## 已确认或推导的行为

| 行为 | 静态证据 | 替代实现 |
|---|---|---|
| 默认间隔 200 秒 | `sub_14000bbf0` 在 `0x14000bd09` 初始化 0xc8；`sub_14000da20` 从配置读 ShiftInterval 后在 `0x14000ddb1` 乘 1000 传入 Win32 SetTimer | 默认 200 秒，可调 |
| 壁纸/图标最大偏移默认 1，九点范围 | 初始化成员 +0x2a0 / +0x2c8 均为 1；生成函数以 (2r+1)^2 控制长度 | 半径默认 1，可调 |
| 方形螺旋九点 | `sub_140011b90` / `sub_140011cb0` 生成路径；`0x140056f60` 常量表直接为 (0,0),(1,0),(1,1),(0,1),(-1,1),(-1,0),(-1,-1),(0,-1),(1,-1) | 相同九点次序，端点往返 |
| 位移壁纸 | `sub_140011010`、GetTargetMonitorWallpapers、Origin-/Shift-图片路径、IDesktopWallpaper 调用 | 按原图片与偏移重新渲染静态壁纸，边缘环绕；不复用华硕图像处理代码 |
| 图标和白名单窗口用 SetWindowPos 移动 | `sub_1400115f0` `0x14001177b`、`0x140011896` 等 | 图标层 + 严格限定的窗口 |
| 任务栏自动隐藏 | `sub_14000c5d0` 调 SHAppBarMessage(ABM_SETSTATE)，传 1/2 状态 | 作为可选项实现，保留原始状态并恢复 |
| FSE 白名单、九点位移、同一秒级间隔 | `sub_14000bbf0` 白名单、`sub_14000cb70` 匹配、`sub_14000d490` SetWindowPos 与 ShiftInterval*1000 | 按相同类名/身份提供有限支持，未验证所有系统版本 |
| 休眠停止、唤醒重启定时器 | 程序中存在明确处理日志与相关路径；未进行实际休眠验证 | Win32 SystemEvents 电源事件，暂停还原、恢复重新计时 |

关键 Craft 证据：

- `ev_2eaa6692a3832484a7f1d85eb709627250c6ef551764223ff56a89aa6410608c`：默认值和 FSE 白名单。
- `ev_d83e91c9aade0eb36ded119ada0c927347f520ab778cbdea440327e309354155`：配置读取、定时器单位、启停流程。
- `ev_7ef04df2e56d9a3687ca7d2efbbe7506a6b70ae78ec9bb718c38b734fc0cb362`：九点常量表。
- `ev_542f6a64fc0a8faf44931bdf0d51d70e18b6699fe91bce7a4276751130a4e3fa`：FSE 位移线程。
- 其他函数的证据 ID 见 `evidence-index.json`。

## 不能据此声称的结论

静态观察不证明某分支在本机实际运行过，也没有完整恢复其所有异常、机型判断、Windows FSE 状态检测或原图处理分支。因此本项目是核心功能兼容实现，不是逐指令克隆。任务栏默认选项、边缘处理、FSE 激活检测存在独立设计。

未实测华硕原版在 HDR、幻灯片、跨屏、远程桌面、多个不同 DPI 屏幕、Xbox FSE 等环境下的全部行为。本项目明确跳过不支持的壁纸模式。没有绕过任何目标执行确认或改变用户桌面来取得证据。

## 增强与测试

独立恢复日志、窗口 HWND/PID/进程启动时间身份检查、外部修改所有权检查、单实例、原版冲突暂停是替代实现的保护机制，不能归因于原版。

`--self-test` 已验证轨迹覆盖/边界/不累积、壁纸逐像素环绕、填充/适应布局、恢复记录序列化、实际 Win32 测试窗口移动及恢复、重启恢复、尊重用户后续移动、COM 只读访问。`--smoke-test` 验证托盘和高 DPI 设置界面。
