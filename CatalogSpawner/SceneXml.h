#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct SceneXmlNode {
  std::string Name;
  std::string Text;
  std::unordered_map<std::string, std::string> Attributes;
  std::vector<SceneXmlNode> Children;

  const SceneXmlNode *Child(const std::string &name) const;
  std::vector<const SceneXmlNode *>
  ChildrenNamed(const std::string &name) const;
  std::string Value(const std::string &name,
                    const std::string &fallback = {}) const;
  std::string Attribute(const std::string &name,
                        const std::string &fallback = {}) const;
  int Int(const std::string &name, int fallback = 0) const;
  unsigned int UInt(const std::string &name, unsigned int fallback = 0) const;
  float Float(const std::string &name, float fallback = 0.0f) const;
  bool Bool(const std::string &name, bool fallback = false) const;
};

bool LoadSceneXml(const std::filesystem::path &path, SceneXmlNode &root,
                  std::string &error);
unsigned int SceneXmlHash(const std::string &value, unsigned int fallback = 0);
