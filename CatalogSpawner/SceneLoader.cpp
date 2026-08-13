#include "SceneLoader.h"

#include "Language.h"
#include "Logger.h"
#include "Paths.h"
#include "SceneTasks.h"
#include "SceneXml.h"
#include "Utils.h"

#include <Windows.h>
#include <inc/main.h>
#include <inc/natives.h>
#include <mmsystem.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
constexpr DWORD kAssetTimeoutMs = 5000;
constexpr std::size_t kPlacementYieldBatchSize = 32;

struct Attachment {
  bool Enabled = false;
  std::int64_t Target = 0;
  int Bone = 0;
  Vector3 Offset{};
  Vector3 Rotation{};
};

struct Placement {
  std::size_t Index = 0;
  SceneXmlNode Xml;
  Hash Model = 0;
  int Type = 0;
  bool Dynamic = false;
  bool Frozen = true;
  std::int64_t InitialHandle = 0;
  Vector3 Position{};
  Vector3 Rotation{};
  Attachment Attach;
};

struct Marker {
  std::string Name;
  std::int64_t InitialHandle = 0;
  std::int64_t LinkToHandle = 0;
  int Type = 0;
  float Scale = 0.9f;
  bool ShowName = false;
  bool Rotate = true;
  bool AllowVehicles = false;
  int R = 102, G = 0, B = 204, A = 190;
  Vector3 Position{};
  Vector3 Rotation{};
  Vector3 Destination{};
  float DestinationHeading = 0.0f;
  std::int64_t AttachedTo = 0;
  Vector3 AttachOffset{};
  std::int64_t DestinationAttachedTo = 0;
  Vector3 DestinationOffset{};
  bool WasInside = false;
};

struct VehicleAuditEntry {
  std::size_t PlacementIndex = 0;
  Entity Handle = 0;
  Hash ExpectedModel = 0;
  std::string HashName;
  Vector3 ExpectedPosition{};
  bool ExpectedFrozen = false;
  bool ExpectedVisible = true;
  bool ExpectedCollision = true;
};

struct Session {
  std::uint64_t Id = 0;
  std::string Name;
  std::filesystem::path Source;
  std::vector<Entity> Entities;
  std::unordered_map<std::int64_t, Entity> Handles;
  std::vector<SceneTaskSequence> Tasks;
  std::vector<Marker> Markers;
  std::vector<Blip> Blips;
  std::vector<int> ParticleFx;
  std::vector<VehicleAuditEntry> VehicleAudit;
  std::vector<std::string> Failures;
  std::unordered_map<std::string, std::size_t> FailureCounts;
  std::string AudioAlias;
  ULONGLONG MarkerCooldownUntil = 0;
  ULONGLONG VehicleAuditStartedAt = 0;
  bool VehicleAudit1sComplete = false;
  bool VehicleAudit5sComplete = false;
};

std::vector<std::unique_ptr<Session>> gSessions;
std::uint64_t gNextSessionId = 1;
std::string gActiveAudioAlias;

Vector3 Vector(const SceneXmlNode *node, const char *child = nullptr) {
  if (!node)
    return {};
  const SceneXmlNode *value = child ? node->Child(child) : node;
  return value
             ? Vector3{value->Float("X"), value->Float("Y"), value->Float("Z")}
             : Vector3{};
}

std::vector<int> CsvInts(const std::string &value) {
  std::vector<int> result;
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t end = value.find(',', start);
    const std::string part = Utils::Trim(value.substr(
        start, end == std::string::npos ? std::string::npos : end - start));
    int number = 0;
    const auto parsed =
        std::from_chars(part.data(), part.data() + part.size(), number);
    result.push_back(parsed.ec == std::errc{} ? number : 0);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return result;
}

int NodeIndex(const SceneXmlNode &node, int fallback) {
  if (node.Name.size() > 1 && node.Name[0] == '_') {
    int value = fallback;
    const auto parsed = std::from_chars(
        node.Name.data() + 1, node.Name.data() + node.Name.size(), value);
    if (parsed.ec == std::errc{})
      return value;
  }
  return fallback;
}

bool TextBool(const std::string &value, bool fallback = false) {
  const std::string normalized = Utils::ToLower(Utils::Trim(value));
  if (normalized == "true" || normalized == "1")
    return true;
  if (normalized == "false" || normalized == "0")
    return false;
  return fallback;
}

bool RequestAnimationDictionary(const std::string &dictionary) {
  if (dictionary.empty())
    return false;
  STREAMING::REQUEST_ANIM_DICT(dictionary.c_str());
  const ULONGLONG started = GetTickCount64();
  while (!STREAMING::HAS_ANIM_DICT_LOADED(dictionary.c_str()) &&
         GetTickCount64() - started < kAssetTimeoutMs)
    WAIT(0);
  const bool loaded = STREAMING::HAS_ANIM_DICT_LOADED(dictionary.c_str());
  if (!loaded)
    LOG_WARNING("[Scene] Animation dictionary timeout dictionary='{}'",
                dictionary);
  return loaded;
}

bool RequestAnimationSet(const std::string &animationSet) {
  if (animationSet.empty())
    return false;
  STREAMING::REQUEST_ANIM_SET(animationSet.c_str());
  const ULONGLONG started = GetTickCount64();
  while (!STREAMING::HAS_ANIM_SET_LOADED(animationSet.c_str()) &&
         GetTickCount64() - started < kAssetTimeoutMs)
    WAIT(0);
  const bool loaded = STREAMING::HAS_ANIM_SET_LOADED(animationSet.c_str());
  if (!loaded)
    LOG_WARNING("[Scene] Animation set timeout set='{}'", animationSet);
  return loaded;
}

void PlayPedAnimation(Ped ped, const std::string &dictionary,
                      const std::string &name, float speed,
                      float speedMultiplier, int duration, int flag,
                      float playbackRate, bool lockPosition) {
  if (name.empty() || !RequestAnimationDictionary(dictionary))
    return;
  TASK::TASK_PLAY_ANIM(ped, dictionary.c_str(), name.c_str(), speed,
                       speedMultiplier, duration, flag, playbackRate,
                       lockPosition, lockPosition, lockPosition);
}

void EnsureSpoonerRelationshipGroups() {
  static bool initialized = false;
  if (initialized)
    return;
  initialized = true;

  Hash friends = Utils::Joaat("SPOONER_FRIENDS");
  Hash enemies = Utils::Joaat("SPOONER_ENEMIES");
  Hash neutral = Utils::Joaat("SPOONER_NEUTRAL");
  PED::ADD_RELATIONSHIP_GROUP("SPOONER_FRIENDS", &friends);
  PED::ADD_RELATIONSHIP_GROUP("SPOONER_ENEMIES", &enemies);
  PED::ADD_RELATIONSHIP_GROUP("SPOONER_NEUTRAL", &neutral);
  const Hash player = Utils::Joaat("PLAYER");
  const auto setBoth = [](int relationship, Hash first, Hash second) {
    PED::SET_RELATIONSHIP_BETWEEN_GROUPS(relationship, first, second);
    PED::SET_RELATIONSHIP_BETWEEN_GROUPS(relationship, second, first);
  };
  setBoth(3, friends, neutral);
  setBoth(3, enemies, neutral);
  setBoth(5, friends, enemies);
  setBoth(0, friends, player);
}

const SceneXmlNode *PlacementTaskSequence(const SceneXmlNode &placement) {
  if (const auto *ped = placement.Child("PedProperties"))
    if (const auto *tasks = ped->Child("TaskSequence"))
      return tasks;
  return placement.Child("TaskSequence");
}

std::int64_t TargetId(const std::string &raw) {
  const std::string value = Utils::ToLower(Utils::Trim(raw));
  if (value == "player")
    return -1;
  if (value == "vehicle")
    return -2;
  std::int64_t parsed = 0;
  std::from_chars(value.data(), value.data() + value.size(), parsed);
  return parsed;
}

Entity ResolveTarget(const Session &session, std::int64_t id) {
  if (id == -1)
    return PLAYER::PLAYER_PED_ID();
  if (id == -2)
    return PED::GET_VEHICLE_PED_IS_IN(PLAYER::PLAYER_PED_ID(), false);
  const auto found = session.Handles.find(id);
  return found == session.Handles.end() ? 0 : found->second;
}

