#include "Language.h"

#include "Logger.h"
#include "Utils.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <xmllite.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <system_error>

LanguageManager gLanguage;

namespace {
using StringMap = std::unordered_map<std::string, std::string>;

const StringMap kEnglishStrings = {
    { "menu.main.title", "Catalog Spawner" },
    { "menu.main.peds", "Peds" },
    { "menu.main.weapons", "Weapons" },
    { "menu.main.vehicles", "Vehicles" },
    { "menu.main.scenes", "Scenes" },
    { "menu.main.settings", "Settings" },
    { "menu.peds.title", "Peds" },
    { "menu.weapons.title", "Weapons" },
    { "menu.vehicles.title", "Vehicles" },
    { "menu.scenes.title", "Scenes" },
    { "menu.scenes.unload", "Unload loaded scenes ({count} entities)" },
    { "menu.scenes.active", "Active scenes ({count})" },
    { "menu.scenes.active_title", "Active scenes" },
    { "menu.scenes.none_active", "No active scenes" },
    { "menu.scenes.unload_one", "Unload {name} ({count} entities)" },
    { "menu.scenes.failures", "Load failures: {count}" },
    { "menu.scene.no_selection", "No scene selected" },
    { "menu.scene.invalid", "This scene XML is invalid" },
    { "menu.scene.ready", "Review and load this scene" },
    { "menu.scene.destructive_warning", "Warning: destructive directives detected; direct loading ignores them" },
    { "menu.scene.clear_database", "ClearDatabase: ignored (would unload all Catalog Spawner scenes)" },
    { "menu.scene.clear_markers", "ClearMarkers: ignored (would clear active Catalog Spawner markers)" },
    { "menu.scene.clear_world", "ClearWorld: ignored (would clear world entities within {radius} units)" },
    { "menu.scene.load", "Load scene" },
    { "menu.scene.confirm_load", "Confirm and load scene" },
    { "menu.settings.title", "Settings" },
    { "menu.empty", "No configured items" },
    { "menu.empty.detail", "Edit the corresponding XML catalog and reopen the menu." },
    { "menu.item_count", "{count} item(s)" },
    { "footer.brand", "xmodbox.com" },
    { "settings.language", "Language" },
    { "settings.enter_vehicle", "Enter spawned vehicle" },
    { "settings.enter_vehicle.detail", "Put the current player into the driver seat after spawning a vehicle." },
    { "info.ped.title", "Ped info" },
    { "info.weapon.title", "Weapon info" },
    { "info.vehicle.title", "Vehicle info" },
    { "info.scene.title", "Scene info" },
    { "info.scene_path", "Scene file: {value}" },
    { "info.spawn_name", "Spawn name: {value}" },
    { "info.hash", "Hash: {value}" },
    { "info.category", "Category: {value}" },
    { "info.description", "Description: {value}" },
    { "info.ammo", "Ammo: {value}" },
    { "info.damage", "Base damage: {value}" },
    { "info.clip_size", "Clip size: {value}" },
    { "info.make", "Make: {value}" },
    { "info.game_name", "Game name: {value}" },
    { "info.vehicle_class", "Class: {value}" },
    { "common.none", "None" },
    { "common.unknown", "Unknown" },
    { "common.back", "Back" },
    { "subtitle.model_load_failed", "Couldn't load model: {model}" },
    { "subtitle.ped_missing", "Ped model doesn't exist: {model}" },
    { "subtitle.ped_switched", "Player model changed to {name}" },
    { "subtitle.weapon_invalid", "Weapon doesn't exist: {model}" },
    { "subtitle.weapon_given", "Received {name}" },
    { "subtitle.vehicle_missing", "Vehicle doesn't exist: {model}" },
    { "subtitle.vehicle_create_failed", "Couldn't create vehicle: {model}" },
    { "subtitle.vehicle_spawned", "Spawned {name}" },
    { "subtitle.scene_invalid", "Couldn't read scene: {name}" },
    { "subtitle.scene_loaded", "Loaded {name}: {loaded} entities, {failed} failed" },
    { "subtitle.scenes_unloaded", "Unloaded {count} scene entities" },
};

std::set<std::string> Placeholders(const std::string& value) {
    std::set<std::string> result;
    size_t start = 0;
    while ((start = value.find('{', start)) != std::string::npos) {
        const size_t end = value.find('}', start + 1);
        if (end == std::string::npos)
            break;
        result.insert(value.substr(start, end - start + 1));
        start = end + 1;
    }
    return result;
}

std::string ReaderValue(IXmlReader* reader) {
    const wchar_t* value = nullptr;
    unsigned int length = 0;
    if (FAILED(reader->GetValue(&value, &length)))
        return {};
    return Utils::WideToUtf8(value, length);
}

std::string Attribute(IXmlReader* reader, const wchar_t* name) {
    if (FAILED(reader->MoveToAttributeByName(name, nullptr)))
        return {};
    const std::string value = ReaderValue(reader);
    reader->MoveToElement();
    return value;
}
}

