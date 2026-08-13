#include "ScriptMenu.h"

#include "CatalogItem.h"
#include "ConfigLoader.h"
#include "ImageCache.h"
#include "Language.h"
#include "Script.h"
#include "SceneLoader.h"
#include "Settings.h"
#include "Spawner.h"
#include "Utils.h"
#include "Version.h"

#include <inc/natives.h>

#include <algorithm>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
std::unordered_map<std::string, SceneLoader::PreflightResult>
    gScenePreflightCache;

std::string T(const char* key) {
    return gLanguage.Text(key);
}

void DrawFooterBrand(int optionCount) {
    constexpr int kMaxDisplayedOptions = 10;
    constexpr float kTitleHeight = 0.1f;
    constexpr float kSubtitleHeight = 0.035f;
    constexpr float kOptionHeight = 0.035f;
    constexpr float kMenuTextMargin = 0.005f;
    constexpr float kOptionTextSize = 0.45f;

    const std::string text = T("footer.brand");
    if (text.empty() || optionCount <= 0)
        return;

    const int displayedOptions = std::min(optionCount, kMaxDisplayedOptions);
    float footerTextY = gMenu.menuY + kTitleHeight + kSubtitleHeight +
        displayedOptions * kOptionHeight;
    float textScale = kOptionTextSize;
    if (gMenu.optionsFont == 0) {
        textScale *= 0.75f;
        footerTextY += 0.003f;
    }

    GRAPHICS::SET_SCRIPT_GFX_ALIGN('L', 'T');
    GRAPHICS::SET_SCRIPT_GFX_ALIGN_PARAMS({ 0.0f, 0.0f }, 0.0f, 0.0f);
    HUD::SET_TEXT_JUSTIFICATION(1);
    HUD::SET_TEXT_FONT(gMenu.optionsFont);
    HUD::SET_TEXT_SCALE(0.0f, textScale);
    HUD::SET_TEXT_COLOUR(gMenu.titleTextColor.R, gMenu.titleTextColor.G,
        gMenu.titleTextColor.B, gMenu.titleTextColor.A);
    HUD::BEGIN_TEXT_COMMAND_DISPLAY_TEXT("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(text.c_str());
    HUD::END_TEXT_COMMAND_DISPLAY_TEXT({
        gMenu.menuX - gMenu.MenuWidth() / 2.0f + kMenuTextMargin,
        footerTextY,
    }, 0);
    GRAPHICS::RESET_SCRIPT_GFX_ALIGN();
}

std::string InfoTitle(CatalogKind kind) {
    switch (kind) {
    case CatalogKind::Ped: return T("info.ped.title");
    case CatalogKind::Weapon: return T("info.weapon.title");
    case CatalogKind::Vehicle: return T("info.vehicle.title");
    case CatalogKind::Scene: return T("info.scene.title");
    }
    return {};
}

std::string ConfiguredOrUnknown(const std::string& value) {
    return value.empty() ? T("common.unknown") : value;
}

std::vector<std::string> BuildDetails(const CatalogItem& item) {
    std::vector<std::string> details;
    const std::string preview = gImages.MenuExtra(item.PreviewPath);
    if (!preview.empty())
        details.push_back(preview);

    if (item.Kind != CatalogKind::Scene) {
        details.push_back(gLanguage.Format("info.spawn_name", { { "value", item.SpawnName } }));
        details.push_back(gLanguage.Format("info.hash", { { "value", Utils::HexHash(item.ModelHash()) } }));
    }
    if (!item.Category.empty())
        details.push_back(gLanguage.Format("info.category", { { "value", item.Category } }));
    if (!item.Description.empty())
        details.push_back(gLanguage.Format("info.description", { { "value", item.Description } }));

    const Hash hash = item.ModelHash();
    if (item.Kind == CatalogKind::Weapon) {
        details.push_back(gLanguage.Format("info.ammo", {
            { "value", std::to_string(item.Ammo) },
        }));
        if (WEAPON::IS_WEAPON_VALID(hash)) {
            details.push_back(gLanguage.Format("info.damage", {
                { "value", std::format("{:.1f}", WEAPON::GET_WEAPON_DAMAGE(hash, 0)) },
            }));
            details.push_back(gLanguage.Format("info.clip_size", {
                { "value", std::to_string(WEAPON::GET_WEAPON_CLIP_SIZE(hash)) },
            }));
        }
    }
    else if (item.Kind == CatalogKind::Vehicle && STREAMING::IS_MODEL_A_VEHICLE(hash)) {
        const char* gameLabel = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(hash);
        std::string gameName = gameLabel ? Utils::GxtText(gameLabel) : std::string();
        if (gameName.empty() && gameLabel && std::string(gameLabel) != "NULL")
            gameName = gameLabel;

        const char* makeLabel = VEHICLE::GET_MAKE_NAME_FROM_VEHICLE_MODEL(hash);
        std::string makeName = makeLabel ? Utils::GxtText(makeLabel) : std::string();
        if (makeName.empty() && makeLabel && std::string(makeLabel) != "NULL")
            makeName = makeLabel;

        const std::string className = Utils::GxtText(std::format(
            "VEH_CLASS_{}", VEHICLE::GET_VEHICLE_CLASS_FROM_NAME(hash)));
        details.push_back(gLanguage.Format("info.game_name", {
            { "value", ConfiguredOrUnknown(gameName) },
        }));
        details.push_back(gLanguage.Format("info.make", {
            { "value", ConfiguredOrUnknown(makeName) },
        }));
        details.push_back(gLanguage.Format("info.vehicle_class", {
            { "value", ConfiguredOrUnknown(className) },
        }));
    }

    for (const auto& [label, value] : item.ExtraInfo)
        details.push_back(std::format("{}: {}", label, value));

    // GTAVMenuBase sizes the right pane from its content. A whitespace-only
    // line reserves bottom padding without drawing another visible value.
    details.emplace_back(" ");
    return details;
}

const SceneLoader::PreflightResult& ScenePreflight(const CatalogItem& scene) {
    const std::string key = scene.ScenePath.lexically_normal().string();
    const auto existing = gScenePreflightCache.find(key);
    if (existing != gScenePreflightCache.end())
        return existing->second;
    return gScenePreflightCache.emplace(key,
        SceneLoader::Preflight(scene.ScenePath)).first->second;
}

std::vector<std::string> BuildSceneDetails(const CatalogItem& scene,
    const SceneLoader::PreflightResult& preflight) {
    std::vector<std::string> details = BuildDetails(scene);
    if (!details.empty())
        details.pop_back();

    if (!preflight.Valid) {
        details.emplace_back(" ");
        details.push_back(T("menu.scene.invalid"));
        if (!preflight.Error.empty())
            details.push_back(preflight.Error);
    }
    else if (preflight.Destructive) {
        details.emplace_back(" ");
        details.push_back(T("menu.scene.destructive_warning"));
        if (preflight.ClearDatabase)
            details.push_back(T("menu.scene.clear_database"));
        if (preflight.ClearMarkers)
            details.push_back(T("menu.scene.clear_markers"));
        if (preflight.ClearWorldRadius > 0.0f)
            details.push_back(gLanguage.Format("menu.scene.clear_world", {
                { "radius", std::format("{:.1f}", preflight.ClearWorldRadius) },
            }));
    }

    details.emplace_back(" ");
    return details;
}

void AddItem(const CatalogItem& item) {
    bool highlighted = false;
    if (gMenu.OptionPlus(item.DisplayName, {}, &highlighted, nullptr, nullptr,
        InfoTitle(item.Kind))) {
        switch (item.Kind) {
        case CatalogKind::Ped:
            Spawner::SwitchPlayerModel(item);
            break;
        case CatalogKind::Weapon:
            Spawner::GiveWeapon(item);
            break;
        case CatalogKind::Vehicle:
            Spawner::SpawnVehicle(item, gSettings.EnterSpawnedVehicle);
            break;
        case CatalogKind::Scene:
            SceneLoader::Load(item.ScenePath, item.DisplayName);
            break;
        }
    }
    if (highlighted)
        gMenu.OptionPlusPlus(BuildDetails(item), InfoTitle(item.Kind));
}

void AddEmptyState() {
    gMenu.Option(T("menu.empty"), { T("menu.empty.detail") });
}

void UpdateCatalogMenu(const char* titleKey, const std::vector<CatalogItem>& items) {
    gMenu.Title(T(titleKey));
    gMenu.Subtitle(gLanguage.Format("menu.item_count", {
        { "count", std::to_string(items.size()) },
    }));
    if (items.empty()) {
        AddEmptyState();
        return;
    }
    for (const auto& item : items)
        AddItem(item);
}

void UpdateMainMenu() {
    gMenu.Title(T("menu.main.title"));
    gMenu.Subtitle(std::format("v{}", CATALOG_SPAWNER_VERSION));
    gMenu.MenuOption(T("menu.main.peds"), "pedsmenu");
    gMenu.MenuOption(T("menu.main.weapons"), "weaponsmenu");
    gMenu.MenuOption(T("menu.main.vehicles"), "vehiclesmenu");
    gMenu.MenuOption(T("menu.main.scenes"), "scenesmenu");
    gMenu.MenuOption(T("menu.main.settings"), "settingsmenu");
}

void UpdateScenesMenu() {
    const auto& scenes = gCatalogs.Scenes();
    gMenu.Title(T("menu.scenes.title"));
    gMenu.Subtitle(gLanguage.Format("menu.item_count", {
        { "count", std::to_string(scenes.size()) },
    }));
    const auto sessions = SceneLoader::ActiveSessions();
    if (!sessions.empty())
        gMenu.MenuOption(gLanguage.Format("menu.scenes.active", {
            { "count", std::to_string(sessions.size()) },
        }), "scenesessionsmenu");
    if (scenes.empty()) {
        AddEmptyState();
        return;
    }
    for (const auto& scene : scenes) {
        bool highlighted = false;
        const bool selected = gMenu.OptionPlus(scene.DisplayName, {}, &highlighted,
            nullptr, nullptr, InfoTitle(CatalogKind::Scene));
        if (!highlighted && !selected)
            continue;

        const auto& preflight = ScenePreflight(scene);
        if (highlighted)
            gMenu.OptionPlusPlus(BuildSceneDetails(scene, preflight),
                InfoTitle(CatalogKind::Scene));
        if (selected) {
            if (preflight.Valid)
                SceneLoader::Load(scene.ScenePath, scene.DisplayName);
            else
                Utils::ShowSubtitle(gLanguage.Format("subtitle.scene_invalid", {
                    { "name", scene.DisplayName },
                }));
        }
    }
}

void UpdateSceneSessionsMenu() {
    const auto sessions = SceneLoader::ActiveSessions();
    gMenu.Title(T("menu.scenes.active_title"));
    gMenu.Subtitle(gLanguage.Format("menu.item_count", {
        { "count", std::to_string(sessions.size()) },
    }));
    if (sessions.empty()) {
        gMenu.Option(T("menu.scenes.none_active"));
        return;
    }
    if (gMenu.Option(gLanguage.Format("menu.scenes.unload", {
        { "count", std::to_string(SceneLoader::LoadedEntityCount()) },
    }))) SceneLoader::UnloadAll();
    for (const auto& session : sessions) {
        if (gMenu.Option(gLanguage.Format("menu.scenes.unload_one", {
            { "name", session.Name },
            { "count", std::to_string(session.EntityCount) },
        }), session.FailureCount > 0 ? std::vector<std::string>{
            gLanguage.Format("menu.scenes.failures", {
                { "count", std::to_string(session.FailureCount) },
            })
        } : std::vector<std::string>{})) {
            SceneLoader::Unload(session.Id);
        }
    }
}

void UpdateSettingsMenu() {
    gMenu.Title(T("menu.settings.title"));
    gMenu.Subtitle("");

    const auto& languages = gLanguage.Languages();
    std::vector<std::string> names;
    int selected = 0;
    for (size_t index = 0; index < languages.size(); ++index) {
        names.push_back(languages[index].Name);
        if (languages[index].Code == gLanguage.ActiveCode())
            selected = static_cast<int>(index);
    }
    if (!names.empty() && gMenu.StringArray(T("settings.language"), names, selected)) {
        if (selected >= 0 && selected < static_cast<int>(languages.size()) &&
            gLanguage.Select(languages[selected].Code)) {
            gSettings.Language = gLanguage.ActiveCode();
            gSettings.Save();
        }
    }

    if (gMenu.BoolOption(T("settings.enter_vehicle"), gSettings.EnterSpawnedVehicle,
        { T("settings.enter_vehicle.detail") })) {
        gSettings.Save();
    }
}
}

