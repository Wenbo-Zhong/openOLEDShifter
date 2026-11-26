// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "windows_wallpaper.hpp"
#include "windows_storage.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <objidl.h>
#include <gdiplus.h>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <system_error>

namespace oled::win {
namespace fs = std::filesystem;
namespace {
void check(HRESULT status, const char* operation) {
    if (FAILED(status))
        throw std::system_error(static_cast<int>(status), std::system_category(), operation);
}

void check(Gdiplus::Status status, const char* operation) {
    if (status != Gdiplus::Ok)
        throw std::runtime_error(std::string(operation) + ": GDI+ " + std::to_string(status));
}

struct Imaging {
    ULONG_PTR token{};
    Imaging() {
        const Gdiplus::GdiplusStartupInput input;
        check(Gdiplus::GdiplusStartup(&token, &input, nullptr), "Start GDI+");
    }
    ~Imaging() { Gdiplus::GdiplusShutdown(token); }
};

struct Record {
    std::wstring monitor, original, previous, next;
};

bool equal_path(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::wstring current_wallpaper(IDesktopWallpaper& api, const std::wstring& monitor) {
    LPWSTR value = nullptr;
    const auto result = api.GetWallpaper(monitor.c_str(), &value);
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(value, CoTaskMemFree);
    check(result, "Read wallpaper");
    return value ? value : L"";
}

std::optional<Record> load(const fs::path& journal) {
    if (!fs::exists(journal))
        return std::nullopt;
    std::ifstream file(journal, std::ios::binary);
    // UTF-16 avoids loss when Windows paths contain non-ASCII characters.
    std::wstring contents;
    wchar_t value{};
    while (file.read(reinterpret_cast<char*>(&value), sizeof(value)))
        contents.push_back(value);
    if (!file.eof() || file.gcount() != 0)
        throw std::runtime_error("Cannot read wallpaper recovery journal");
    std::wistringstream stream(contents);
    Record record;
    if (!(stream >> std::quoted(record.monitor) >> std::quoted(record.original) >>
          std::quoted(record.previous) >> std::quoted(record.next)) ||
        record.monitor.empty() || record.original.empty() || record.next.empty())
        throw std::runtime_error("Invalid wallpaper recovery journal");
    return record;
}

void save(const fs::path& journal, const Record& record) {
    std::wostringstream stream;
    stream << std::quoted(record.monitor) << L'\n' << std::quoted(record.original) << L'\n'
           << std::quoted(record.previous) << L'\n' << std::quoted(record.next) << L'\n';
    const auto contents = stream.str();
    atomic_write(journal, std::as_bytes(std::span(contents)));
}

fs::path cache_slot(const fs::path& journal, const wchar_t* suffix) {
    return journal.wstring() + suffix;
}

bool owns(const Record& record, const std::wstring& current) {
    // 日志先于 SetWallpaper 提交，移动前后两个缓存路径都可能是异常退出后的合法状态。
    return equal_path(record.next, current) ||
           (!record.previous.empty() && equal_path(record.previous, current));
}
}

WallpaperSession::WallpaperSession() {
    check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Initialize wallpaper COM");
    const auto result = CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&api_));
    if (FAILED(result)) {
        CoUninitialize();
        check(result, "Open desktop wallpaper API");
    }
}

WallpaperSession::~WallpaperSession() {
    api_->Release();
    CoUninitialize();
}

void render_wallpaper(const fs::path& source, const fs::path& output, int width, int height,
                      DESKTOP_WALLPAPER_POSITION mode, COLORREF color, Offset offset) {
    using namespace Gdiplus;
    if (width <= 0 || height <= 0 || mode == DWPOS_TILE || mode == DWPOS_SPAN)
        throw std::invalid_argument("Unsupported wallpaper dimensions or layout");
    const Imaging imaging;
    Bitmap input(source.c_str());
    check(input.GetLastStatus(), "Read source image");
    const double input_width = input.GetWidth(), input_height = input.GetHeight();
    if (input_width <= 0 || input_height <= 0)
        throw std::runtime_error("Wallpaper image has no pixels");
    double scale = 1;
    if (mode == DWPOS_FIT)
        scale = std::min(width / input_width, height / input_height);
    else if (mode == DWPOS_FILL)
        scale = std::max(width / input_width, height / input_height);
    const int w = mode == DWPOS_STRETCH ? width : std::max(1, static_cast<int>(std::lround(input_width * scale)));
    const int h = mode == DWPOS_STRETCH ? height : std::max(1, static_cast<int>(std::lround(input_height * scale)));

    Bitmap canvas(width, height, PixelFormat32bppARGB);
    {
        Graphics graphics(&canvas);
        check(graphics.Clear(Color(GetRValue(color), GetGValue(color), GetBValue(color))), "Clear wallpaper canvas");
        check(graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic), "Set wallpaper interpolation");
        ImageAttributes attributes;
        check(attributes.SetWrapMode(WrapModeTileFlipXY), "Set image edge sampling");
        check(graphics.DrawImage(&input, Rect((width - w) / 2, (height - h) / 2, w, h),
                                 0, 0, static_cast<int>(input_width), static_cast<int>(input_height),
                                 UnitPixel, &attributes), "Scale wallpaper");
    }
    Bitmap shifted(width, height, PixelFormat32bppARGB);
    {
        Graphics graphics(&shifted);
        check(graphics.SetCompositingMode(CompositingModeSourceCopy), "Set wallpaper compositing");
        const int x = ((offset.x % width) + width) % width;
        const int y = ((offset.y % height) + height) % height;
        for (int dx : {-1, 0})
            for (int dy : {-1, 0})
                check(graphics.DrawImage(&canvas, x + dx * width, y + dy * height, width, height), "Shift wallpaper");
    }
    constexpr CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0, 0, 0xf8, 0x1e, 0xf3, 0x2e}};
    check(shifted.Save(output.c_str(), &png, nullptr), "Save shifted wallpaper");
}

