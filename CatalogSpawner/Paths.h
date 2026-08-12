#pragma once

#include <Windows.h>

#include <filesystem>

namespace Paths {
void SetModule(HMODULE module);
HMODULE Module();
std::filesystem::path ModuleDirectory();
std::filesystem::path DataDirectory();
bool EnsureDataDirectories();
}