enum class ModelLoadResult { Loaded, NotInCdImage, TimedOut };

const char *PlacementTypeName(int type) {
  switch (type) {
  case 1:
    return "ped";
  case 2:
    return "vehicle";
  case 3:
    return "prop";
  default:
    return "invalid";
  }
}

void RecordFailure(Session &session, std::string_view category,
                   const std::string &detail) {
  session.Failures.push_back(detail);
  ++session.FailureCounts[std::string(category)];
  LOG_WARNING("[Scene] Session {} failure category={} {}", session.Id,
              category, detail);
}

void AuditVehicles(Session &session, std::string_view stage,
                   ULONGLONG elapsedMs) {
  std::size_t existingCount = 0, missingCount = 0, modelMismatchCount = 0,
              invisibleCount = 0, notMissionCount = 0,
              collisionDisabledCount = 0, collisionNotLoadedCount = 0,
              waitingForCollisionCount = 0, movedOver1mCount = 0,
              movedOver10mCount = 0;
  float maximumDistance = 0.0f;

  LOG_INFO("[SceneAudit] Session {} vehicles stage={} elapsed_ms={} begin "
           "tracked={}",
           session.Id, stage, elapsedMs, session.VehicleAudit.size());
  for (const VehicleAuditEntry &item : session.VehicleAudit) {
    if (!ENTITY::DOES_ENTITY_EXIST(item.Handle)) {
      ++missingCount;
      LOG_WARNING(
          "[SceneAudit] Session {} stage={} placement={} entity={} exists=false "
          "expected_model={} hash_name='{}' expected_pos=({:.3f},{:.3f},{:.3f})",
          session.Id, stage, item.PlacementIndex, item.Handle,
          Utils::HexHash(item.ExpectedModel), item.HashName,
          item.ExpectedPosition.x, item.ExpectedPosition.y,
          item.ExpectedPosition.z);
      continue;
    }

    ++existingCount;
    const Hash actualModel = ENTITY::GET_ENTITY_MODEL(item.Handle);
    const Vector3 actualPosition = ENTITY::GET_ENTITY_COORDS(item.Handle, true);
    const Vector3 velocity = ENTITY::GET_ENTITY_VELOCITY(item.Handle);
    const float dx = actualPosition.x - item.ExpectedPosition.x,
                dy = actualPosition.y - item.ExpectedPosition.y,
                dz = actualPosition.z - item.ExpectedPosition.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    maximumDistance = std::max(maximumDistance, distance);
    if (actualModel != item.ExpectedModel)
      ++modelMismatchCount;
    if (!ENTITY::IS_ENTITY_VISIBLE(item.Handle))
      ++invisibleCount;
    if (!ENTITY::IS_ENTITY_A_MISSION_ENTITY(item.Handle))
      ++notMissionCount;
    const bool collisionEnabled =
        !ENTITY::GET_ENTITY_COLLISION_DISABLED(item.Handle);
    if (!collisionEnabled)
      ++collisionDisabledCount;
    const bool collisionLoaded =
        ENTITY::HAS_COLLISION_LOADED_AROUND_ENTITY(item.Handle);
    if (!collisionLoaded)
      ++collisionNotLoadedCount;
    const bool waitingForCollision =
        ENTITY::IS_ENTITY_WAITING_FOR_WORLD_COLLISION(item.Handle);
    if (waitingForCollision)
      ++waitingForCollisionCount;
    if (distance > 1.0f)
      ++movedOver1mCount;
    if (distance > 10.0f)
      ++movedOver10mCount;

    const bool anomalous = actualModel != item.ExpectedModel ||
                           !ENTITY::IS_ENTITY_VISIBLE(item.Handle) ||
                           !ENTITY::IS_ENTITY_A_MISSION_ENTITY(item.Handle) ||
                           collisionEnabled != item.ExpectedCollision ||
                           waitingForCollision || distance > 10.0f;
    const std::string detail = std::format(
        "[SceneAudit] Session {} stage={} placement={} entity={} exists=true "
        "expected_model={} actual_model={} hash_name='{}' "
        "expected_pos=({:.3f},{:.3f},{:.3f}) "
        "actual_pos=({:.3f},{:.3f},{:.3f}) "
        "delta=({:.3f},{:.3f},{:.3f}) distance={:.3f} "
        "velocity=({:.3f},{:.3f},{:.3f}) visible={} mission={} "
        "expected_frozen={} static={} expected_collision={} "
        "collision_enabled={} collision_loaded={} waiting_for_collision={}",
        session.Id, stage, item.PlacementIndex, item.Handle,
        Utils::HexHash(item.ExpectedModel), Utils::HexHash(actualModel),
        item.HashName, item.ExpectedPosition.x, item.ExpectedPosition.y,
        item.ExpectedPosition.z, actualPosition.x, actualPosition.y,
        actualPosition.z, dx, dy, dz, distance, velocity.x, velocity.y,
        velocity.z, ENTITY::IS_ENTITY_VISIBLE(item.Handle),
        ENTITY::IS_ENTITY_A_MISSION_ENTITY(item.Handle), item.ExpectedFrozen,
        ENTITY::IS_ENTITY_STATIC(item.Handle), item.ExpectedCollision,
        collisionEnabled, collisionLoaded, waitingForCollision);
    if (anomalous)
      LOG_WARNING("{}", detail);
    else
      LOG_DEBUG("{}", detail);
  }

  LOG_INFO(
      "[SceneAudit] Session {} vehicles stage={} elapsed_ms={} summary "
      "tracked={} existing={} missing={} model_mismatch={} invisible={} "
      "not_mission={} collision_disabled={} collision_not_loaded={} "
      "waiting_for_collision={} moved_over_1m={} moved_over_10m={} "
      "max_distance={:.3f}",
      session.Id, stage, elapsedMs, session.VehicleAudit.size(), existingCount,
      missingCount, modelMismatchCount, invisibleCount, notMissionCount,
      collisionDisabledCount, collisionNotLoadedCount,
      waitingForCollisionCount, movedOver1mCount, movedOver10mCount,
      maximumDistance);
}

ModelLoadResult LoadModel(Hash model, ULONGLONG &elapsedMs) {
  const ULONGLONG started = GetTickCount64();
  if (!STREAMING::IS_MODEL_IN_CDIMAGE(model))
    return ModelLoadResult::NotInCdImage;
  STREAMING::REQUEST_MODEL(model);
  while (!STREAMING::HAS_MODEL_LOADED(model)) {
    WAIT(0);
    if (GetTickCount64() - started > kAssetTimeoutMs) {
      elapsedMs = GetTickCount64() - started;
      STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
      return ModelLoadResult::TimedOut;
    }
  }
  elapsedMs = GetTickCount64() - started;
  return ModelLoadResult::Loaded;
}