void OnMenuOpen() {
    gScenePreflightCache.clear();
    ReloadUserData();
}

void OnMenuExit() {
}

void UpdateMenu() {
    gMenu.CheckKeys();
    int footerOptionCount = 0;
    if (gMenu.CurrentMenu("mainmenu")) {
        UpdateMainMenu();
        footerOptionCount = 5;
    }
    if (gMenu.CurrentMenu("pedsmenu")) {
        UpdateCatalogMenu("menu.peds.title", gCatalogs.Peds());
        footerOptionCount = std::max(1, static_cast<int>(gCatalogs.Peds().size()));
    }
    if (gMenu.CurrentMenu("weaponsmenu")) {
        UpdateCatalogMenu("menu.weapons.title", gCatalogs.Weapons());
        footerOptionCount = std::max(1, static_cast<int>(gCatalogs.Weapons().size()));
    }
    if (gMenu.CurrentMenu("vehiclesmenu")) {
        UpdateCatalogMenu("menu.vehicles.title", gCatalogs.Vehicles());
        footerOptionCount = std::max(1, static_cast<int>(gCatalogs.Vehicles().size()));
    }
    if (gMenu.CurrentMenu("scenesmenu")) {
        UpdateScenesMenu();
        footerOptionCount = std::max(1, static_cast<int>(gCatalogs.Scenes().size()) +
            (!SceneLoader::ActiveSessions().empty() ? 1 : 0));
    }
    if (gMenu.CurrentMenu("scenesessionsmenu")) {
        UpdateSceneSessionsMenu();
        footerOptionCount = std::max(1,
            static_cast<int>(SceneLoader::ActiveSessions().size()) + 1);
    }
    if (gMenu.CurrentMenu("settingsmenu")) {
        UpdateSettingsMenu();
        footerOptionCount = 2;
    }
    gMenu.EndMenu();
    DrawFooterBrand(footerOptionCount);
}
