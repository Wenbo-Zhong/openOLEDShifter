# Repository guidance

## Read first

- `README.md`: implemented capabilities and limits.
- `analysis/BEHAVIOR.md`: static evidence from the original ASUS Windows executable; this is NOT Linux runtime evidence.
- `docs/UBUNTU_PORT.md`: the user's Ubuntu migration objective, milestones, and acceptance checks.

## User objective

Build a usable, open-source OLED shifting experience on Ubuntu without MyASUS. Windows and GNOME Wayland backends now exist; preserve both while improving shared code. Do not silently expand the task to BIOS, fan control, undervolting, calibration, or panel firmware compensation.

## License and sources

This project is GPL-3.0-only. Keep compatible licensing and the source notices. Do not import ASUS executables, raw decompiled source, personal wallpaper files, registry exports, logs, credentials, or proprietary assets. Original binary analysis artifacts remain local and ignored; the checked-in behavior note is the portable summary.

## Implementation boundaries

- `NativeApp/core/`: shared C++ policy and localization without desktop dependencies.
- `NativeApp/app/`: shared settings contract, command execution/queue, wxWidgets settings and UI text.
- `NativeApp/platform/windows/`, `NativeApp/platform/linux/`: platform entry points, tray and system operations.
- `NativeApp/tests/`, `NativeApp/tools/`: native tests and desktop policy generator.
- `Linux/extension/`, `Linux/plasma-wallpaper/`: compositor-specific wallpaper integrations.
- `Linux/shared/`, `Linux/packaging/`: common Linux shift runtime and packaging resources.
- `translations/`: shared language files.
- `assets/`: project icon and generator. Keep the existing icon unless requested.

Read `docs/ARCHITECTURE.md`. Keep system operations in their respective backends. Generate trajectory, timing and defaults for desktop plugins from the shared C++ policy.

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

Native: run CTest from the CMake build directory. On Windows, run `./build.ps1 -WxSource <path>` and then `release/windows-x64/OpenOLEDShifter.exe --smoke-test`. Tests may move their own fixture windows only.

Ubuntu: record the actual distribution, GNOME/KDE version, `XDG_SESSION_TYPE`, display scales and monitor layout before selecting a backend. Build headless tests for the reusable core first. Run visible desktop integration tests in an explicitly chosen test session/fixture, documenting their effects and cleanup. Never report a Windows-only test as proof of Linux support.
