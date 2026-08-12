#include "Logger.h"

#include <Windows.h>

#include <chrono>
#include <fstream>

Logger gLogger;

void Logger::SetPath(const std::filesystem::path& path) {
    std::scoped_lock lock(mutex_);
    path_ = path;
}

void Logger::Clear() {
    std::scoped_lock lock(mutex_);
    if (path_.empty())
        return;
    std::ofstream file(path_, std::ios::trunc);
}

void Logger::WriteLine(std::string_view level, const std::string& message) {
    std::scoped_lock lock(mutex_);
    if (path_.empty())
        return;

    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::ofstream file(path_, std::ios::app);
    if (!file)
        return;
    file << std::format("{:02}:{:02}:{:02}.{:03} [{}] {}\n",
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds, level, message);
}