void ApplyPedProperties(Ped ped, const SceneXmlNode &p) {
  const bool networked = NETWORK::NETWORK_IS_GAME_IN_PROGRESS();
  PED::SET_BLOCKING_OF_NON_TEMPORARY_EVENTS(ped, p.Bool("IsStill", true));
  PED::SET_PED_CAN_RAGDOLL(ped, p.Bool("CanRagdoll", true));
  PED::SET_PED_RAGDOLL_ON_COLLISION(ped, p.Bool("CanRagdoll", false));
  PED::SET_PED_ARMOUR(ped, std::max(0, p.Int("Armour")));
  if (p.Bool("HasShortHeight"))
    PED::SET_PED_CONFIG_FLAG(ped, 223, true);
  const Hash weapon = SceneXmlHash(p.Value("CurrentWeapon"));
  if (weapon && WEAPON::IS_WEAPON_VALID(weapon))
    WEAPON::GIVE_WEAPON_TO_PED(ped, weapon, 9999, false, true);
  PED::SET_PED_CAN_SWITCH_WEAPON(ped, false);
  TASK::SET_PED_PATH_CAN_USE_CLIMBOVERS(ped, true);
  TASK::SET_PED_PATH_CAN_USE_LADDERS(ped, true);
  TASK::SET_PED_PATH_CAN_DROP_FROM_HEIGHT(ped, true);
  PED::SET_PED_COMBAT_ABILITY(ped, 2);
  PED::SET_PED_COMBAT_MOVEMENT(ped, 2);
  PED::SET_PED_CAN_PLAY_AMBIENT_ANIMS(ped, true);
  PED::SET_PED_CAN_PLAY_AMBIENT_BASE_ANIMS(ped, true);
  PED::SET_PED_CAN_PLAY_GESTURE_ANIMS(ped, true);
  PED::SET_PED_CAN_PLAY_VISEME_ANIMS(ped, true, true);
  PED::SET_PED_IS_IGNORED_BY_AUTO_OPEN_DOORS(ped, true);

  if (const auto *props = p.Child("PedProps")) {
    int fallback = 0;
    for (const auto &item : props->Children) {
      const int index = NodeIndex(item, fallback++);
      const auto values = CsvInts(item.Text);
      if (!values.empty()) {
        if (values[0] < 0)
          PED::CLEAR_PED_PROP(ped, index, 0);
        else
          PED::SET_PED_PROP_INDEX(ped, index, values[0],
                                  values.size() > 1 ? values[1] : 0, networked,
                                  0);
      }
    }
  }
  if (const auto *comps = p.Child("PedComps")) {
    int fallback = 0;
    for (const auto &item : comps->Children) {
      const int index = NodeIndex(item, fallback++);
      const auto values = CsvInts(item.Text);
      if (!values.empty())
        PED::SET_PED_COMPONENT_VARIATION(ped, index, values[0],
                                         values.size() > 1 ? values[1] : 0, 0);
    }
  }
  if (const auto *flags = p.Child("PedConfigFlags")) {
    int fallback = 0;
    for (const auto &item : flags->Children)
      PED::SET_PED_CONFIG_FLAG(ped, NodeIndex(item, fallback++),
                               TextBool(item.Text));
  }
  if (const auto *head = p.Child("HeadFeatures")) {
    if (const auto *blend = head->Child("ShapeAndSkinTone"))
      PED::SET_PED_HEAD_BLEND_DATA(
          ped, blend->Int("ShapeFatherId"), blend->Int("ShapeMotherId"),
          blend->Int("ShapeOverrideId"), blend->Int("ToneFatherId"),
          blend->Int("ToneMotherId"), blend->Int("ToneOverrideId"),
          blend->Float("ShapeVal"), blend->Float("ToneVal"),
          blend->Float("OverrideVal"), blend->Bool("IsP"));
    PED::SET_PED_HAIR_TINT(ped, head->Int("HairColour"),
                           head->Int("HairColourStreaks"));
    PED::SET_HEAD_BLEND_EYE_COLOR(ped, head->Int("EyeColour"));
    if (const auto *facial = head->Child("FacialFeatures")) {
      int fallback = 0;
      for (const auto &item : facial->Children)
        PED::SET_PED_MICRO_MORPH(ped, NodeIndex(item, fallback++),
                                 std::strtof(item.Text.c_str(), nullptr));
    }
    if (const auto *overlays = head->Child("Overlays")) {
      int fallback = 0;
      for (const auto &item : overlays->Children) {
        const int index = NodeIndex(item, fallback++);
        PED::SET_PED_HEAD_OVERLAY(ped, index, item.Int("index"),
                                  item.Float("opacity", 1.0f));
        PED::SET_PED_HEAD_OVERLAY_TINT(
            ped, index, index == 1 || index == 2 || index == 10 ? 1 : 2,
            item.Int("colour"), item.Int("colourSecondary"));
      }
    }
  }
  if (const auto *tattoos = p.Child("TattooLogoDecals")) {
    for (const auto &decal : tattoos->Children) {
      const Hash collection = SceneXmlHash(decal.Attribute("collection"));
      const Hash overlay = SceneXmlHash(decal.Attribute("value"));
      if (collection && overlay)
        PED::ADD_PED_DECORATION_FROM_HASHES(ped, collection, overlay);
    }
  }
  if (const auto *packs = p.Child("DamagePacks"))
    for (const auto &pack : packs->Children)
      PED::APPLY_PED_DAMAGE_PACK(ped, Utils::Trim(pack.Text).c_str(), 1.0f,
                                 1.0f);
  if (p.Child("RelationshipGroup")) {
    EnsureSpoonerRelationshipGroups();
    const Hash relationship = SceneXmlHash(p.Value("RelationshipGroup"));
    if (relationship)
      PED::SET_PED_RELATIONSHIP_GROUP_HASH(ped, relationship);
  }
  const std::string movement = p.Value("MovementGroupName");
  if (RequestAnimationSet(movement))
    PED::SET_PED_MOVEMENT_CLIPSET(ped, movement.c_str(), 0.25f);
  const std::string weaponMovement = p.Value("WeaponMovementGroupName");
  if (RequestAnimationSet(weaponMovement))
    PED::SET_PED_WEAPON_MOVEMENT_CLIPSET(ped, weaponMovement.c_str());
  if (p.Bool("ScenarioActive"))
    TASK::TASK_START_SCENARIO_IN_PLACE(ped, p.Value("ScenarioName").c_str(), -1,
                                       true);
  if (const auto *animations = p.Child("Animations")) {
    for (const auto &anim : animations->Children)
      PlayPedAnimation(ped, anim.Value("Dict"), anim.Value("Name"),
                       anim.Float("Speed", 4.0f),
                       anim.Float("SpeedMultiplier", -4.0f),
                       anim.Int("Duration", -1), anim.Int("Flag", 1),
                       anim.Float("PlaybackRate"), anim.Bool("LockPos"));
  } else if (p.Bool("AnimActive")) {
    PlayPedAnimation(ped, p.Value("AnimDict"), p.Value("AnimName"), 4.0f,
                     -4.0f, -1, 1, 0.0f, false);
  }
  const std::string facialMood = p.Value("FacialMood");
  if (!facialMood.empty())
    PED::SET_FACIAL_IDLE_ANIM_OVERRIDE(ped, facialMood.c_str(), nullptr);
}

