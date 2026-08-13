#include "SceneTasks.h"

#include "Logger.h"
#include "Utils.h"

#include <Windows.h>
#include <inc/natives.h>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;

Vector3 Vector(const SceneXmlNode &node, const char *child) {
  const SceneXmlNode *value = node.Child(child);
  if (!value)
    return {};
  return {value->Float("X"), value->Float("Y"), value->Float("Z")};
}

Vector3 Direction(float heading, float pitch, float magnitude) {
  const float h = heading * kPi / 180.0f;
  const float p = pitch * kPi / 180.0f;
  return {-std::sin(h) * std::cos(p) * magnitude,
          std::cos(h) * std::cos(p) * magnitude, std::sin(p) * magnitude};
}

Entity Resolve(const SceneXmlNode &node, const char *field,
               SceneTaskContext &context) {
  const std::string raw = Utils::Trim(node.Value(field));
  if (Utils::ToLower(raw) == "player")
    return PLAYER::PLAYER_PED_ID();
  const std::int64_t id = static_cast<std::int64_t>(node.Int(field));
  if (context.Handles) {
    const auto found = context.Handles->find(id);
    if (found != context.Handles->end())
      return found->second;
  }
  return 0;
}

Vehicle VehicleFor(Ped ped) {
  return PED::IS_PED_IN_ANY_VEHICLE(ped, false)
             ? PED::GET_VEHICLE_PED_IS_IN(ped, false)
             : 0;
}

void SetBlipName(Blip blip, const std::string &name) {
  if (name.empty())
    return;
  HUD::BEGIN_TEXT_COMMAND_SET_BLIP_NAME("STRING");
  HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME(name.c_str());
  HUD::END_TEXT_COMMAND_SET_BLIP_NAME(blip);
}

void RecordFailure(SceneTaskContext &context, const std::string &value) {
  if (context.Failures)
    context.Failures->push_back(value);
  LOG_WARNING("[Scene] {}", value);
}
} // namespace

SceneTaskSequence::SceneTaskSequence(Entity owner, const SceneXmlNode &node)
    : owner_(owner) {
  for (const SceneXmlNode *item : node.ChildrenNamed("Task")) {
    Task task;
    task.Xml = *item;
    task.Type = item->Int("Type", 2);
    task.Duration = item->Int("Duration", 0);
    task.AfterLife = item->Int("KeepTaskRunningAfterTime", 0);
    task.Looped = item->Bool("IsLoopedTask", false);
    if (task.Type >= 0 && task.Type <= 54)
      tasks_.push_back(std::move(task));
  }
  active_ = ENTITY::DOES_ENTITY_EXIST(owner_) && !tasks_.empty();
}

void SceneTaskSequence::Tick(SceneTaskContext &context) {
  if (!active_ || !ENTITY::DOES_ENTITY_EXIST(owner_)) {
    active_ = false;
    return;
  }
  if (index_ >= tasks_.size()) {
    index_ = 0;
    started_ = false;
  }
  Task &task = tasks_[index_];
  const unsigned long long now = GetTickCount64();
  if (!started_) {
    if (!StartTask(task, context)) {
      active_ = false;
      return;
    }
    started_ = true;
    deadline_ =
        now + static_cast<unsigned long long>(std::max(0, task.Duration));
  }
  if (task.Looped)
    StartTask(task, context);
  if (task.Duration > 0 && now < deadline_)
    return;
  if (task.AfterLife <= 0 && ENTITY::IS_ENTITY_A_PED(owner_))
    TASK::CLEAR_PED_TASKS(static_cast<Ped>(owner_));
  ++index_;
  started_ = false;
}

void SceneTaskSequence::Stop() {
  if (ENTITY::DOES_ENTITY_EXIST(owner_) && ENTITY::IS_ENTITY_A_PED(owner_))
    TASK::CLEAR_PED_TASKS_IMMEDIATELY(static_cast<Ped>(owner_));
  active_ = false;
}

