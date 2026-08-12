#include "Spawner.h"

#include "Language.h"
#include "Logger.h"
#include "Utils.h"

#include <Windows.h>
#include <inc/main.h>
#include <inc/natives.h>

#include <algorithm>

namespace {
constexpr DWORD kModelTimeoutMs = 5000;

bool LoadModel(Hash hash, const std::string& spawnName) {
    STREAMING::REQUEST_MODEL(hash);
    const ULONGLONG started = GetTickCount64();
    while (!STREAMING::HAS_MODEL_LOADED(hash)) {
        WAIT(0);
        if (GetTickCount64() - started > kModelTimeoutMs) {
            STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(hash);
            Utils::ShowSubtitle(gLanguage.Format("subtitle.model_load_failed", {
                { "model", spawnName },
            }));
            LOG_WARNING("Model load timed out: {} ({})", spawnName, Utils::HexHash(hash));
            return false;
        }
    }
    return true;
}
}

bool Spawner::SwitchPlayerModel(const CatalogItem& item) {
    const Hash hash = item.ModelHash();
    if (!STREAMING::IS_MODEL_IN_CDIMAGE(hash) || !STREAMING::IS_MODEL_A_PED(hash)) {
        Utils::ShowSubtitle(gLanguage.Format("subtitle.ped_missing", {
            { "model", item.SpawnName },
        }));
        LOG_WARNING("Invalid ped model: {} ({})", item.SpawnName, Utils::HexHash(hash));
        return false;
    }
    if (!LoadModel(hash, item.SpawnName))
        return false;

    const Player player = PLAYER::PLAYER_ID();
    const Ped previousPed = PLAYER::PLAYER_PED_ID();
    const Vehicle previousVehicle = PED::GET_VEHICLE_PED_IS_IN(previousPed, false);
    const bool wasDriver = ENTITY::DOES_ENTITY_EXIST(previousVehicle) &&
        VEHICLE::GET_PED_IN_VEHICLE_SEAT(previousVehicle, -1, false) == previousPed;

    PLAYER::SET_PLAYER_MODEL(player, hash);
    WAIT(0);
    const Ped newPed = PLAYER::PLAYER_PED_ID();
    if (ENTITY::DOES_ENTITY_EXIST(newPed)) {
        PED::SET_PED_DEFAULT_COMPONENT_VARIATION(newPed);
        if (wasDriver && ENTITY::DOES_ENTITY_EXIST(previousVehicle))
            PED::SET_PED_INTO_VEHICLE(newPed, previousVehicle, -1);
    }
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(hash);

    Utils::ShowSubtitle(gLanguage.Format("subtitle.ped_switched", {
        { "name", item.DisplayName },
    }));
    LOG_INFO("Player model changed to {} ({})", item.SpawnName, Utils::HexHash(hash));
    return true;
}

bool Spawner::GiveWeapon(const CatalogItem& item) {
    const Hash hash = item.ModelHash();
    if (!WEAPON::IS_WEAPON_VALID(hash)) {
        Utils::ShowSubtitle(gLanguage.Format("subtitle.weapon_invalid", {
            { "model", item.SpawnName },
        }));
        LOG_WARNING("Invalid weapon: {} ({})", item.SpawnName, Utils::HexHash(hash));
        return false;
    }

    const Ped playerPed = PLAYER::PLAYER_PED_ID();
    WEAPON::GIVE_WEAPON_TO_PED(playerPed, hash, std::max(0, item.Ammo), false, true);
    Utils::ShowSubtitle(gLanguage.Format("subtitle.weapon_given", {
        { "name", item.DisplayName },
    }));
    LOG_INFO("Gave weapon {} ({}) with {} ammo", item.SpawnName, Utils::HexHash(hash), item.Ammo);
    return true;
}

bool Spawner::SpawnVehicle(const CatalogItem& item, bool enterVehicle) {
    const Hash hash = item.ModelHash();
    if (!STREAMING::IS_MODEL_IN_CDIMAGE(hash) || !STREAMING::IS_MODEL_A_VEHICLE(hash)) {
        Utils::ShowSubtitle(gLanguage.Format("subtitle.vehicle_missing", {
            { "model", item.SpawnName },
        }));
        LOG_WARNING("Invalid vehicle model: {} ({})", item.SpawnName, Utils::HexHash(hash));
        return false;
    }
    if (!LoadModel(hash, item.SpawnName))
        return false;

    const Ped playerPed = PLAYER::PLAYER_PED_ID();
    const Vehicle currentVehicle = PED::GET_VEHICLE_PED_IS_IN(playerPed, false);
    Vector3 newMin{};
    Vector3 newMax{};
    MISC::GET_MODEL_DIMENSIONS(hash, &newMin, &newMax);
    float rightOffset = std::max(2.0f, (newMax.x - newMin.x) / 2.0f + 1.5f);
    if (ENTITY::DOES_ENTITY_EXIST(currentVehicle)) {
        Vector3 oldMin{};
        Vector3 oldMax{};
        MISC::GET_MODEL_DIMENSIONS(ENTITY::GET_ENTITY_MODEL(currentVehicle), &oldMin, &oldMax);
        rightOffset += std::max(0.0f, (oldMax.x - oldMin.x) / 2.0f);
    }

    const Vector3 position = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(
        playerPed, { rightOffset, 0.0f, 0.0f });
    Vehicle vehicle = VEHICLE::CREATE_VEHICLE(hash, position,
        ENTITY::GET_ENTITY_HEADING(playerPed), false, true, false);
    if (!ENTITY::DOES_ENTITY_EXIST(vehicle)) {
        STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(hash);
        Utils::ShowSubtitle(gLanguage.Format("subtitle.vehicle_create_failed", {
            { "model", item.SpawnName },
        }));
        LOG_ERROR("CREATE_VEHICLE returned no entity for {}", item.SpawnName);
        return false;
    }

    VEHICLE::SET_VEHICLE_ON_GROUND_PROPERLY(vehicle, 5.0f);
    VEHICLE::SET_VEHICLE_DIRT_LEVEL(vehicle, 0.0f);
    if (enterVehicle)
        PED::SET_PED_INTO_VEHICLE(PLAYER::PLAYER_PED_ID(), vehicle, -1);

    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(hash);
    ENTITY::SET_ENTITY_AS_MISSION_ENTITY(vehicle, false, true);
    ENTITY::SET_ENTITY_AS_NO_LONGER_NEEDED(&vehicle);
    Utils::ShowSubtitle(gLanguage.Format("subtitle.vehicle_spawned", {
        { "name", item.DisplayName },
    }));
    LOG_INFO("Spawned vehicle {} ({}) enter={}", item.SpawnName, Utils::HexHash(hash), enterVehicle);
    return true;
}
