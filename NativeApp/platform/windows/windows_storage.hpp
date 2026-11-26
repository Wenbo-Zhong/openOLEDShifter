// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 OpenOLEDShifter contributors
#pragma once

#include <filesystem>
#include <limits>
#include <span>
#include <stdexcept>
#include <system_error>
#include <windows.h>

namespace oled::win {
std::filesystem::path data_root();

// 先写入并刷新同目录临时文件，再替换正式记录，避免直接覆盖时留下半截恢复数据。
// 修改桌面状态的调用方必须在系统操作之前成功提交恢复意图。
inline void atomic_write(const std::filesystem::path& file, std::span<const std::byte> bytes) {
    if (bytes.size() > (std::numeric_limits<DWORD>::max)())
        throw std::length_error("File is too large");
    const auto temporary = file.wstring() + L".tmp";
    const HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::system_error(GetLastError(), std::system_category(), "Open temporary file");
    const auto size = static_cast<DWORD>(bytes.size());
    DWORD written{};
    const bool success = WriteFile(handle, bytes.data(), size, &written, nullptr) &&
                         written == size && FlushFileBuffers(handle);
    const auto error = success ? ERROR_SUCCESS : GetLastError();
    CloseHandle(handle);
    if (!success)
        throw std::system_error(error ? error : ERROR_WRITE_FAULT, std::system_category(), "Flush temporary file");
    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::system_error(GetLastError(), std::system_category(), "Commit file");
}
}