void ApplyVehicleProperties(Vehicle vehicle, const SceneXmlNode &p) {
  VEHICLE::SET_VEHICLE_MOD_KIT(vehicle, 0);
  VEHICLE::SET_VEHICLE_LIVERY(vehicle, p.Int("Livery", -1));
  VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT(vehicle,
                                         p.Value("NumberPlateText").c_str());
  VEHICLE::SET_VEHICLE_NUMBER_PLATE_TEXT_INDEX(vehicle,
                                               p.Int("NumberPlateIndex"));
  VEHICLE::SET_VEHICLE_WHEEL_TYPE(vehicle, p.Int("WheelType"));
  VEHICLE::SET_VEHICLE_WINDOW_TINT(vehicle, p.Int("WindowTint"));
  VEHICLE::SET_VEHICLE_TYRES_CAN_BURST(vehicle, !p.Bool("BulletProofTyres"));
  VEHICLE::SET_VEHICLE_DIRT_LEVEL(vehicle, p.Float("DirtLevel"));
  if (p.Child("EngineOn"))
    VEHICLE::SET_VEHICLE_ENGINE_ON(vehicle, p.Bool("EngineOn"), true, false);
  if (p.Child("EngineHealth"))
    VEHICLE::SET_VEHICLE_ENGINE_HEALTH(vehicle, p.Float("EngineHealth"));
  VEHICLE::SET_VEHICLE_SIREN(vehicle, p.Bool("SirenActive"));
  if (p.Child("LightsOn"))
    VEHICLE::SET_VEHICLE_LIGHTS(vehicle, p.Bool("LightsOn") ? 3 : 4);
  AUDIO::SET_VEHICLE_RADIO_LOUD(vehicle, p.Bool("IsRadioLoud"));
  VEHICLE::SET_VEHICLE_DOORS_LOCKED(vehicle, p.Int("LockStatus"));
  VEHICLE::SET_VEHICLE_ENVEFF_SCALE(vehicle, p.Float("PaintFade"));
  if (p.Child("HeadlightIntensity"))
    VEHICLE::SET_VEHICLE_LIGHT_MULTIPLIER(vehicle,
                                          p.Float("HeadlightIntensity", 1.0f));
  const int roof = p.Int("RoofState", -1);
  if (roof == 1 || roof == 2)
    VEHICLE::RAISE_CONVERTIBLE_ROOF(vehicle, roof == 2);
  else if (roof == 0 || roof == 3)
    VEHICLE::LOWER_CONVERTIBLE_ROOF(vehicle, roof == 0);
  if (const auto *c = p.Child("Colours")) {
    VEHICLE::SET_VEHICLE_MOD_COLOR_1(vehicle, c->Int("Mod1_a"),
                                     c->Int("Mod1_b"), c->Int("Mod1_c"));
    VEHICLE::SET_VEHICLE_MOD_COLOR_2(vehicle, c->Int("Mod2_a"),
                                     c->Int("Mod2_b"));
    VEHICLE::SET_VEHICLE_COLOURS(vehicle, c->Int("Primary"),
                                 c->Int("Secondary"));
    VEHICLE::SET_VEHICLE_EXTRA_COLOURS(vehicle, c->Int("Pearl"), c->Int("Rim"));
    if (c->Bool("IsPrimaryColourCustom"))
      VEHICLE::SET_VEHICLE_CUSTOM_PRIMARY_COLOUR(
          vehicle, c->Int("Cust1_R"), c->Int("Cust1_G"), c->Int("Cust1_B"));
    if (c->Bool("IsSecondaryColourCustom"))
      VEHICLE::SET_VEHICLE_CUSTOM_SECONDARY_COLOUR(
          vehicle, c->Int("Cust2_R"), c->Int("Cust2_G"), c->Int("Cust2_B"));
    VEHICLE::SET_VEHICLE_TYRE_SMOKE_COLOR(vehicle, c->Int("tyreSmoke_R"),
                                          c->Int("tyreSmoke_G"),
                                          c->Int("tyreSmoke_B"));
    if (c->Child("LrXenonHeadlights"))
      VEHICLE::SET_VEHICLE_XENON_LIGHT_COLOR_INDEX(vehicle,
                                                   c->Int("LrXenonHeadlights"));
    if (c->Child("LrInterior"))
      VEHICLE::SET_VEHICLE_EXTRA_COLOUR_5(vehicle, c->Int("LrInterior"));
    if (c->Child("LrDashboard"))
      VEHICLE::SET_VEHICLE_EXTRA_COLOUR_6(vehicle, c->Int("LrDashboard"));
  }
  if (const auto *neon = p.Child("Neons")) {
    VEHICLE::SET_VEHICLE_NEON_ENABLED(vehicle, 0, neon->Bool("Left"));
    VEHICLE::SET_VEHICLE_NEON_ENABLED(vehicle, 1, neon->Bool("Right"));
    VEHICLE::SET_VEHICLE_NEON_ENABLED(vehicle, 2, neon->Bool("Front"));
    VEHICLE::SET_VEHICLE_NEON_ENABLED(vehicle, 3, neon->Bool("Back"));
    VEHICLE::SET_VEHICLE_NEON_COLOUR(vehicle, neon->Int("R"), neon->Int("G"),
                                     neon->Int("B"));
  }
  if (const auto *extras = p.Child("ModExtras")) {
    int fallback = 0;
    for (const auto &item : extras->Children)
      VEHICLE::SET_VEHICLE_EXTRA(vehicle, NodeIndex(item, fallback++),
                                 !TextBool(item.Text));
  }
  if (const auto *mods = p.Child("Mods")) {
    int fallback = 0;
    for (const auto &item : mods->Children) {
      const int index = NodeIndex(item, fallback++);
      if (item.Text.find(',') == std::string::npos) {
        VEHICLE::TOGGLE_VEHICLE_MOD(vehicle, index, TextBool(item.Text));
      } else {
        const auto v = CsvInts(item.Text);
        if (!v.empty())
          VEHICLE::SET_VEHICLE_MOD(vehicle, index, v[0],
                                   v.size() > 1 && v[1] != 0);
      }
    }
  }
  const std::pair<const char *, int> doors[] = {
      {"FrontLeftDoor", 0}, {"FrontRightDoor", 1}, {"BackLeftDoor", 2},
      {"BackRightDoor", 3}, {"Hood", 4},           {"Trunk", 5},
      {"Trunk2", 6}};
  if (const auto *open = p.Child("DoorsOpen"))
    for (const auto &[name, index] : doors)
      if (open->Bool(name))
        VEHICLE::SET_VEHICLE_DOOR_OPEN(vehicle, index, false, true);
  if (const auto *broken = p.Child("DoorsBroken"))
    for (const auto &[name, index] : doors)
      if (broken->Bool(name))
        VEHICLE::SET_VEHICLE_DOOR_BROKEN(vehicle, index, true);
  const std::pair<const char *, int> tyres[] = {
      {"FrontLeft", 0}, {"FrontRight", 1}, {"_2", 2},
      {"_3", 3},        {"BackLeft", 4},   {"BackRight", 5},
      {"_6", 6},        {"_7", 7},         {"_8", 8}};
  if (const auto *burst = p.Child("TyresBursted"))
    for (const auto &[name, index] : tyres)
      if (burst->Bool(name))
        VEHICLE::SET_VEHICLE_TYRE_BURST(vehicle, index, true, 1000.0f);
  const std::string engineSound = p.Value("EngineSoundName");
  if (!engineSound.empty())
    AUDIO::FORCE_USE_AUDIO_GAME_OBJECT(vehicle, engineSound.c_str());
  if (p.Child("RpmMultiplier"))
    VEHICLE::MODIFY_VEHICLE_TOP_SPEED(vehicle, p.Float("RpmMultiplier"));
  if (p.Child("TorqueMultiplier"))
    VEHICLE::SET_VEHICLE_CHEAT_POWER_INCREASE(vehicle,
                                               p.Float("TorqueMultiplier"));
  if (p.Child("MaxSpeed"))
    ENTITY::SET_ENTITY_MAX_SPEED(vehicle, p.Float("MaxSpeed"));
  if (p.Bool("IsRadioLoud"))
    AUDIO::SET_VEHICLE_RADIO_ENABLED(vehicle, true);
}

