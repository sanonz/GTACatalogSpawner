#include "TeleportStore.h"

#include "Logger.h"
#include "Utils.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <xmllite.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <system_error>

TeleportStore gTeleports;

namespace {
std::string ReaderValue(IXmlReader* reader) {
    const wchar_t* value = nullptr;
    unsigned int length = 0;
    if (FAILED(reader->GetValue(&value, &length)))
        return {};
    return Utils::WideToUtf8(value, length);
}

bool ParseFloatStrict(const std::string& raw, float& output) {
    const std::string trimmed = Utils::Trim(raw);
    if (trimmed.empty())
        return false;
    char* end = nullptr;
    const float parsed = std::strtof(trimmed.c_str(), &end);
    if (end != trimmed.c_str() + trimmed.size() || !std::isfinite(parsed))
        return false;
    output = parsed;
    return true;
}

std::string EscapeXml(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}
}

void TeleportStore::SetPath(std::filesystem::path path) {
    path_ = std::move(path);
}

bool TeleportStore::Load() {
    if (path_.empty())
        return false;

    std::error_code fsError;
    if (!std::filesystem::exists(path_, fsError)) {
        locations_.clear();
        return true;
    }

    IStream* stream = nullptr;
    HRESULT result = SHCreateStreamOnFileEx(path_.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
        FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream);
    if (FAILED(result)) {
        LOG_WARNING("[Teleport] Failed to open {} (0x{:08X})", path_.string(),
            static_cast<unsigned int>(result));
        return false;
    }

    IXmlReader* reader = nullptr;
    result = CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(&reader), nullptr);
    if (FAILED(result)) {
        stream->Release();
        LOG_ERROR("[Teleport] Failed to create XML reader (0x{:08X})",
            static_cast<unsigned int>(result));
        return false;
    }

    result = reader->SetInput(stream);
    bool rootSeen = false;
    bool insideLocation = false;
    std::string currentField;
    std::string currentValue;
    SavedLocation current{};
    bool hasX = false;
    bool hasY = false;
    bool hasZ = false;
    std::vector<SavedLocation> parsed;
    XmlNodeType nodeType = XmlNodeType_None;

    while (SUCCEEDED(result) && (result = reader->Read(&nodeType)) == S_OK) {
        const wchar_t* localName = nullptr;
        unsigned int length = 0;
        reader->GetLocalName(&localName, &length);
        const std::string name = Utils::WideToUtf8(localName, length);
        unsigned int depth = 0;
        reader->GetDepth(&depth);

        if (nodeType == XmlNodeType_Element) {
            if (!rootSeen && depth == 0 && name == "teleports") {
                rootSeen = true;
            }
            else if (rootSeen && depth == 1 && name == "location" && !insideLocation) {
                insideLocation = !reader->IsEmptyElement();
                current = {};
                hasX = false;
                hasY = false;
                hasZ = false;
            }
            else if (insideLocation && depth == 2 && currentField.empty()) {
                currentField = name;
                currentValue.clear();
                if (reader->IsEmptyElement())
                    currentField.clear();
            }
            else if (insideLocation && !currentField.empty()) {
                LOG_WARNING("[Teleport] Nested fields are not supported in {}", path_.string());
                result = E_FAIL;
            }
        }
        else if (!currentField.empty() && (nodeType == XmlNodeType_Text ||
            nodeType == XmlNodeType_CDATA || nodeType == XmlNodeType_Whitespace)) {
            currentValue += ReaderValue(reader);
        }
        else if (!currentField.empty() && nodeType == XmlNodeType_EndElement &&
            name == currentField) {
            const std::string field = Utils::ToLower(currentField);
            const std::string value = Utils::Trim(currentValue);
            if (field == "name") {
                current.Name = value;
            }
            else if (field == "zone") {
                current.Zone = value;
            }
            else if (field == "x") {
                hasX = ParseFloatStrict(value, current.Position.x);
            }
            else if (field == "y") {
                hasY = ParseFloatStrict(value, current.Position.y);
            }
            else if (field == "z") {
                hasZ = ParseFloatStrict(value, current.Position.z);
            }
            else if (field == "heading") {
                float heading = 0.0f;
                if (ParseFloatStrict(value, heading)) {
                    current.Heading = heading;
                    current.HasHeading = true;
                }
            }
            currentField.clear();
            currentValue.clear();
        }
        else if (insideLocation && nodeType == XmlNodeType_EndElement &&
            depth == 2 && name == "location") {
            insideLocation = false;
            current.Name = Utils::Trim(current.Name);
            if (!current.Name.empty() && hasX && hasY && hasZ) {
                parsed.push_back(std::move(current));
            }
            else {
                LOG_WARNING("[Teleport] Ignoring incomplete location entry in {}", path_.string());
            }
        }
    }

    reader->Release();
    stream->Release();
    if (FAILED(result) || !rootSeen || insideLocation || !currentField.empty()) {
        LOG_WARNING("[Teleport] Invalid teleports XML: {}", path_.string());
        return false;
    }

    locations_ = std::move(parsed);
    LOG_INFO("[Teleport] Loaded {} saved location(s) from {}", locations_.size(),
        path_.filename().string());
    return true;
}

