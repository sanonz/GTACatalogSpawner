#include "Logger.h"

#include <Windows.h>

#include <string>

Logger gLogger;

void Logger::SetPath(const std::filesystem::path& path) {
    std::scoped_lock lock(mutex_);
    path_ = path;
}

void Logger::Clear() {
    std::scoped_lock lock(mutex_);
    if (path_.empty())
        return;
    HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
}

void Logger::WriteLine(std::string_view level, const std::string& message) {
    std::scoped_lock lock(mutex_);
    if (path_.empty())
        return;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    const std::string line = std::format(
        "{:02}:{:02}:{:02}.{:03} [{}] [tid={}] {}\r\n",
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, level,
        GetCurrentThreadId(), message);

    HANDLE file = CreateFileW(path_.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        OutputDebugStringA(line.c_str());
        return;
    }

    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written,
        nullptr);
    if (level != "DEBUG")
        FlushFileBuffers(file);
    CloseHandle(file);
}
