#include "SceneXml.h"

#include "Utils.h"

#include <Shlwapi.h>
#include <Windows.h>
#include <xmllite.h>

#include <charconv>
#include <cstdlib>

namespace {
std::string ReaderString(IXmlReader *reader, bool localName) {
  const wchar_t *value = nullptr;
  unsigned int length = 0;
  const HRESULT result = localName ? reader->GetLocalName(&value, &length)
                                   : reader->GetValue(&value, &length);
  return FAILED(result) ? std::string() : Utils::WideToUtf8(value, length);
}

template <typename T>
T ParseInteger(const std::string &input, T fallback, int base = 10) {
  std::string value = Utils::Trim(input);
  if (value.empty())
    return fallback;
  T parsed{};
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed, base);
  return result.ec == std::errc{} && result.ptr == value.data() + value.size()
             ? parsed
             : fallback;
}
} // namespace

const SceneXmlNode *SceneXmlNode::Child(const std::string &name) const {
  for (const auto &child : Children) {
    if (child.Name == name)
      return &child;
  }
  return nullptr;
}

std::vector<const SceneXmlNode *>
SceneXmlNode::ChildrenNamed(const std::string &name) const {
  std::vector<const SceneXmlNode *> result;
  for (const auto &child : Children) {
    if (child.Name == name)
      result.push_back(&child);
  }
  return result;
}

std::string SceneXmlNode::Value(const std::string &name,
                                const std::string &fallback) const {
  const auto *child = Child(name);
  if (child)
    return Utils::Trim(child->Text);
  const auto attribute = Attributes.find(name);
  return attribute == Attributes.end() ? fallback
                                       : Utils::Trim(attribute->second);
}

std::string SceneXmlNode::Attribute(const std::string &name,
                                    const std::string &fallback) const {
  const auto found = Attributes.find(name);
  return found == Attributes.end() ? fallback : found->second;
}

int SceneXmlNode::Int(const std::string &name, int fallback) const {
  return ParseInteger(Value(name), fallback);
}

unsigned int SceneXmlNode::UInt(const std::string &name,
                                unsigned int fallback) const {
  return SceneXmlHash(Value(name), fallback);
}

float SceneXmlNode::Float(const std::string &name, float fallback) const {
  const std::string value = Value(name);
  if (value.empty())
    return fallback;
  char *end = nullptr;
  const float parsed = std::strtof(value.c_str(), &end);
  return end == value.c_str() + value.size() ? parsed : fallback;
}

bool SceneXmlNode::Bool(const std::string &name, bool fallback) const {
  const std::string value = Utils::ToLower(Utils::Trim(Value(name)));
  if (value == "true" || value == "1")
    return true;
  if (value == "false" || value == "0")
    return false;
  return fallback;
}

unsigned int SceneXmlHash(const std::string &input, unsigned int fallback) {
  std::string value = Utils::Trim(input);
  int base = 10;
  if (value.size() > 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    value.erase(0, 2);
    base = 16;
  }
  return ParseInteger<unsigned int>(value, fallback, base);
}

bool LoadSceneXml(const std::filesystem::path &path, SceneXmlNode &root,
                  std::string &error) {
  IStream *stream = nullptr;
  HRESULT result =
      SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
                             FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream);
  if (FAILED(result)) {
    error = "failed to open XML";
    return false;
  }

  IXmlReader *reader = nullptr;
  result = CreateXmlReader(__uuidof(IXmlReader),
                           reinterpret_cast<void **>(&reader), nullptr);
  if (FAILED(result)) {
    stream->Release();
    error = "failed to create XML reader";
    return false;
  }
  result = reader->SetInput(stream);
  std::vector<SceneXmlNode *> stack;
  bool rootSeen = false;
  XmlNodeType type = XmlNodeType_None;
  while (SUCCEEDED(result) && (result = reader->Read(&type)) == S_OK) {
    if (type == XmlNodeType_Element) {
      SceneXmlNode node;
      node.Name = ReaderString(reader, true);
      if (reader->MoveToFirstAttribute() == S_OK) {
        do {
          const std::string name = ReaderString(reader, true);
          node.Attributes[name] = ReaderString(reader, false);
        } while (reader->MoveToNextAttribute() == S_OK);
        reader->MoveToElement();
      }
      SceneXmlNode *inserted = nullptr;
      if (stack.empty()) {
        if (rootSeen) {
          result = E_FAIL;
          break;
        }
        root = std::move(node);
        inserted = &root;
        rootSeen = true;
      } else {
        stack.back()->Children.push_back(std::move(node));
        inserted = &stack.back()->Children.back();
      }
      if (!reader->IsEmptyElement())
        stack.push_back(inserted);
    } else if ((type == XmlNodeType_Text || type == XmlNodeType_CDATA ||
                type == XmlNodeType_Whitespace) &&
               !stack.empty()) {
      stack.back()->Text += ReaderString(reader, false);
    } else if (type == XmlNodeType_EndElement) {
      if (stack.empty()) {
        result = E_FAIL;
        break;
      }
      stack.pop_back();
    }
  }
  reader->Release();
  stream->Release();
  if (FAILED(result) || !rootSeen || !stack.empty()) {
    error = "malformed XML";
    return false;
  }
  return true;
}
