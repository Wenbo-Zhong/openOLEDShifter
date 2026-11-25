// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "windows_wallpaper.hpp"
#include "windows_displays.hpp"

#include <cstring>
#include <fstream>
#include <gdiplus.h>
#include <iostream>
#include <map>

namespace fs = std::filesystem;
using namespace oled::win;

struct FakeWallpaper final : IDesktopWallpaper {
    std::map<std::wstring, std::wstring> wallpapers;
    bool reject = false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE SetWallpaper(LPCWSTR display, LPCWSTR path) override {
        if (reject)
            return E_ACCESSDENIED;
        if (!wallpapers.contains(display))
            return E_INVALIDARG;
        wallpapers.at(display) = path;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetWallpaper(LPCWSTR display, LPWSTR* path) override {
        if (!wallpapers.contains(display))
            return E_INVALIDARG;
        const auto& current = wallpapers.at(display);
        const auto size = (current.size() + 1) * sizeof(wchar_t);
        *path = static_cast<wchar_t*>(CoTaskMemAlloc(size));
        if (!*path)
            return E_OUTOFMEMORY;
        std::memcpy(*path, current.c_str(), size);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMonitorDevicePathAt(UINT, LPWSTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMonitorDevicePathCount(UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMonitorRECT(LPCWSTR, RECT* rect) override { *rect = {0, 0, 2, 1}; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(COLORREF) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(COLORREF* value) override { *value = 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetPosition(DESKTOP_WALLPAPER_POSITION) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetPosition(DESKTOP_WALLPAPER_POSITION* mode) override { *mode = DWPOS_CENTER; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetSlideshow(IShellItemArray*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSlideshow(IShellItemArray**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetSlideshowOptions(DESKTOP_SLIDESHOW_OPTIONS, UINT) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSlideshowOptions(DESKTOP_SLIDESHOW_OPTIONS*, UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE AdvanceSlideshow(LPCWSTR, DESKTOP_SLIDESHOW_DIRECTION) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetStatus(DESKTOP_SLIDESHOW_STATE* state) override { *state = DSS_ENABLED; return S_OK; }
    HRESULT STDMETHODCALLTYPE Enable(BOOL) override { return E_NOTIMPL; }
};

void require(bool result, const char* message) {
    if (!result)
        throw std::runtime_error(message);
}

int main() try {
    struct Fixture {
        fs::path root = fs::current_path() / (L"OpenOLEDWallpaperTest-" + std::to_wstring(GetCurrentProcessId()));
        Fixture() { require(fs::create_directory(root), "could not create private fixture directory"); }
        ~Fixture() { std::error_code ignored; fs::remove_all(root, ignored); }
    } fixture;
    const auto source = fixture.root / L"source.bmp";
    const auto journal = fixture.root / L"recovery";
    {
        const BITMAPFILEHEADER header{0x4d42, 62, 0, 0, 54};
        const BITMAPINFOHEADER info{40, 2, 1, 1, 24, BI_RGB, 8, 0, 0, 0, 0};
        const unsigned char pixels[]{0, 0, 255, 0, 255, 0, 0, 0};
        std::ofstream bmp(source, std::ios::binary);
        bmp.write(reinterpret_cast<const char*>(&header), sizeof(header));
        bmp.write(reinterpret_cast<const char*>(&info), sizeof(info));
        bmp.write(reinterpret_cast<const char*>(pixels), sizeof(pixels));
        require(bmp.good(), "could not write fixture image");
    }
    FakeWallpaper api;
    const std::vector<std::wstring> primary_only{L"display"};
    auto& current = api.wallpapers[L"display"];
    current = source.wstring();
    shift_wallpaper(api, L"display", journal, {1, 0});
    const auto first = current;
    require(first != source.wstring() && fs::exists(first) && fs::exists(journal), "did not stage and apply shift");
    {
        struct Imaging {
            ULONG_PTR token{};
            Imaging() {
                const Gdiplus::GdiplusStartupInput input;
                require(Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok, "start GDI+");
            }
            ~Imaging() { Gdiplus::GdiplusShutdown(token); }
        } imaging;
        Gdiplus::Bitmap output(first.c_str());
        Gdiplus::Color left, right;
        require(output.GetPixel(0, 0, &left) == Gdiplus::Ok &&
                output.GetPixel(1, 0, &right) == Gdiplus::Ok &&
                left.GetGreen() == 255 && right.GetRed() == 255, "one-pixel shift did not wrap the image");
    }
    api.reject = true;
    bool failed = false;
    try { shift_wallpaper(api, L"display", journal, {0, 0}); }
    catch (const std::system_error&) { failed = true; }
    require(failed && current == first, "failed application changed current wallpaper");
    api.reject = false;
    require(restore_wallpaper(api, journal, primary_only) &&
            current == source.wstring() && !fs::exists(journal), "previous owned target was not restored");

    shift_wallpaper(api, L"display", journal, {1, 0});
    current = (fixture.root / L"user-selected.bmp").wstring();
    require(restore_wallpaper(api, journal, primary_only), "could not settle user-owned wallpaper");
    require(fs::path(current).filename() == L"user-selected.bmp", "overwrote user's wallpaper choice");
    require(!fs::exists(journal), "discarded ownership journal still present");

    const WallpaperDisplay primary{L"display", L"DISPLAY1", {0, 0, 2, 1}, true};
    const WallpaperDisplay secondary{L"external", L"DISPLAY2", {2, 0, 4, 1}, false};
    const WallpaperDisplay relocated{L"EXTERNAL", L"DISPLAY3", {-2, 0, 0, 1}, true};
    const auto first_journal = wallpaper_journal(fixture.root, primary);
    const auto second_journal = wallpaper_journal(fixture.root, secondary);
    require(first_journal != second_journal && secondary.key() == relocated.key(),
            "display key changed when its position or enumeration changed");
    require(selects_display("all", primary) && selects_display("all", secondary) &&
            selects_display("primary", primary) && !selects_display("primary", secondary) &&
            selects_display(secondary.key(), secondary) && !selects_display(secondary.key(), primary),
            "display selection is incorrect");

    const auto second_source = fixture.root / L"external.bmp";
    fs::copy_file(source, second_source);
    current = source.wstring();
    auto& second_current = api.wallpapers[secondary.device_path];
    second_current = second_source.wstring();
    shift_wallpaper(api, primary.device_path, first_journal, {1, 0});
    shift_wallpaper(api, secondary.device_path, second_journal, {1, 0});
    const auto second_shift = second_current;
    require(current != second_current && wallpaper_journals(fixture.root).size() == 2,
            "displays share recovery state");
    require(!restore_wallpaper(api, second_journal, primary_only) &&
            fs::exists(second_journal) && second_current == second_shift,
            "removed or touched recovery while the external display was disconnected");
    require(restore_wallpaper(api, first_journal, primary_only) &&
            current == source.wstring() && second_current == second_shift,
            "primary restore disturbed the disconnected display");
    const std::vector<std::wstring> both{primary.device_path, secondary.device_path};
    require(restore_wallpaper(api, second_journal, both) && second_current == second_source.wstring() &&
            wallpaper_journals(fixture.root).empty(), "external display recovery did not resume");
    std::cout << "PASS: isolated wallpaper rendering and per-display recovery\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