Entity Spawn(const Placement &p, Session &session, std::size_t total) {
  const std::string model = Utils::HexHash(p.Model);
  const std::string hashName = p.Xml.Value("HashName");
  LOG_DEBUG(
      "[Scene] Session {} placement {}/{} begin type={}({}) model={} "
      "hash_name='{}' initial_handle={} pos=({:.3f},{:.3f},{:.3f}) "
      "rot=({:.3f},{:.3f},{:.3f}) dynamic={} frozen={}",
      session.Id, p.Index, total, PlacementTypeName(p.Type), p.Type, model,
      hashName, p.InitialHandle, p.Position.x, p.Position.y, p.Position.z,
      p.Rotation.x, p.Rotation.y, p.Rotation.z, p.Dynamic, p.Frozen);

  ULONGLONG modelLoadMs = 0;
  const ModelLoadResult load = LoadModel(p.Model, modelLoadMs);
  if (load != ModelLoadResult::Loaded) {
    const char *category = load == ModelLoadResult::NotInCdImage
                               ? "model_not_in_cdimage"
                               : "model_timeout";
    RecordFailure(
        session, category,
        std::format("placement={}/{} type={}({}) model={} hash_name='{}' "
                    "initial_handle={} model_load_ms={} pos=({:.3f},{:.3f},{:.3f})",
                    p.Index, total, PlacementTypeName(p.Type), p.Type, model,
                    hashName, p.InitialHandle, modelLoadMs, p.Position.x,
                    p.Position.y, p.Position.z));
    return 0;
  }
  STREAMING::REQUEST_COLLISION_FOR_MODEL(p.Model);
  Entity entity = 0;
  bool creationAttempted = false;
  if (p.Type == 1 && STREAMING::IS_MODEL_A_PED(p.Model)) {
    creationAttempted = true;
    entity =
        PED::CREATE_PED(26, p.Model, p.Position, p.Rotation.z, true, true);
  } else if (p.Type == 2 && STREAMING::IS_MODEL_A_VEHICLE(p.Model)) {
    creationAttempted = true;
    entity = VEHICLE::CREATE_VEHICLE(p.Model, p.Position, p.Rotation.z, true,
                                     true, false);
  } else if (p.Type == 3) {
    creationAttempted = true;
    entity = OBJECT::CREATE_OBJECT_NO_OFFSET(p.Model, p.Position, true, true,
                                             p.Dynamic, false);
  } else {
    RecordFailure(
        session, "type_mismatch",
        std::format("placement={}/{} type={}({}) model={} hash_name='{}' "
                    "initial_handle={} model_load_ms={}",
                    p.Index, total, PlacementTypeName(p.Type), p.Type, model,
                    hashName, p.InitialHandle, modelLoadMs));
  }
  if (!ENTITY::DOES_ENTITY_EXIST(entity)) {
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(p.Model);
    if (creationAttempted)
      RecordFailure(
          session, "create_returned_zero",
          std::format("placement={}/{} type={}({}) model={} hash_name='{}' "
                      "initial_handle={} model_load_ms={} "
                      "pos=({:.3f},{:.3f},{:.3f})",
                      p.Index, total, PlacementTypeName(p.Type), p.Type, model,
                      hashName, p.InitialHandle, modelLoadMs, p.Position.x,
                      p.Position.y, p.Position.z));
    return 0;
  }
  ENTITY::SET_ENTITY_AS_MISSION_ENTITY(entity, true, true);
  ENTITY::SET_ENTITY_DYNAMIC(entity, false);
  ENTITY::FREEZE_ENTITY_POSITION(entity, true);
  ENTITY::SET_ENTITY_COORDS_NO_OFFSET(entity, p.Position, false, false, false);
  ENTITY::SET_ENTITY_ROTATION(entity, p.Rotation.x, p.Rotation.y, p.Rotation.z,
                               2, true);
  ENTITY::SET_ENTITY_HAS_GRAVITY(entity, p.Xml.Bool("HasGravity", true));
  ENTITY::SET_ENTITY_VISIBLE(entity, p.Xml.Bool("IsVisible", true), false);
  ENTITY::SET_ENTITY_INVINCIBLE(entity, p.Xml.Bool("IsInvincible"));
  ENTITY::SET_ENTITY_PROOFS(
      entity, p.Xml.Bool("IsBulletProof"), p.Xml.Bool("IsFireProof"),
      p.Xml.Bool("IsExplosionProof"), p.Xml.Bool("IsCollisionProof"),
      p.Xml.Bool("IsMeleeProof"), false, false, false);
  ENTITY::SET_ENTITY_ONLY_DAMAGED_BY_PLAYER(
      entity, p.Xml.Bool("IsOnlyDamagedByPlayer"));
  ENTITY::SET_ENTITY_COLLISION(entity, !p.Xml.Bool("IsCollisionProof"), true);
  ENTITY::SET_ENTITY_ALPHA(
      entity, std::clamp(p.Xml.Int("OpacityLevel", 255), 0, 255), false);
  ENTITY::SET_ENTITY_LOD_DIST(entity, p.Xml.Int("LodDistance", 16960));
  if (p.Xml.Child("MaxHealth"))
    ENTITY::SET_ENTITY_MAX_HEALTH(entity, p.Xml.Int("MaxHealth"));
  if (p.Xml.Child("Health"))
    ENTITY::SET_ENTITY_HEALTH(entity, p.Xml.Int("Health"), 0, 0);
  if (p.Xml.Bool("IsOnFire"))
    FIRE::START_ENTITY_FIRE(entity);
  if (p.Type == 1) {
    if (const auto *props = p.Xml.Child("PedProperties"))
      ApplyPedProperties(static_cast<Ped>(entity), *props);
  } else if (p.Type == 2) {
    if (const auto *props = p.Xml.Child("VehicleProperties"))
      ApplyVehicleProperties(static_cast<Vehicle>(entity), *props);
  } else if (const auto *props = p.Xml.Child("ObjectProperties")) {
    const int tint = props->Int("TextureVariation", -1);
    if (tint >= 0)
      OBJECT::SET_OBJECT_TINT_INDEX(entity, tint);
  }
  ENTITY::SET_ENTITY_LIGHTS(entity, false);
  LOG_DEBUG("[Scene] Session {} placement {}/{} created entity={} type={} "
            "model={} model_load_ms={}",
            session.Id, p.Index, total, entity, PlacementTypeName(p.Type),
            model, modelLoadMs);
  return entity;
}

void WaitForModelCollisions(
    const std::vector<std::pair<Placement, Entity>> &spawned,
    const Session &session) {
  std::unordered_set<Hash> models;
  for (const auto &[placement, entity] : spawned) {
    if (ENTITY::DOES_ENTITY_EXIST(entity)) {
      models.insert(placement.Model);
      STREAMING::REQUEST_COLLISION_FOR_MODEL(placement.Model);
    }
  }
  const ULONGLONG started = GetTickCount64();
  std::vector<Hash> pending;
  do {
    pending.clear();
    for (Hash model : models)
      if (!STREAMING::HAS_COLLISION_FOR_MODEL_LOADED(model))
        pending.push_back(model);
    if (pending.empty())
      break;
    WAIT(0);
  } while (GetTickCount64() - started <= kAssetTimeoutMs);

  const ULONGLONG elapsed = GetTickCount64() - started;
  LOG_INFO("[Scene] Session {} model collision preparation total={} loaded={} "
           "pending={} elapsed_ms={}",
           session.Id, models.size(), models.size() - pending.size(),
           pending.size(), elapsed);
  for (Hash model : pending)
    LOG_WARNING("[Scene] Session {} model collision timeout model={}",
                session.Id, Utils::HexHash(model));
}

void RestorePlacementState(
    const std::vector<std::pair<Placement, Entity>> &spawned,
    const Session &session) {
  std::unordered_set<Hash> models;
  std::size_t restored = 0;
  for (const auto &[placement, entity] : spawned) {
    models.insert(placement.Model);
    if (!ENTITY::DOES_ENTITY_EXIST(entity))
      continue;
    ENTITY::SET_ENTITY_DYNAMIC(entity, placement.Dynamic);
    ENTITY::FREEZE_ENTITY_POSITION(entity, placement.Frozen);
    ++restored;
  }
  for (Hash model : models)
    STREAMING::SET_MODEL_AS_NO_LONGER_NEEDED(model);
  LOG_INFO("[Scene] Session {} restored final physics state entities={} "
           "released_models={}",
           session.Id, restored, models.size());
}

void ApplyWorld(const SceneXmlNode &root) {
  if (const auto *remove = root.Child("IPLsToRemove"))
    for (const auto &item : remove->Children)
      STREAMING::REMOVE_IPL(Utils::Trim(item.Text).c_str());
  if (const auto *load = root.Child("IPLsToLoad")) {
    const bool loadMp = TextBool(load->Attribute("load_mp_maps"));
    const bool loadSp = TextBool(load->Attribute("load_sp_maps"));
    if (loadMp || loadSp)
      MISC::SET_INSTANCE_PRIORITY_MODE(true);
    if (loadMp)
      DLC::ON_ENTER_MP();
    if (loadSp)
      DLC::ON_ENTER_SP();
    for (const auto &item : load->Children)
      STREAMING::REQUEST_IPL(Utils::Trim(item.Text).c_str());
    if (loadMp || loadSp)
      MISC::SET_INSTANCE_PRIORITY_MODE(false);
  }
  if (const auto *interiors = root.Child("InteriorsToEnable"))
    for (const auto &item : interiors->Children) {
      int id = item.Int("id", -1);
      if (id < 0)
        id = INTERIOR::GET_INTERIOR_AT_COORDS(Vector(&item));
      if (!INTERIOR::IS_VALID_INTERIOR(id))
        continue;
      INTERIOR::PIN_INTERIOR_IN_MEMORY(id);
      INTERIOR::DISABLE_INTERIOR(id,
                                 item.Attribute("enable", "true") == "false");
      for (const SceneXmlNode *prop : item.ChildrenNamed("InteriorProp")) {
        const std::string name = prop->Attribute("name");
        if (prop->Attribute("enable", "true") == "false")
          INTERIOR::DEACTIVATE_INTERIOR_ENTITY_SET(id, name.c_str());
        else
          INTERIOR::ACTIVATE_INTERIOR_ENTITY_SET(id, name.c_str());
      }
      INTERIOR::REFRESH_INTERIOR(id);
    }
  if (const auto *interiors = root.Child("InteriorsToCap"))
    for (const auto &item : interiors->Children) {
      int id = item.Int("id", -1);
      if (id < 0)
        id = INTERIOR::GET_INTERIOR_AT_COORDS(Vector(&item));
      if (INTERIOR::IS_VALID_INTERIOR(id))
        INTERIOR::CAP_INTERIOR(id, item.Attribute("cap", "true") != "false");
    }
  const std::string weather = root.Value("WeatherToSet");
  if (!weather.empty())
    MISC::SET_WEATHER_TYPE_OVERTIME_PERSIST(weather.c_str(), 3.0f);
  if (const auto *timecycle = root.Child("TimecycleModifier")) {
    const std::string value = Utils::Trim(timecycle->Text);
    if (!value.empty()) {
      GRAPHICS::SET_TIMECYCLE_MODIFIER(value.c_str());
      GRAPHICS::SET_TIMECYCLE_MODIFIER_STRENGTH(
          timecycle->Float("strength", 1.0f));
    }
  }
}