bool LanguageManager::ParseFile(const std::filesystem::path& path, LanguagePack& pack) {
    IStream* stream = nullptr;
    HRESULT result = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
        FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream);
    if (FAILED(result)) {
        LOG_WARNING("[Language] Failed to open {} (0x{:08X})", path.string(),
            static_cast<unsigned int>(result));
        return false;
    }

    IXmlReader* reader = nullptr;
    result = CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(&reader), nullptr);
    if (FAILED(result)) {
        stream->Release();
        LOG_WARNING("[Language] Failed to create reader (0x{:08X})",
            static_cast<unsigned int>(result));
        return false;
    }

    result = reader->SetInput(stream);
    bool rootSeen = false;
    bool insideText = false;
    std::string currentKey;
    std::string currentValue;
    XmlNodeType nodeType = XmlNodeType_None;

    while (SUCCEEDED(result) && (result = reader->Read(&nodeType)) == S_OK) {
        const wchar_t* localName = nullptr;
        unsigned int length = 0;
        reader->GetLocalName(&localName, &length);
        const std::string name = Utils::WideToUtf8(localName, length);

        if (nodeType == XmlNodeType_Element) {
            unsigned int depth = 0;
            reader->GetDepth(&depth);
            if (!rootSeen && depth == 0 && name == "language") {
                rootSeen = true;
                pack.Info.Code = Utils::Trim(Attribute(reader, L"code"));
                pack.Info.Name = Utils::Trim(Attribute(reader, L"name"));
            }
            else if (rootSeen && depth == 1 && name == "text" && !insideText) {
                currentKey = Utils::Trim(Attribute(reader, L"key"));
                currentValue.clear();
                if (!reader->IsEmptyElement())
                    insideText = true;
            }
            else if (insideText) {
                result = E_FAIL;
            }
        }
        else if (insideText && (nodeType == XmlNodeType_Text || nodeType == XmlNodeType_CDATA ||
            nodeType == XmlNodeType_Whitespace)) {
            currentValue += ReaderValue(reader);
        }
        else if (insideText && nodeType == XmlNodeType_EndElement && name == "text") {
            currentValue = Utils::Trim(currentValue);
            if (!currentKey.empty() && !currentValue.empty())
                pack.Strings[currentKey] = currentValue;
            insideText = false;
        }
    }

    reader->Release();
    stream->Release();
    if (FAILED(result) || !rootSeen || pack.Info.Code.empty() || pack.Info.Name.empty()) {
        LOG_WARNING("[Language] Invalid language pack: {}", path.string());
        return false;
    }
    pack.Info.FilePath = path;
    return true;
}

bool LanguageManager::Reload(const std::filesystem::path& directory,
    const std::string& preferredCode) {
    packs_.clear();
    languageInfos_.clear();

    LanguagePack english;
    english.Info = { "en-US", "English", {} };
    english.Strings = kEnglishStrings;
    packs_.push_back(std::move(english));

    std::error_code error;
    std::vector<std::filesystem::path> files;
    if (std::filesystem::is_directory(directory, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
            if (error)
                break;
            if (entry.is_regular_file() && Utils::ToLower(entry.path().extension().string()) == ".xml")
                files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const auto& file : files) {
        LanguagePack parsed;
        if (!ParseFile(file, parsed))
            continue;
        const auto duplicate = std::find_if(packs_.begin(), packs_.end(), [&](const auto& existing) {
            return existing.Info.Code == parsed.Info.Code;
        });
        if (duplicate != packs_.end()) {
            if (parsed.Info.Code == "en-US" && duplicate->Info.FilePath.empty()) {
                duplicate->Info.Name = parsed.Info.Name;
                duplicate->Info.FilePath = parsed.Info.FilePath;
                for (const auto& [key, value] : parsed.Strings)
                    duplicate->Strings[key] = value;
            }
            else {
                LOG_WARNING("[Language] Duplicate language code '{}' in {}", parsed.Info.Code,
                    file.string());
            }
            continue;
        }
        packs_.push_back(std::move(parsed));
    }

    for (const auto& pack : packs_)
        languageInfos_.push_back(pack.Info);
    if (!Select(preferredCode)) {
        Select("en-US");
        return false;
    }
    return true;
}

bool LanguageManager::Select(const std::string& code) {
    const auto found = std::find_if(packs_.begin(), packs_.end(), [&](const auto& pack) {
        return pack.Info.Code == code;
    });
    if (found == packs_.end())
        return false;
    Activate(*found);
    return true;
}

void LanguageManager::Activate(const LanguagePack& pack) {
    activeStrings_ = kEnglishStrings;
    for (const auto& [key, value] : pack.Strings) {
        const auto fallback = kEnglishStrings.find(key);
        if (fallback != kEnglishStrings.end() &&
            Placeholders(fallback->second) != Placeholders(value)) {
            LOG_WARNING("[Language] Placeholder mismatch for '{}'; using English", key);
            continue;
        }
        activeStrings_[key] = value;
    }
    activeCode_ = pack.Info.Code;
    LOG_INFO("[Language] Active language: {}", activeCode_);
}

std::string LanguageManager::Text(const std::string& key) const {
    const auto found = activeStrings_.find(key);
    if (found != activeStrings_.end())
        return found->second;
    const auto fallback = kEnglishStrings.find(key);
    if (fallback != kEnglishStrings.end())
        return fallback->second;
    LOG_WARNING("[Language] Unknown translation key '{}'", key);
    return key;
}

std::string LanguageManager::Format(const std::string& key,
    std::initializer_list<std::pair<std::string, std::string>> replacements) const {
    std::string result = Text(key);
    for (const auto& [name, value] : replacements) {
        const std::string token = "{" + name + "}";
        size_t position = 0;
        while ((position = result.find(token, position)) != std::string::npos) {
            result.replace(position, token.size(), value);
            position += value.size();
        }
    }
    return result;
}

const std::vector<LanguageInfo>& LanguageManager::Languages() const {
    return languageInfos_;
}

const std::string& LanguageManager::ActiveCode() const {
    return activeCode_;
}
