#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace SceneLoader {
struct PreflightResult {
  bool Valid = false;
  bool Destructive = false;
  bool ClearDatabase = false;
  bool ClearMarkers = false;
  float ClearWorldRadius = 0.0f;
  std::string Error;
};

struct SessionInfo {
  std::uint64_t Id = 0;
  std::string Name;
  std::size_t EntityCount = 0;
  std::size_t FailureCount = 0;
};

PreflightResult Preflight(const std::filesystem::path &path);
bool Load(const std::filesystem::path &path, const std::string &displayName,
          bool allowDestructive = false);
void Tick();
void Unload(std::uint64_t id);
void UnloadAll();
std::vector<SessionInfo> ActiveSessions();
std::size_t LoadedEntityCount();
} // namespace SceneLoader