void ClearWorld(const SceneXmlNode &root) {
  const float radius = root.Float("ClearWorld");
  const auto *ref = root.Child("ReferenceCoords");
  if (radius <= 0.0f || !ref)
    return;
  const Vector3 pos = Vector(ref);
  MISC::CLEAR_AREA_OF_OBJECTS(pos, radius, 0);
  MISC::CLEAR_AREA_OF_PEDS(pos, radius, 1);
  MISC::CLEAR_AREA_OF_VEHICLES(pos, radius, false, false, false, false, false,
                               false, 0);
  MISC::CLEAR_AREA_OF_PROJECTILES(pos, radius, 0);
}

Marker ParseMarker(const SceneXmlNode &n) {
  Marker m;
  m.Name = n.Value("Name");
  m.InitialHandle = n.Int("InitialHandle");
  m.Type = n.Int("Type");
  m.Scale = n.Float("Scale", 0.9f);
  m.ShowName = n.Bool("ShowName");
  m.Rotate = n.Bool("RotateContinuously", true);
  m.AllowVehicles = n.Bool("AllowVehicles");
  m.DestinationHeading = n.Float("DestinationHeading");
  if (const auto *c = n.Child("Colour")) {
    m.R = c->Int("R", 102);
    m.G = c->Int("G");
    m.B = c->Int("B", 204);
    m.A = c->Int("A", 190);
  }
  if (const auto *pos = n.Child("Position")) {
    m.Position = Vector(pos, "Position");
    m.Rotation = Vector(pos, "Rotation");
    if (const auto *a = pos->Child("Attachment")) {
      m.AttachedTo = a->Int("InitHandle");
      m.AttachOffset = Vector(a, "Offset");
    }
  }
  if (const auto *dest = n.Child("Destination")) {
    m.LinkToHandle = dest->Int("LinkInitHandle");
    m.Destination = Vector(dest, "Position");
    if (const auto *a = dest->Child("Attachment")) {
      m.DestinationAttachedTo = a->Int("InitHandle");
      m.DestinationOffset = Vector(a, "Offset");
    }
  }
  return m;
}

Vector3 MarkerPosition(const Marker &marker, const Session &session) {
  if (const Entity entity = ResolveTarget(session, marker.AttachedTo);
      ENTITY::DOES_ENTITY_EXIST(entity))
    return ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(entity,
                                                          marker.AttachOffset);
  return marker.Position;
}

void DrawMarkerName(const Marker &marker, const Vector3 &position) {
  if (!marker.ShowName || marker.Name.empty())
    return;
  GRAPHICS::SET_DRAW_ORIGIN(position, false);
  HUD::SET_TEXT_FONT(0);
  HUD::SET_TEXT_SCALE(0.0f, 0.28f);
  HUD::SET_TEXT_CENTRE(true);
  HUD::SET_TEXT_COLOUR(255, 255, 255, 220);
  HUD::SET_TEXT_OUTLINE();
  HUD::BEGIN_TEXT_COMMAND_DISPLAY_TEXT("STRING");
  HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(marker.Name.c_str());
  HUD::END_TEXT_COMMAND_DISPLAY_TEXT({0.0f, 0.0f}, 0);
  GRAPHICS::CLEAR_DRAW_ORIGIN();
}

void StartPlacementFx(const Placement &p, Entity entity, Session &session) {
  const std::string asset = p.Xml.Value("PtfxLopAsset"),
                    effect = p.Xml.Value("PtfxLopEffect");
  if (asset.empty() || effect.empty())
    return;
  STREAMING::REQUEST_NAMED_PTFX_ASSET(asset.c_str());
  const ULONGLONG start = GetTickCount64();
  while (!STREAMING::HAS_NAMED_PTFX_ASSET_LOADED(asset.c_str()) &&
         GetTickCount64() - start < kAssetTimeoutMs)
    WAIT(0);
  if (!STREAMING::HAS_NAMED_PTFX_ASSET_LOADED(asset.c_str())) {
    RecordFailure(session, "ptfx_timeout",
                  std::format("placement={} entity={} asset='{}' effect='{}'",
                              p.Index, entity, asset, effect));
    return;
  }
  GRAPHICS::USE_PARTICLE_FX_ASSET(asset.c_str());
  const int handle = GRAPHICS::START_PARTICLE_FX_LOOPED_ON_ENTITY(
      effect.c_str(), entity, {}, {}, 1.0f, false, false, false);
  if (handle)
    session.ParticleFx.push_back(handle);
}

void StartAudio(const SceneXmlNode &root, Session &session) {
  const auto *audio = root.Child("AudioFile");
  if (!audio)
    return;
  const std::string utf8Path = Utils::Trim(audio->Text);
  LOG_DEBUG("[Scene] Session {} audio node path='{}'", session.Id, utf8Path);
  std::filesystem::path path = std::filesystem::path(std::u8string(
      reinterpret_cast<const char8_t *>(utf8Path.data()), utf8Path.size()));
  if (path.is_relative()) {
    const std::filesystem::path relative = path;
    const std::filesystem::path candidates[] = {
        session.Source.parent_path() / relative,
        Paths::DataDirectory() / L"Audio" / relative,
        Paths::ModuleDirectory() / L"menyooStuff" / L"Audio" / relative,
    };
    for (const auto &candidate : candidates) {
      if (std::filesystem::is_regular_file(candidate)) {
        path = candidate;
        break;
      }
    }
  }
  if (!std::filesystem::is_regular_file(path)) {
    RecordFailure(session,
                  utf8Path.empty() ? "audio_empty_path" : "audio_not_found",
                  std::format("path='{}' resolved='{}'", utf8Path,
                              path.string()));
    return;
  }
  if (!gActiveAudioAlias.empty()) {
    const std::wstring oldAlias(gActiveAudioAlias.begin(),
                                gActiveAudioAlias.end());
    mciSendStringW((L"stop " + oldAlias).c_str(), nullptr, 0, nullptr);
    mciSendStringW((L"close " + oldAlias).c_str(), nullptr, 0, nullptr);
  }
  session.AudioAlias = "CatalogScene" + std::to_string(session.Id);
  gActiveAudioAlias = session.AudioAlias;
  const std::wstring alias(session.AudioAlias.begin(),
                           session.AudioAlias.end());
  const std::wstring open =
      L"open \"" + path.wstring() + L"\" type MPEGVideo alias " + alias;
  if (mciSendStringW(open.c_str(), nullptr, 0, nullptr) != 0) {
    RecordFailure(session, "audio_open_failed",
                  std::format("path='{}' alias='{}'", path.string(),
                              session.AudioAlias));
    return;
  }
  mciSendStringW((L"play " + alias + L" from 0").c_str(), nullptr, 0, nullptr);
  const std::string volumeText = audio->Attribute("volume", "400");
  const std::wstring volume(volumeText.begin(), volumeText.end());
  mciSendStringW((L"setaudio " + alias + L" volume to " + volume).c_str(),
                 nullptr, 0, nullptr);
}

