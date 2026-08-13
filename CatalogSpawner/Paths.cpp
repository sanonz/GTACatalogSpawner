#include "Paths.h"

#include "Logger.h"

#include <array>
#include <system_error>

namespace {
HMODULE gModule = nullptr;
}

void Paths::SetModule(HMODULE module) {
    gModule = module;
}

HMODULE Paths::Module() {
    return gModule;
}

std::filesystem::path Paths::ModuleDirectory() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetModuleFileNameW(gModule, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size())
        return {};
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

std::filesystem::path Paths::DataDirectory() {
    return ModuleDirectory() / L"CatalogSpawner";
}

bool Paths::EnsureDataDirectories() {
    std::error_code error;
    std::filesystem::create_directories(DataDirectory() / L"Languages", error);
    if (!error)
        std::filesystem::create_directories(DataDirectory() / L"Previews" / L"Peds", error);
    if (!error)
        std::filesystem::create_directories(DataDirectory() / L"Previews" / L"Weapons", error);
    if (!error)
        std::filesystem::create_directories(DataDirectory() / L"Previews" / L"Vehicles", error);
    if (!error)
        std::filesystem::create_directories(DataDirectory() / L"Previews" / L"Scenes", error);
    if (!error)
        std::filesystem::create_directories(DataDirectory() / L"Scenes", error);
    if (!error)
        std::filesystem::create_directories(DataDirectory() / L"Audio", error);
    if (error) {
        LOG_ERROR("Failed to create data directories: {}", error.message());
        return false;
    }
    return true;
}