bool SceneTaskSequence::StartTask(Task &task, SceneTaskContext &context) {
  const SceneXmlNode &n = task.Xml;
  const Ped ped = static_cast<Ped>(owner_);
  const int duration =
      task.AfterLife > 0 || task.Duration <= 0 ? -1 : task.Duration;
  const Vector3 position = Vector(n, "Position");
  const Entity target = Resolve(n, "TargetInitHandle", context);
  switch (task.Type) {
  case 0:
    if (ENTITY::IS_ENTITY_A_PED(owner_)) {
      TASK::CLEAR_PED_TASKS_IMMEDIATELY(ped);
      TASK::TASK_CLEAR_LOOK_AT(ped);
    }
    return true;
  case 1:
    Stop();
    return false;
  case 2:
    return true;
  case 3:
    TASK::TASK_PAUSE(ped, duration);
    return true;
  case 4:
    if (task.AfterLife > 0)
      TASK::TASK_USE_MOBILE_PHONE(ped, true, 1);
    else
      TASK::TASK_USE_MOBILE_PHONE_TIMED(ped, duration);
    return true;
  case 5:
    TASK::TASK_WRITHE(ped, PLAYER::PLAYER_PED_ID(), duration, 0, false, 0);
    return true;
  case 6:
    TASK::TASK_FOLLOW_NAV_MESH_TO_COORD(ped, Vector(n, "Destination"),
                                        n.Float("Speed", 1.0f), duration, 0.5f,
                                        0, 0.0f);
    return true;
  case 7: {
    TASK::TASK_FLUSH_ROUTE();
    if (const auto *route = n.Child("Route"))
      for (const auto &point : route->Children)
        TASK::TASK_EXTEND_ROUTE(
            {point.Float("X"), point.Float("Y"), point.Float("Z")});
    TASK::TASK_FOLLOW_POINT_ROUTE(ped, n.Float("Speed", 1.0f), 0);
    return true;
  }
  case 8:
    TASK::TASK_WANDER_IN_AREA(ped, Vector(n, "Coord"), n.Float("Radius", 20.0f),
                              2.0f, 10.0f);
    return true;
  case 9:
    TASK::TASK_WANDER_STANDARD(ped, 10.0f, 10);
    return true;
  case 10:
    TASK::TASK_START_SCENARIO_IN_PLACE(ped, n.Value("ScenarioName").c_str(),
                                       duration, true);
    return true;
  case 11:
    TASK::TASK_COMBAT_HATED_TARGETS_AROUND_PED(ped, n.Float("Radius", 30.0f),
                                               0);
    return true;
  case 12:
    if (ENTITY::DOES_ENTITY_EXIST(target))
      TASK::TASK_COMBAT_PED(ped, static_cast<Ped>(target), 0, 16);
    return true;
  case 13:
    if (ENTITY::DOES_ENTITY_EXIST(target))
      TASK::TASK_CHAT_TO_PED(ped, static_cast<Ped>(target), 16, {}, 0.0f, 0.0f);
    return true;
  case 14: {
    const std::string dict = n.Value("AnimDict");
    const std::string anim = n.Value("AnimName");
    STREAMING::REQUEST_ANIM_DICT(dict.c_str());
    const unsigned long long started = GetTickCount64();
    while (!STREAMING::HAS_ANIM_DICT_LOADED(dict.c_str()) &&
           GetTickCount64() - started < 5000)
      WAIT(0);
    if (STREAMING::HAS_ANIM_DICT_LOADED(dict.c_str()))
      TASK::TASK_PLAY_ANIM(
          ped, dict.c_str(), anim.c_str(), n.Float("Speed", 8.0f),
          n.Float("SpeedMultiplier", -8.0f), duration, n.Int("Flag", 0), 0.0f,
          n.Bool("LockPos"), n.Bool("LockPos"), n.Bool("LockPos"));
    if (n.Bool("OverrideDurationToNormalSpeedAnimDuration"))
      task.Duration = static_cast<int>(
          ENTITY::GET_ANIM_DURATION(dict.c_str(), anim.c_str()) * 1000.0f);
    return true;
  }
  case 15:
    TASK::TASK_SMART_FLEE_COORD(ped, Vector(n, "Origin"), 1000.0f, duration,
                                false, false);
    return true;
  case 16:
    AUDIO::PLAY_PED_AMBIENT_SPEECH_WITH_VOICE_NATIVE(
        ped, n.Value("SpeechName").c_str(), n.Value("VoiceName").c_str(),
        n.Value("ParamName", "SPEECH_PARAMS_FORCE").c_str(), false);
    return true;
  case 17:
    if (n.Bool("Warp"))
      TASK::TASK_USE_NEAREST_SCENARIO_TO_COORD_WARP(
          ped, ENTITY::GET_ENTITY_COORDS(owner_, true),
          n.Float("SearchRadius", 20.0f), 0);
    else
      TASK::TASK_USE_NEAREST_SCENARIO_TO_COORD(
          ped, ENTITY::GET_ENTITY_COORDS(owner_, true),
          n.Float("SearchRadius", 20.0f), duration);
    return true;
  case 18:
    TASK::TASK_PED_SLIDE_TO_COORD(ped, Vector(n, "Destination"),
                                  n.Float("Heading"), n.Float("Speed", 1.0f));
    return true;
  case 19:
    PED::SET_PED_TO_LOAD_COVER(ped, true);
    TASK::TASK_PUT_PED_DIRECTLY_INTO_COVER(ped, Vector(n, "CoverPos"), duration,
                                           true, 0.5f, true, true, 0, false);
    PED::SET_PED_CAN_PEEK_IN_COVER(ped, n.Bool("CanPeakInCover"));
    return true;
  case 20:
    TASK::TASK_ACHIEVE_HEADING(ped, n.Float("Heading"), duration);
    return true;
  case 21:
    if (ENTITY::DOES_ENTITY_EXIST(target))
      TASK::TASK_TURN_PED_TO_FACE_ENTITY(ped, target, duration);
    return true;
  case 22: {
    Entity move = owner_;
    if (n.Bool("TakeVehicleToo") && VehicleFor(ped))
      move = VehicleFor(ped);
    ENTITY::SET_ENTITY_COORDS_NO_OFFSET(move, Vector(n, "Destination"), false,
                                        false, false);
    return true;
  }
  case 23:
    if (ENTITY::DOES_ENTITY_EXIST(target))
      TASK::TASK_FOLLOW_TO_OFFSET_OF_ENTITY(
          ped, target,
          {MISC::GET_RANDOM_FLOAT_IN_RANGE(-1.0f, 1.0f),
           MISC::GET_RANDOM_FLOAT_IN_RANGE(-1.5f, 0.5f), 0.0f},
          n.Float("Speed", 1.0f), duration, 0.1f, true);
    return true;
  case 24:
    TASK::TASK_THROW_PROJECTILE(ped, Vector(n, "TargetPos"), 0, 0);
    return true;
  case 25: {
    float heading = n.Float("Heading"), pitch = n.Float("Pitch");
    if (n.Bool("IsRelative")) {
      const Vector3 rotation = ENTITY::GET_ENTITY_ROTATION(owner_, 2);
      heading += rotation.z;
      pitch += rotation.x;
    }
    ENTITY::SET_ENTITY_VELOCITY(
        owner_, Direction(heading, pitch, n.Float("Magnitude")));
    return true;
  }
  case 26:
    ENTITY::APPLY_FORCE_TO_ENTITY(
        owner_, n.Int("ForceType", 1),
        Direction(n.Float("Heading"), n.Float("Pitch"), n.Float("Magnitude")),
        Vector(n, "OffsetVector"), 0, n.Bool("IsRelative"), true, true, false,
        true);
    return true;
  case 27: {
    const Vector3 here = ENTITY::GET_ENTITY_COORDS(owner_, true),
                  goal = Vector(n, "Point");
    const float gain = n.Float("AngularFrequency", 1.0f),
                damp = n.Float("DampRatio", 0.1f);
    const Vector3 vel = ENTITY::GET_ENTITY_VELOCITY(owner_);
    ENTITY::SET_ENTITY_VELOCITY(owner_,
                                {(goal.x - here.x) * gain - vel.x * damp,
                                 (goal.y - here.y) * gain - vel.y * damp,
                                 (goal.z - here.z) * gain - vel.z * damp});
    return true;
  }
  case 28:
    if (n.Int("FreezeType") == 0)
      ENTITY::FREEZE_ENTITY_POSITION(owner_, true);
    else if (n.Int("FreezeType") == 1)
      ENTITY::FREEZE_ENTITY_POSITION(owner_, false);
    else {
      ENTITY::FREEZE_ENTITY_POSITION(owner_, true);
      ENTITY::FREEZE_ENTITY_POSITION(owner_, false);
    }
    return true;
  case 29: {
    const Vehicle v = ENTITY::IS_ENTITY_A_VEHICLE(owner_)
                          ? static_cast<Vehicle>(owner_)
                          : VehicleFor(ped);
    if (v && (!n.Bool("OnGroundOnly") || VEHICLE::IS_VEHICLE_ON_ALL_WHEELS(v)))
      VEHICLE::SET_VEHICLE_FORWARD_SPEED(v, n.Float("SpeedInKmph") / 3.6f);
    return true;
  }
  case 30: {
    const Entity vehicle = Resolve(n, "VehicleInitHandle", context);
    if (ENTITY::DOES_ENTITY_EXIST(vehicle))
      TASK::TASK_ENTER_VEHICLE(ped, static_cast<Vehicle>(vehicle), duration,
                               n.Int("SeatIndex", -2), 2.0f, 1, nullptr, 0);
    return true;
  }
  case 31:
    if (const Vehicle v = VehicleFor(ped))
      TASK::TASK_LEAVE_VEHICLE(ped, v, 0);
    return true;
  case 32: {
    const Entity vehicle = Resolve(n, "VehicleInitHandle", context);
    if (ENTITY::DOES_ENTITY_EXIST(vehicle))
      PED::SET_PED_INTO_VEHICLE(ped, static_cast<Vehicle>(vehicle),
                                n.Int("SeatIndex", -2));
    return true;
  }
  case 33: {
    const Vehicle v = ENTITY::IS_ENTITY_A_VEHICLE(owner_)
                          ? static_cast<Vehicle>(owner_)
                          : VehicleFor(ped);
    if (v)
      TASK::TASK_EVERYONE_LEAVE_VEHICLE(v);
    return true;
  }
  case 34:
    if (const Vehicle v = VehicleFor(ped))
      TASK::TASK_VEHICLE_DRIVE_WANDER(ped, v,
                                      n.Float("SpeedInKmph", 30.0f) / 3.6f,
                                      n.Int("DrivingStyle", 786603));
    return true;
  case 35:
    if (const Vehicle v = VehicleFor(ped))
      TASK::TASK_VEHICLE_DRIVE_TO_COORD_LONGRANGE(
          ped, v, Vector(n, "Destination"),
          n.Float("SpeedInKmph", 30.0f) / 3.6f, n.Int("DrivingStyle", 786603),
          5.0f);
    return true;
  case 36:
    if (const Vehicle v = VehicleFor(ped);
        v && ENTITY::DOES_ENTITY_EXIST(target))
      TASK::TASK_VEHICLE_ESCORT(ped, v, target, -1,
                                n.Float("SpeedInKmph", 30.0f) / 3.6f,
                                n.Int("DrivingStyle", 786603),
                                n.Float("MinDistance", 10.0f), 0, 5.0f);
    return true;
  case 37:
    if (const Vehicle v = VehicleFor(ped))
      TASK::TASK_PLANE_LAND(ped, v, Vector(n, "RunwayStart"),
                            Vector(n, "RunwayEnd"));
    return true;
  case 38:
    ENTITY::SET_ENTITY_ALPHA(
        owner_, std::clamp(n.Int("OpacityValue", 255), 0, 255), false);
    return true;
  case 39: {
    const unsigned long long now = GetTickCount64();
    if (task.LastAction && now - task.LastAction < n.UInt("Delay"))
      return true;
    task.LastAction = now;
    const std::string asset = n.Value("AssetName"),
                      effect = n.Value("EffectName");
    STREAMING::REQUEST_NAMED_PTFX_ASSET(asset.c_str());
    const unsigned long long started = GetTickCount64();
    while (!STREAMING::HAS_NAMED_PTFX_ASSET_LOADED(asset.c_str()) &&
           GetTickCount64() - started < 1000)
      WAIT(0);
    if (!STREAMING::HAS_NAMED_PTFX_ASSET_LOADED(asset.c_str()))
      return true;
    GRAPHICS::USE_PARTICLE_FX_ASSET(asset.c_str());
    if (const auto *colour = n.Child("Colour")) {
      GRAPHICS::SET_PARTICLE_FX_NON_LOOPED_COLOUR(
          colour->Float("R", 255.0f) / 255.0f,
          colour->Float("G", 255.0f) / 255.0f,
          colour->Float("B", 255.0f) / 255.0f);
      GRAPHICS::SET_PARTICLE_FX_NON_LOOPED_ALPHA(colour->Float("A", 255.0f) /
                                                 255.0f);
    }
    GRAPHICS::START_PARTICLE_FX_NON_LOOPED_ON_ENTITY(
        effect.c_str(), owner_, Vector(n, "RelativePosition"),
        Vector(n, "RelativeRotation"), n.Float("Scale", 1.0f), false, false,
        false);
    return true;
  }
  case 40:
    TASK::TASK_LOOK_AT_COORD(ped, position, duration, 0, 2);
    return true;
  case 41:
    if (ENTITY::DOES_ENTITY_EXIST(target))
      TASK::TASK_LOOK_AT_ENTITY(ped, target, duration, 0, 2);
    return true;
  case 42:
    TASK::TASK_SHOOT_AT_COORD(ped, position, duration, 0);
    return true;
  case 43:
    if (ENTITY::DOES_ENTITY_EXIST(target))
      TASK::TASK_SHOOT_AT_ENTITY(ped, target, duration, 0);
    return true;
  case 44:
    if (ENTITY::IS_ENTITY_AN_OBJECT(owner_))
      OBJECT::SET_OBJECT_TINT_INDEX(owner_, n.Int("NewValue"));
    return true;
  case 45:
    ENTITY::SET_ENTITY_HEALTH(owner_, n.Int("HealthValue", 100), 0, 0);
    return true;
  case 46:
    WEAPON::SET_CURRENT_PED_WEAPON(ped, n.UInt("WeaponHash"), true);
    return true;
  case 47: {
    Vector3 r{n.Float("Pitch"), n.Float("Roll"), n.Float("Yaw")};
    if (n.Bool("IsRelative")) {
      const Vector3 c = ENTITY::GET_ENTITY_ROTATION(owner_, 2);
      r = {c.x + r.x, c.y + r.y, c.z + r.z};
    }
    ENTITY::SET_ENTITY_ROTATION(owner_, r.x, r.y, r.z, 2, true);
    return true;
  }
  case 48: {
    if (!ENTITY::DOES_ENTITY_EXIST(target))
      return true;
    const Vector3 base = ENTITY::GET_OFFSET_FROM_ENTITY_IN_WORLD_COORDS(
        target, Vector(n, "OffsetVector"));
    const Vector3 here = ENTITY::GET_ENTITY_COORDS(owner_, true),
                  vel = ENTITY::GET_ENTITY_VELOCITY(owner_);
    const float gain = n.Float("AngularFrequency", 1.0f),
                damp = n.Float("DampRatio", 0.1f);
    ENTITY::SET_ENTITY_VELOCITY(owner_,
                                {(base.x - here.x) * gain - vel.x * damp,
                                 (base.y - here.y) * gain - vel.y * damp,
                                 (base.z - here.z) * gain - vel.z * damp});
    return true;
  }
  case 49:
    TASK::TASK_AIM_GUN_AT_COORD(ped, position, duration, false, false);
    return true;
  case 50:
    if (ENTITY::DOES_ENTITY_EXIST(target))
      TASK::TASK_AIM_GUN_AT_ENTITY(ped, target, duration, false);
    return true;
  case 51: {
    Blip blip = HUD::GET_BLIP_FROM_ENTITY(owner_);
    if (!HUD::DOES_BLIP_EXIST(blip)) {
      blip = HUD::ADD_BLIP_FOR_ENTITY(owner_);
      if (context.Blips)
        context.Blips->push_back(blip);
    }
    HUD::SET_BLIP_SPRITE(blip, n.Int("Icon", 1));
    HUD::SET_BLIP_COLOUR(blip, n.Int("Colour"));
    HUD::SET_BLIP_ALPHA(blip, n.Int("Alpha", 255));
    HUD::SET_BLIP_SCALE(blip, n.Float("Scale", 1.0f));
    HUD::SET_BLIP_FLASHES(blip, n.Bool("IsFlashing"));
    HUD::SET_BLIP_AS_FRIENDLY(blip, n.Bool("IsFriendly"));
    HUD::SET_BLIP_AS_SHORT_RANGE(blip, n.Bool("IsShortRange"));
    HUD::SET_BLIP_ROUTE(blip, n.Bool("ShowRoute"));
    if (n.Int("ShowNumber") != 0)
      HUD::SHOW_NUMBER_ON_BLIP(blip, n.Int("ShowNumber"));
    HUD::SET_BLIP_SHOW_CONE(blip, n.Bool("ShowCone"), n.Int("HudColorIndex"));
    HUD::SET_BLIP_DISPLAY(blip, n.Bool("IsSelectableOnMap", true) ? 2 : 8);
    HUD::SET_BLIP_PRIORITY(blip, n.Int("Priority", 2));
    if (n.Bool("SyncRotation"))
      HUD::SET_BLIP_ROTATION_WITH_FLOAT(blip,
                                        ENTITY::GET_ENTITY_HEADING(owner_));
    SetBlipName(blip, n.Value("Label"));
    return true;
  }
  case 52:
    if (Blip blip = HUD::GET_BLIP_FROM_ENTITY(owner_);
        HUD::DOES_BLIP_EXIST(blip)) {
      const Blip removed = blip;
      HUD::REMOVE_BLIP(&blip);
      if (context.Blips)
        context.Blips->erase(
            std::remove(context.Blips->begin(), context.Blips->end(), removed),
            context.Blips->end());
    }
    return true;
  case 53:
    TASK::TASK_LOOK_AT_COORD(ped, position, duration, 8192, 2);
    return true;
  case 54:
    if (ENTITY::DOES_ENTITY_EXIST(target))
      TASK::TASK_LOOK_AT_ENTITY(ped, target, duration, 8192, 2);
    return true;
  default:
    RecordFailure(context,
                  "Unsupported task type " + std::to_string(task.Type));
    return true;
  }
}