void DestroySession(Session &session) {
  for (auto &task : session.Tasks)
    task.Stop();
  for (Blip &blip : session.Blips)
    if (HUD::DOES_BLIP_EXIST(blip))
      HUD::REMOVE_BLIP(&blip);
  for (int fx : session.ParticleFx)
    GRAPHICS::REMOVE_PARTICLE_FX(fx, false);
  if (!session.AudioAlias.empty()) {
    const std::wstring alias(session.AudioAlias.begin(),
                             session.AudioAlias.end());
    mciSendStringW((L"stop " + alias).c_str(), nullptr, 0, nullptr);
    mciSendStringW((L"close " + alias).c_str(), nullptr, 0, nullptr);
    if (gActiveAudioAlias == session.AudioAlias)
      gActiveAudioAlias.clear();
  }
  for (auto it = session.Entities.rbegin(); it != session.Entities.rend();
       ++it) {
    Entity e = *it;
    if (ENTITY::DOES_ENTITY_EXIST(e)) {
      ENTITY::SET_ENTITY_AS_MISSION_ENTITY(e, true, true);
      ENTITY::DELETE_ENTITY(&e);
    }
  }
}
} // namespace

SceneLoader::PreflightResult
SceneLoader::Preflight(const std::filesystem::path &path) {
  PreflightResult result;
  LOG_DEBUG("[Scene] Preflight begin path='{}'", path.string());
  SceneXmlNode root;
  if (!LoadSceneXml(path, root, result.Error) ||
      root.Name != "SpoonerPlacements") {
    if (result.Error.empty())
      result.Error = "root must be SpoonerPlacements";
    LOG_WARNING("[Scene] Preflight failed path='{}' error='{}' root='{}'",
                path.string(), result.Error, root.Name);
    return result;
  }
  result.Valid = true;
  result.ClearDatabase = root.Bool("ClearDatabase");
  result.ClearMarkers = root.Bool("ClearMarkers");
  result.ClearWorldRadius = root.Float("ClearWorld");
  result.Destructive = result.ClearDatabase || result.ClearMarkers ||
                       result.ClearWorldRadius > 0.0f;
  LOG_DEBUG("[Scene] Preflight success path='{}' destructive={} "
            "clear_database={} clear_markers={} clear_world_radius={:.3f}",
            path.string(), result.Destructive, result.ClearDatabase,
            result.ClearMarkers, result.ClearWorldRadius);
  return result;
}

bool SceneLoader::Load(const std::filesystem::path &path,
                       const std::string &displayName, bool allowDestructive) {
  LOG_INFO("[Scene] Load requested name='{}' path='{}' allow_destructive={}",
           displayName, path.string(), allowDestructive);
  SceneXmlNode root;
  std::string error;
  if (!LoadSceneXml(path, root, error) || root.Name != "SpoonerPlacements") {
    Utils::ShowSubtitle(
        gLanguage.Format("subtitle.scene_invalid", {{"name", displayName}}));
    LOG_WARNING("[Scene] Invalid '{}': {}", path.string(), error);
    return false;
  }
  const PreflightResult check = Preflight(path);
  if (check.Destructive && !allowDestructive) {
    LOG_WARNING("[Scene] Destructive directives ignored for direct load: {}",
                path.string());
  }
  auto session = std::make_unique<Session>();
  session->Id = gNextSessionId++;
  session->Name = displayName;
  session->Source = path;
  const auto placements = root.ChildrenNamed("Placement");
  const std::size_t totalPlacements = placements.size();
  std::size_t pedCount = 0, vehicleCount = 0, propCount = 0,
              invalidTypeCount = 0, attachmentCount = 0, ptfxCount = 0,
              taskSequenceCount = 0;
  for (const SceneXmlNode *node : placements) {
    switch (node->Int("Type")) {
    case 1:
      ++pedCount;
      break;
    case 2:
      ++vehicleCount;
      break;
    case 3:
      ++propCount;
      break;
    default:
      ++invalidTypeCount;
      break;
    }
    if (const auto *attachment = node->Child("Attachment");
        attachment && TextBool(attachment->Attribute("isAttached")))
      ++attachmentCount;
    if (!node->Value("PtfxLopAsset").empty() ||
        !node->Value("PtfxLopEffect").empty())
      ++ptfxCount;
    if (PlacementTaskSequence(*node))
      ++taskSequenceCount;
  }
  LOG_INFO(
      "[Scene] Session {} parsed name='{}' placements={} peds={} vehicles={} "
      "props={} invalid_types={} attachments={} ptfx={} task_sequences={} "
      "audio_node={} markers={} source='{}'",
      session->Id, displayName, totalPlacements, pedCount, vehicleCount,
      propCount, invalidTypeCount, attachmentCount, ptfxCount,
      taskSequenceCount, root.Child("AudioFile") != nullptr,
      root.ChildrenNamed("Marker").size(), path.string());
  ApplyWorld(root);
  if (allowDestructive) {
    ClearWorld(root);
    if (root.Bool("ClearDatabase"))
      UnloadAll();
    if (root.Bool("ClearMarkers"))
      for (auto &s : gSessions)
        s->Markers.clear();
  }
  const Ped player = PLAYER::PLAYER_PED_ID();
  const Vector3 oldPos = ENTITY::GET_ENTITY_COORDS(player, true);
  CAM::DO_SCREEN_FADE_OUT(300);
  if (const auto *loading = root.Child("ImgLoadingCoords")) {
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(player, Vector(loading), false, false,
                                        false);
    WAIT(1400);
  }
  std::vector<std::pair<Placement, Entity>> spawned;
  std::size_t placementIndex = 0;
  for (const SceneXmlNode *node : placements) {
    ++placementIndex;
    Placement p;
    p.Index = placementIndex;
    p.Xml = *node;
    p.Model = SceneXmlHash(node->Value("ModelHash"));
    p.Type = node->Int("Type");
    p.Dynamic = node->Bool("Dynamic");
    p.Frozen = node->Bool("FrozenPos", !p.Dynamic);
    p.InitialHandle = node->Int("InitialHandle");
    p.Position = Vector(node->Child("PositionRotation"));
    if (const auto *pr = node->Child("PositionRotation"))
      p.Rotation = {pr->Float("Pitch"), pr->Float("Roll"), pr->Float("Yaw")};
    if (const auto *a = node->Child("Attachment")) {
      p.Attach.Enabled = TextBool(a->Attribute("isAttached"));
      p.Attach.Target = TargetId(a->Value("AttachedTo"));
      p.Attach.Bone = a->Int("BoneIndex");
      p.Attach.Offset = Vector(a);
      p.Attach.Rotation = {a->Float("Pitch"), a->Float("Roll"),
                           a->Float("Yaw")};
    }
    if (!p.Model || p.Type < 1 || p.Type > 3) {
      RecordFailure(
          *session, "invalid_placement",
          std::format("placement={}/{} type={} model={} hash_name='{}' "
                      "initial_handle={}",
                      p.Index, totalPlacements, p.Type,
                      Utils::HexHash(p.Model), node->Value("HashName"),
                      p.InitialHandle));
      continue;
    }
    Entity entity = Spawn(p, *session, totalPlacements);
    if (!entity)
      continue;
    session->Entities.push_back(entity);
    if (p.InitialHandle)
      session->Handles[p.InitialHandle] = entity;
    if (p.Type == 2)
      session->VehicleAudit.push_back(
          {p.Index, entity, p.Model, p.Xml.Value("HashName"), p.Position,
           p.Frozen, p.Xml.Bool("IsVisible", true),
           !p.Xml.Bool("IsCollisionProof")});
    spawned.push_back({std::move(p), entity});
    if (placementIndex % kPlacementYieldBatchSize == 0)
      WAIT(0);
  }
  WaitForModelCollisions(spawned, *session);
  for (auto &[p, entity] : spawned) {
    if (!ENTITY::DOES_ENTITY_EXIST(entity))
      continue;
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(entity, p.Position, false, false,
                                        false);
    ENTITY::SET_ENTITY_ROTATION(entity, p.Rotation.x, p.Rotation.y,
                                p.Rotation.z, 2, true);
  }
  for (auto &[p, entity] : spawned) {
    if (p.Attach.Enabled) {
      Entity parent = ResolveTarget(*session, p.Attach.Target);
      if (ENTITY::DOES_ENTITY_EXIST(parent))
        ENTITY::ATTACH_ENTITY_TO_ENTITY(
            entity, parent, p.Attach.Bone, p.Attach.Offset, p.Attach.Rotation,
            false, false, !p.Xml.Bool("IsCollisionProof"), false, 2, true,
            false);
      else
        RecordFailure(
            *session, "attachment_target_missing",
            std::format("placement={}/{} entity={} model={} target={}",
                        p.Index, totalPlacements, entity,
                        Utils::HexHash(p.Model), p.Attach.Target));
    }
    StartPlacementFx(p, entity, *session);
    if (root.Bool("StartTaskSequencesOnLoad", true))
      if (const auto *tasks = PlacementTaskSequence(p.Xml))
        session->Tasks.emplace_back(entity, *tasks);
  }
  RestorePlacementState(spawned, *session);
  for (const SceneXmlNode *marker : root.ChildrenNamed("Marker"))
    session->Markers.push_back(ParseMarker(*marker));
  if (const auto *ref = root.Child("ReferenceCoords")) {
    Blip b = HUD::ADD_BLIP_FOR_COORD(Vector(ref));
    HUD::SET_BLIP_SPRITE(b, 280);
    HUD::BEGIN_TEXT_COMMAND_SET_BLIP_NAME("STRING");
    HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(displayName.c_str());
    HUD::END_TEXT_COMMAND_SET_BLIP_NAME(b);
    session->Blips.push_back(b);
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(player, Vector(ref), false, false,
                                        false);
  } else if (root.Child("ImgLoadingCoords"))
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(player, oldPos, false, false, false);
  WAIT(200);
  CAM::DO_SCREEN_FADE_IN(300);
  if (!root.Value("Note").empty())
    Utils::ShowSubtitle(root.Value("Note"), 6000);
  StartAudio(root, *session);
  session->VehicleAuditStartedAt = GetTickCount64();
  AuditVehicles(*session, "post_load", 0);
  const size_t loaded = session->Entities.size(),
               failed = session->Failures.size();
  for (const auto &[category, count] : session->FailureCounts)
    LOG_INFO("[Scene] Session {} failure summary category={} count={}",
             session->Id, category, count);
  LOG_INFO("[Scene] Loaded '{}' session {}: {} entities, {} failures",
           displayName, session->Id, loaded, failed);
  gSessions.push_back(std::move(session));
  Utils::ShowSubtitle(gLanguage.Format("subtitle.scene_loaded",
                                       {{"name", displayName},
                                        {"loaded", std::to_string(loaded)},
                                        {"failed", std::to_string(failed)}}),
                      4000);
  return true;
}

