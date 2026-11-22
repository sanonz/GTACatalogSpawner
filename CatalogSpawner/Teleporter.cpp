#include "Teleporter.h"

#include "Language.h"
#include "Logger.h"
#include "SceneLoader.h"
#include "TextInput.h"
#include "Utils.h"

#include <Windows.h>
#include <inc/main.h>
#include <inc/natives.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <sstream>
#include <vector>

namespace {
constexpr DWORD kCollisionTimeoutMs = 900;
constexpr DWORD kGroundProbeTimeoutMs = 1800;
constexpr float kMinCoordLimit = -20000.0f;
constexpr float kMaxCoordLimit = 20000.0f;

Entity TeleportTarget(bool& isVehicle) {
    isVehicle = false;
    const Ped playerPed = PLAYER::PLAYER_PED_ID();
    if (!ENTITY::DOES_ENTITY_EXIST(playerPed))
        return 0;

    if (PED::IS_PED_IN_ANY_VEHICLE(playerPed, false)) {
        const Vehicle vehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, false);
        if (ENTITY::DOES_ENTITY_EXIST(vehicle)) {
            isVehicle = true;
            return vehicle;
        }
    }
    return playerPed;
}

std::string LookupStreetName(Vector3 pos) {
    Hash streetHash = 0;
    Hash crossingHash = 0;
    PATHFIND::GET_STREET_NAME_AT_COORD(pos, &streetHash, &crossingHash);
    if (streetHash == 0)
        return {};
    const char* street = HUD::GET_STREET_NAME_FROM_HASH_KEY(streetHash);
    if (!street || street[0] == '\0' || std::string(street) == "NULL")
        return {};
    return Utils::Trim(street);
}

std::string LookupZoneName(Vector3 pos) {
    const char* zoneCode = ZONE::GET_NAME_OF_ZONE(pos);
    if (!zoneCode || zoneCode[0] == '\0')
        return {};
    std::string localized = Utils::GxtText(zoneCode);
    if (!localized.empty())
        return Utils::Trim(localized);
    return Utils::Trim(zoneCode);
}

bool ProbeGroundZ(Entity target, Vector3& destination) {
    static constexpr std::array<float, 21> kProbeHeights = {
        100.0f, 150.0f, 50.0f, 0.0f, 200.0f, 250.0f, 300.0f,
        350.0f, 400.0f, 450.0f, 500.0f, 550.0f, 600.0f, 650.0f,
        700.0f, 750.0f, 800.0f, 850.0f, 900.0f, 950.0f, 1000.0f
    };

    const ULONGLONG started = GetTickCount64();
    for (const float probeHeight : kProbeHeights) {
        if (GetTickCount64() - started > kGroundProbeTimeoutMs)
            break;

        const Vector3 probePos{ destination.x, destination.y, probeHeight };
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(target, probePos, false, false, false);
        STREAMING::REQUEST_COLLISION_AT_COORD(probePos);
        SceneLoader::Tick();
        WAIT(0);

        float groundZ = 0.0f;
        if (MISC::GET_GROUND_Z_FOR_3D_COORD(probePos, &groundZ, false, false)) {
            destination.z = groundZ + 1.0f;
            return true;
        }
    }

    float waterZ = 0.0f;
    if (WATER::GET_WATER_HEIGHT({ destination.x, destination.y, 100.0f }, &waterZ)) {
        destination.z = waterZ + 1.5f;
        return true;
    }

    if (std::fabs(destination.z) < 0.001f)
        destination.z = 5.0f;
    return false;
}
}

std::optional<std::string> Teleporter::PromptTextInput(const std::string& title,
    const std::string& defaultText, int maxLength) {
    return TextInput::Prompt(title, defaultText, maxLength);
}

bool Teleporter::ParseFloat(const std::string& raw, float& output) {
    const std::string trimmed = Utils::Trim(raw);
    if (trimmed.empty())
        return false;
    char* end = nullptr;
    const float value = std::strtof(trimmed.c_str(), &end);
    if (end != trimmed.c_str() + trimmed.size() || !std::isfinite(value))
        return false;
    if (value < kMinCoordLimit || value > kMaxCoordLimit)
        return false;
    output = value;
    return true;
}

bool Teleporter::ParseCoordinates(const std::string& raw, Vector3& output) {
    std::string normalized = raw;
    for (char& ch : normalized) {
        if (ch == ',' || ch == ';' || ch == '(' || ch == ')' || ch == '[' || ch == ']')
            ch = ' ';
    }

    std::istringstream stream(normalized);
    std::vector<float> values;
    std::string token;
    while (stream >> token) {
        float parsed = 0.0f;
        if (!ParseFloat(token, parsed))
            return false;
        values.push_back(parsed);
        if (values.size() > 3)
            return false;
    }

    if (values.size() != 2 && values.size() != 3)
        return false;

    output.x = values[0];
    output.y = values[1];
    output.z = values.size() == 3 ? values[2] : 0.0f;
    return true;
}

