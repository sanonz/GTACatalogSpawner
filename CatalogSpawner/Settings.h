#pragma once

#include <filesystem>
#include <string>

class Settings {
public:
    void SetPath(std::filesystem::path path);
    void Load();
    void Save() const;

    std::string Language = "en-US";
    bool EnterSpawnedVehicle = true;

private:
    std::filesystem::path path_;
};

extern Settings gSettings;