void SceneLoader::Tick() {
  for (auto &session : gSessions) {
    if (session->VehicleAuditStartedAt != 0) {
      const ULONGLONG elapsed =
          GetTickCount64() - session->VehicleAuditStartedAt;
      if (!session->VehicleAudit1sComplete && elapsed >= 1000) {
        AuditVehicles(*session, "after_1s", elapsed);
        session->VehicleAudit1sComplete = true;
      }
      if (!session->VehicleAudit5sComplete && elapsed >= 5000) {
        AuditVehicles(*session, "after_5s", elapsed);
        session->VehicleAudit5sComplete = true;
      }
    }
    SceneTaskContext context{&session->Handles, &session->Blips,
                             &session->ParticleFx, &session->Failures};
    for (auto &task : session->Tasks)
      task.Tick(context);
    for (auto &marker : session->Markers) {
      const Vector3 pos = MarkerPosition(marker, *session);
      Ped ped = PLAYER::PLAYER_PED_ID();
      Entity mover = ped;
      if (marker.AllowVehicles && PED::IS_PED_IN_ANY_VEHICLE(ped, false))
        mover = PED::GET_VEHICLE_PED_IS_IN(ped, false);
      const Vector3 p = ENTITY::GET_ENTITY_COORDS(mover, true);
      const float dx = p.x - pos.x, dy = p.y - pos.y, dz = p.z - pos.z;
      const float distanceSquared = dx * dx + dy * dy + dz * dz;
      if (distanceSquared > 80.0f * 80.0f) {
        marker.WasInside = false;
        continue;
      }
      float rotation =
          marker.Rotation.z +
          (marker.Rotate ? static_cast<float>(GetTickCount64() % 3600) / 10.0f
                         : 0.0f);
      GRAPHICS::DRAW_MARKER(marker.Type, pos, {},
                            {marker.Rotation.x, marker.Rotation.y, rotation},
                            {marker.Scale, marker.Scale, marker.Scale},
                            marker.R, marker.G, marker.B, marker.A, false, true,
                            2, false, nullptr, nullptr, false);
      DrawMarkerName(marker, pos);
      const bool inside = distanceSquared <= marker.Scale * marker.Scale;
      if (inside && !marker.WasInside &&
          GetTickCount64() >= session->MarkerCooldownUntil &&
          (marker.Destination.x != 0 || marker.Destination.y != 0 ||
           marker.Destination.z != 0 || marker.LinkToHandle != 0)) {
        Vector3 dest = marker.Destination;
        if (marker.LinkToHandle != 0) {
          const auto linked =
              std::find_if(session->Markers.begin(), session->Markers.end(),
                           [&marker](const Marker &m) {
                             return m.InitialHandle == marker.LinkToHandle;
                           });
          if (linked != session->Markers.end())
            dest = MarkerPosition(*linked, *session);
        } else if (Entity e =
                       ResolveTarget(*session, marker.DestinationAttachedTo);
                   ENTITY::DOES_ENTITY_EXIST(e))
          dest = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(
              e, marker.DestinationOffset);
        ENTITY::SET_ENTITY_COORDS_NO_OFFSET(mover, dest, false, false, false);
        ENTITY::SET_ENTITY_HEADING(mover, marker.DestinationHeading);
        session->MarkerCooldownUntil = GetTickCount64() + 1000;
      }
      marker.WasInside = inside;
    }
  }
}

void SceneLoader::Unload(std::uint64_t id) {
  const auto it = std::find_if(gSessions.begin(), gSessions.end(),
                               [id](const auto &s) { return s->Id == id; });
  if (it == gSessions.end())
    return;
  const size_t count = (*it)->Entities.size();
  DestroySession(**it);
  gSessions.erase(it);
  Utils::ShowSubtitle(gLanguage.Format("subtitle.scenes_unloaded",
                                       {{"count", std::to_string(count)}}));
}
void SceneLoader::UnloadAll() {
  size_t count = 0;
  for (auto &s : gSessions) {
    count += s->Entities.size();
    DestroySession(*s);
  }
  gSessions.clear();
  Utils::ShowSubtitle(gLanguage.Format("subtitle.scenes_unloaded",
                                       {{"count", std::to_string(count)}}));
  LOG_INFO("[Scene] Unloaded {} tracked entities", count);
}
std::vector<SceneLoader::SessionInfo> SceneLoader::ActiveSessions() {
  std::vector<SessionInfo> result;
  for (const auto &s : gSessions) {
    size_t count =
        std::count_if(s->Entities.begin(), s->Entities.end(),
                      [](Entity e) { return ENTITY::DOES_ENTITY_EXIST(e); });
    result.push_back({s->Id, s->Name, count, s->Failures.size()});
  }
  return result;
}
std::size_t SceneLoader::LoadedEntityCount() {
  size_t count = 0;
  for (const auto &s : gSessions)
    count +=
        std::count_if(s->Entities.begin(), s->Entities.end(),
                      [](Entity e) { return ENTITY::DOES_ENTITY_EXIST(e); });
  return count;
}
