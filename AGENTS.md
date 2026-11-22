# Repository guidance

## Read first

- `README.md`: implemented capabilities and limits.
- `analysis/BEHAVIOR.md`: static evidence from the original ASUS Windows executable; this is NOT Linux runtime evidence.
- `docs/UBUNTU_PORT.md`: the user's Ubuntu migration objective, milestones, and acceptance checks.

## User objective

Build a usable, open-source OLED shifting experience on Ubuntu without MyASUS. Progressively replace this specific desktop protection function. The current implementation is Windows-only; preserve its working behavior while adding Linux support. Do not silently expand the task to BIOS, fan control, undervolting, calibration, or panel firmware compensation.

## License and sources

This project is GPL-3.0-only. Keep compatible licensing and the source notices. Do not import ASUS executables, raw decompiled source, personal wallpaper files, registry exports, logs, credentials, or proprietary assets. Original binary analysis artifacts remain local and ignored; the checked-in behavior note is the portable summary.

## Implementation boundaries

- `Program.cs`: Windows startup, settings/store and currently mixed-in trajectory logic.
- `Native.cs`: Win32 and COM APIs; must remain isolated from Linux code.
- `Engine.cs`: Windows desktop state changes, recovery and GDI+ rendering.
- `TrayApp.cs`: WinForms tray/settings and Windows session events.
- `SelfTests.cs`: Windows executable's self-test harness; not directly runnable on Ubuntu.
- `assets/`: original project icon; do not replace it during the port unless requested.

First extract and test reusable trajectory/configuration/recovery policy behind platform-neutral interfaces. WinForms, Registry, `System.Drawing.Common` bitmap operations, COM and `user32.dll` do not become Linux-compatible by changing the target framework. Use a small independently runnable Linux entry point instead of pretending the existing Windows EXE supports Ubuntu.

## Behavioral requirements

- Defaults: 200 seconds, one physical pixel, square spiral of nine positions with endpoint reversal. Make physical versus logical pixel handling explicit for scaled displays.
- Bound displacement; derive offsets from a stable original position, never accumulate unbounded drift.
- Restore only state still owned by the app. Respect a user's subsequent wallpaper/window changes.
- Save recovery intent atomically before mutation. Handle crashes, shutdown, lock, suspend, display changes and session restart.
- Do not require root or alter global security policy. Do not disable Wayland protections or use unsafe Shell evaluation as a workaround.
- If a desktop backend cannot implement something, report it as unsupported rather than silently claiming protection.
- Do not automatically remove/disable the user's existing desktop extensions or protection tools. Avoid running two shifters at once.
- Login startup is opt-in; uninstall/disable must restore modified desktop state and remove only files owned by this app.

## Validation

Windows: `./build.ps1`, then `release/OpenOLEDShifter.exe --self-test` and `--smoke-test`. Tests write to `artifacts/`; self-tests may create/move their own fixture window but must not shift the user's desktop.

Ubuntu: record the actual distribution, GNOME/KDE version, `XDG_SESSION_TYPE`, display scales and monitor layout before selecting a backend. Build headless tests for the reusable core first. Run visible desktop integration tests in an explicitly chosen test session/fixture, documenting their effects and cleanup. Never report a Windows-only test as proof of Linux support.
