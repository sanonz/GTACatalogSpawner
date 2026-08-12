#pragma once

#include <filesystem>
#include <format>
#include <mutex>
#include <string>
#include <string_view>

class Logger {
public:
    void SetPath(const std::filesystem::path& path);
    void Clear();

    template <typename... Args>
    void Write(std::string_view level, std::string_view format, Args&&... args) {
        try {
            WriteLine(level, std::vformat(format, std::make_format_args(args...)));
        }
        catch (const std::exception& exception) {
            WriteLine("ERROR", std::format("Log formatting failed: {}", exception.what()));
        }
    }

private:
    void WriteLine(std::string_view level, const std::string& message);

    std::filesystem::path path_;
    std::mutex mutex_;
};

extern Logger gLogger;

#define LOG_DEBUG(...) gLogger.Write("DEBUG", __VA_ARGS__)
#define LOG_INFO(...) gLogger.Write("INFO", __VA_ARGS__)
#define LOG_WARNING(...) gLogger.Write("WARNING", __VA_ARGS__)
#define LOG_ERROR(...) gLogger.Write("ERROR", __VA_ARGS__)
