#pragma once

#include "SceneXml.h"

#include <inc/types.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SceneTaskContext {
  std::unordered_map<std::int64_t, Entity> *Handles = nullptr;
  std::vector<Blip> *Blips = nullptr;
  std::vector<int> *ParticleFx = nullptr;
  std::vector<std::string> *Failures = nullptr;
};

class SceneTaskSequence {
public:
  SceneTaskSequence() = default;
  SceneTaskSequence(Entity owner, const SceneXmlNode &node);

  void Tick(SceneTaskContext &context);
  void Stop();
  bool Active() const { return active_; }

private:
  struct Task {
    SceneXmlNode Xml;
    int Type = 2;
    int Duration = 0;
    int AfterLife = 0;
    bool Looped = false;
    unsigned long long LastAction = 0;
  };

  Entity owner_ = 0;
  std::vector<Task> tasks_;
  std::size_t index_ = 0;
  unsigned long long deadline_ = 0;
  bool started_ = false;
  bool active_ = false;

  bool StartTask(Task &task, SceneTaskContext &context);
};
