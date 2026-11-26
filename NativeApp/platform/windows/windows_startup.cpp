// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#include "windows_startup.hpp"
#include "windows_storage.hpp"

namespace oled::win {
namespace {
namespace fs = std::filesystem;
constexpr auto run_key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto run_value = L"OpenOLEDShifterNative";

void check_win(LSTATUS error, const char* action) {
    if (error != ERROR_SUCCESS)
        throw std::system_error(error, std::system_category(), action);
}
}

bool is_registered() {
    std::wstring command(32768, L'\0');
    DWORD bytes = static_cast<DWORD>(command.size() * sizeof(wchar_t));
    const auto result = RegGetValueW(HKEY_CURRENT_USER, run_key, run_value, RRF_RT_REG_SZ,
                                    nullptr, command.data(), &bytes);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND)
        return false;
    check_win(result, "Read login startup");
    if (bytes < sizeof(wchar_t))
        return false;
    command.resize(bytes / sizeof(wchar_t) - 1);
    const auto end = command.find(L'"', 1);
    return command.starts_with(L'"') && end != std::wstring::npos &&
           command.substr(end + 1) == L" --background" && fs::is_regular_file(command.substr(1, end - 1));
}

void register_startup(bool enabled) {
    if (!enabled) {
        const auto result = RegDeleteKeyValueW(HKEY_CURRENT_USER, run_key, run_value);
        if (result != ERROR_FILE_NOT_FOUND && result != ERROR_PATH_NOT_FOUND)
            check_win(result, "Disable login startup");
        return;
    }
    std::wstring executable(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (size == 0 || size == executable.size())
        throw std::runtime_error("Cannot read executable location");
    executable.resize(size);
    const auto installed = data_root() / L"Programs" / L"OpenOLEDShifter" / L"OpenOLEDShifter.exe";
    fs::create_directories(installed.parent_path());
    if (!fs::exists(installed) || !fs::equivalent(executable, installed)) {
        const auto staged = installed.wstring() + L".new";
        fs::copy_file(executable, staged, fs::copy_options::overwrite_existing);
        if (!MoveFileExW(staged.c_str(), installed.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const auto error = GetLastError();
            std::error_code ignored;
            fs::remove(staged, ignored);
            check_win(error, "Install login executable");
        }
    }
    const auto command = L"\"" + installed.wstring() + L"\" --background";
    check_win(RegSetKeyValueW(HKEY_CURRENT_USER, run_key, run_value, REG_SZ, command.c_str(),
                             static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))), "Enable login startup");
    if (!is_registered())
        throw std::runtime_error("Windows did not retain the login startup entry");
}
}