bool TeleportStore::Save() const {
    if (path_.empty())
        return false;

    std::error_code fsError;
    if (path_.has_parent_path())
        std::filesystem::create_directories(path_.parent_path(), fsError);

    const std::filesystem::path tempPath = path_.wstring() + L".tmp";
    {
        std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            LOG_ERROR("[Teleport] Failed to open temp file for writing: {}", tempPath.string());
            return false;
        }

        output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        output << "<teleports>\n";
        for (const auto& location : locations_) {
            output << "  <location>\n";
            output << "    <name>" << EscapeXml(location.Name) << "</name>\n";
            output << "    <x>" << std::format("{:.2f}", location.Position.x) << "</x>\n";
            output << "    <y>" << std::format("{:.2f}", location.Position.y) << "</y>\n";
            output << "    <z>" << std::format("{:.2f}", location.Position.z) << "</z>\n";
            if (location.HasHeading)
                output << "    <heading>" << std::format("{:.2f}", location.Heading) << "</heading>\n";
            if (!location.Zone.empty())
                output << "    <zone>" << EscapeXml(location.Zone) << "</zone>\n";
            output << "  </location>\n";
        }
        output << "</teleports>\n";
        output.flush();
        if (!output.good()) {
            LOG_ERROR("[Teleport] Failed writing temp file: {}", tempPath.string());
            return false;
        }
    }

    if (!MoveFileExW(tempPath.c_str(), path_.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD winError = GetLastError();
        std::filesystem::remove(tempPath, fsError);
        LOG_ERROR("[Teleport] Failed to replace {} (error {})", path_.string(), winError);
        return false;
    }

    LOG_INFO("[Teleport] Saved {} location(s) to {}", locations_.size(), path_.filename().string());
    return true;
}

bool TeleportStore::Add(SavedLocation location) {
    location.Name = Utils::Trim(location.Name);
    location.Zone = Utils::Trim(location.Zone);
    if (location.Name.empty())
        return false;

    locations_.push_back(std::move(location));
    if (!Save()) {
        locations_.pop_back();
        return false;
    }
    return true;
}

bool TeleportStore::RemoveAt(std::size_t index) {
    if (index >= locations_.size())
        return false;

    SavedLocation backup = std::move(locations_[index]);
    locations_.erase(locations_.begin() + static_cast<std::ptrdiff_t>(index));
    if (!Save()) {
        locations_.insert(locations_.begin() + static_cast<std::ptrdiff_t>(index), std::move(backup));
        return false;
    }
    return true;
}

const std::vector<SavedLocation>& TeleportStore::Locations() const {
    return locations_;
}

std::string TeleportStore::NextDefaultName(const std::string& baseName,
    const std::string& fallbackPrefix) const {
    const std::string cleanedBase = Utils::Trim(baseName);
    const std::string prefix = cleanedBase.empty()
        ? (fallbackPrefix.empty() ? std::string("Location") : Utils::Trim(fallbackPrefix))
        : cleanedBase;

    const auto exists = [&](const std::string& candidate) {
        const std::string lower = Utils::ToLower(candidate);
        return std::any_of(locations_.begin(), locations_.end(), [&](const SavedLocation& item) {
            return Utils::ToLower(item.Name) == lower;
        });
    };

    if (!cleanedBase.empty() && !exists(cleanedBase))
        return cleanedBase;

    for (std::size_t number = cleanedBase.empty() ? 1 : 2;; ++number) {
        const std::string candidate = std::format("{} #{}", prefix, number);
        if (!exists(candidate))
            return candidate;
    }
}
