#pragma once

#include <inc/types.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

struct SavedLocation {
    std::string Name;
    Vector3 Position{};
    float Heading = 0.0f;
    bool HasHeading = false;
    std::string Zone;
};

class TeleportStore {
public:
    void SetPath(std::filesystem::path path);
    bool Load();
    bool Save() const;

    bool Add(SavedLocation location);
    bool RemoveAt(std::size_t index);

    const std::vector<SavedLocation>& Locations() const;
    std::string NextDefaultName(const std::string& baseName, const std::string& fallbackPrefix) const;

private:
    std::filesystem::path path_;
    std::vector<SavedLocation> locations_;
};

extern TeleportStore gTeleports;