void shift_wallpaper(IDesktopWallpaper& api, const std::wstring& monitor,
                     const fs::path& journal, Offset offset) {
    DESKTOP_SLIDESHOW_STATE state{};
    DESKTOP_WALLPAPER_POSITION mode{};
    check(api.GetStatus(&state), "Read wallpaper slideshow state");
    check(api.GetPosition(&mode), "Read wallpaper layout");
    if ((state & DSS_SLIDESHOW) || mode == DWPOS_TILE || mode == DWPOS_SPAN)
        throw std::runtime_error("Slideshow, tiled and spanning wallpapers are unsupported");
    const auto current = current_wallpaper(api, monitor);
    const auto existing = load(journal);
    if (existing && !equal_path(existing->monitor, monitor))
        throw std::runtime_error("Wallpaper journal belongs to another monitor");
    const bool previously_owned = existing && owns(*existing, current);
    // 仍持有壁纸时沿用原图；用户换图后以新图为基准，不能把旧恢复记录覆盖到新壁纸上。
    const auto original = previously_owned ? existing->original : current;
    if (original.empty() || !fs::is_regular_file(original))
        throw std::runtime_error("Source wallpaper is missing");
    const auto first = cache_slot(journal, L".a.png");
    const auto second = cache_slot(journal, L".b.png");
    if (equal_path(original, first.wstring()) || equal_path(original, second.wstring()))
        throw std::runtime_error("Original wallpaper points to a shift cache file");
    // 两个缓存交替使用，避免重写当前仍被桌面读取的图片；每次都从原图重新渲染。
    const auto output = equal_path(current, first.wstring()) ? second : first;
    RECT bounds{};
    COLORREF color{};
    const auto geometry = api.GetMonitorRECT(monitor.c_str(), &bounds);
    if (geometry == S_FALSE)
        throw std::runtime_error("Wallpaper display disconnected");
    check(geometry, "Read monitor geometry");
    check(api.GetBackgroundColor(&color), "Read wallpaper background color");
    render_wallpaper(original, output, bounds.right - bounds.left, bounds.bottom - bounds.top, mode, color, offset);
    if (!equal_path(current_wallpaper(api, monitor), current))
        throw std::runtime_error("Wallpaper changed while preparing shift");
    // 先保存原图及移动前/后的所属状态，确保 SetWallpaper 前后异常退出都能判断是否仍可恢复。
    save(journal, {monitor, original, previously_owned ? current : L"", output.wstring()});
    check(api.SetWallpaper(monitor.c_str(), output.c_str()), "Apply shifted wallpaper");
}

bool restore_wallpaper(IDesktopWallpaper& api, const fs::path& journal,
                       std::span<const std::wstring> active_monitors) {
    const auto record = load(journal);
    if (!record)
        return true;
    const auto monitor = std::ranges::find_if(active_monitors, [&](const auto& id) {
        return equal_path(id, record->monitor);
    });
    // 显示器断开时保留恢复记录，重连后再核对该屏幕的壁纸所有权。
    if (monitor == active_monitors.end())
        return false;
    // 用户已换壁纸时只移除旧日志；不能用旧原图覆盖用户的新选择。
    if (owns(*record, current_wallpaper(api, *monitor))) {
        if (!fs::is_regular_file(record->original))
            throw std::runtime_error("Original wallpaper is missing; recovery journal retained");
        check(api.SetWallpaper(monitor->c_str(), record->original.c_str()), "Restore wallpaper");
    }
    fs::remove(journal);
    return true;
}
}
