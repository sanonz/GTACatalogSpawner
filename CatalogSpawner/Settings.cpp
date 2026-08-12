#include "Settings.h"

#include "Logger.h"

#include <simpleini/SimpleIni.h>

Settings gSettings;

void Settings::SetPath(std::filesystem::path path) {
    path_ = std::move(path);
}

void Settings::Load() {
    CSimpleIniA ini;
    ini.SetUnicode();
    const SI_Error result = ini.LoadFile(path_.string().c_str());
    if (result < 0)
        LOG_WARNING("Settings file is unavailable; defaults will be used: {}", path_.string());
    Language = ini.GetValue("OPTIONS", "Language", "en-US");
    EnterSpawnedVehicle = ini.GetBoolValue("OPTIONS", "EnterSpawnedVehicle", true);
}

void Settings::Save() const {
    CSimpleIniA ini;
    ini.SetUnicode();
    ini.LoadFile(path_.string().c_str());
    ini.SetValue("OPTIONS", "Language", Language.c_str());
    ini.SetBoolValue("OPTIONS", "EnterSpawnedVehicle", EnterSpawnedVehicle);
    if (ini.SaveFile(path_.string().c_str()) < 0)
        LOG_ERROR("Failed to save settings: {}", path_.string());
}