Vector3 Teleporter::CurrentPosition() {
    bool isVehicle = false;
    const Entity target = TeleportTarget(isVehicle);
    if (!ENTITY::DOES_ENTITY_EXIST(target))
        return {};
    return ENTITY::GET_ENTITY_COORDS(target, true);
}

SavedLocation Teleporter::CaptureCurrentLocation() {
    SavedLocation location;
    bool isVehicle = false;
    const Entity target = TeleportTarget(isVehicle);
    if (!ENTITY::DOES_ENTITY_EXIST(target))
        return location;

    location.Position = ENTITY::GET_ENTITY_COORDS(target, true);
    location.Heading = ENTITY::GET_ENTITY_HEADING(target);
    location.HasHeading = true;

    const std::string street = LookupStreetName(location.Position);
    const std::string zone = LookupZoneName(location.Position);
    location.Zone = !zone.empty() ? zone : street;
    location.Name = !street.empty() ? street : zone;
    return location;
}

bool Teleporter::TeleportTo(Vector3 destination, std::optional<float> heading,
    bool autoGroundZ, const std::string& label) {
    bool isVehicle = false;
    const Entity target = TeleportTarget(isVehicle);
    if (!ENTITY::DOES_ENTITY_EXIST(target))
        return false;

    ENTITY::FREEZE_ENTITY_POSITION(target, true);
    ENTITY::SET_ENTITY_VELOCITY(target, { 0.0f, 0.0f, 0.0f });
    STREAMING::REQUEST_COLLISION_AT_COORD(destination);
    const bool sceneStarted = STREAMING::NEW_LOAD_SCENE_START_SPHERE(destination, 50.0f, 0) != FALSE;

    if (autoGroundZ) {
        ProbeGroundZ(target, destination);
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(target, destination, false, false, false);
    }
    else {
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(target, destination, false, false, false);
        const ULONGLONG started = GetTickCount64();
        while (GetTickCount64() - started < kCollisionTimeoutMs) {
            STREAMING::REQUEST_COLLISION_AT_COORD(destination);
            const bool collisionReady = ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(target) != FALSE;
            const bool sceneReady = !sceneStarted || STREAMING::IS_NEW_LOAD_SCENE_LOADED() != FALSE;
            if (collisionReady && sceneReady)
                break;
            SceneLoader::Tick();
            WAIT(0);
        }
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(target, destination, false, false, false);
    }

    if (heading.has_value()) {
        ENTITY::SET_ENTITY_HEADING(target, *heading);
        CAM::SET_GAMEPLAY_CAM_RELATIVE_HEADING(0.0f);
    }

    if (isVehicle && autoGroundZ)
        VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(target, 5.0f);

    ENTITY::SET_ENTITY_VELOCITY(target, { 0.0f, 0.0f, 0.0f });
    ENTITY::FREEZE_ENTITY_POSITION(target, false);
    if (sceneStarted)
        STREAMING::NEW_LOAD_SCENE_STOP();

    const std::string displayTarget = label.empty()
        ? std::format("{:.2f}, {:.2f}, {:.2f}", destination.x, destination.y, destination.z)
        : label;
    Utils::ShowSubtitle(gLanguage.Format("subtitle.teleported", {
        { "target", displayTarget },
    }));
    LOG_INFO("[Teleport] Teleported target={} vehicle={} to ({:.2f}, {:.2f}, {:.2f}) autoGround={}",
        target, isVehicle, destination.x, destination.y, destination.z, autoGroundZ);
    return true;
}

bool Teleporter::TeleportToWaypoint() {
    if (!HUD::IS_WAYPOINT_ACTIVE()) {
        Utils::ShowSubtitle(gLanguage.Text("subtitle.teleport_no_waypoint"));
        return false;
    }

    const Blip blip = HUD::GET_FIRST_BLIP_INFO_ID(8);
    if (!HUD::DOES_BLIP_EXIST(blip)) {
        Utils::ShowSubtitle(gLanguage.Text("subtitle.teleport_no_waypoint"));
        return false;
    }

    Vector3 coords = HUD::GET_BLIP_COORDS(blip);
    coords.z = 0.0f;
    return TeleportTo(coords, std::nullopt, true, gLanguage.Text("menu.teleport.waypoint"));
}
