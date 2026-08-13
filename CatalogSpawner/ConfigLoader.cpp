#include "ConfigLoader.h"

#include "Logger.h"
#include "Utils.h"

#include <Windows.h>
#include <Shlwapi.h>
#include <xmllite.h>

#include <algorithm>
#include <charconv>
#include <set>

CatalogRepository gCatalogs;

Hash CatalogItem::ModelHash() const {
    return Utils::Joaat(SpawnName);
}

namespace {
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

std::string KindName(CatalogKind kind) {
    switch (kind) {
    case CatalogKind::Ped: return "ped";
    case CatalogKind::Weapon: return "weapon";
    case CatalogKind::Vehicle: return "vehicle";
    case CatalogKind::Scene: return "scene";
    }
    return {};
}

void ApplyField(CatalogItem& item, const std::string& field, const std::string& value) {
    if (field == "displayName")
        item.DisplayName = value;
    else if (field == "spawnName")
        item.SpawnName = value;
    else if (field == "scenePath")
        item.ScenePath = std::filesystem::path(std::u8string(value.begin(), value.end()));
    else if (field == "preview")
        item.PreviewPath = std::filesystem::path(std::u8string(value.begin(), value.end()));
    else if (field == "category")
        item.Category = value;
    else if (field == "description")
        item.Description = value;
    else if (field == "ammo") {
        int ammo = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), ammo);
        if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && ammo >= 0)
            item.Ammo = ammo;
    }
}
}

bool CatalogRepository::ParseCatalog(const std::filesystem::path& path, CatalogKind expectedKind,
    const std::filesystem::path& dataDirectory, std::vector<CatalogItem>& output) {
    IStream* stream = nullptr;
    HRESULT result = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
        FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &stream);
    if (FAILED(result)) {
        LOG_WARNING("[Catalog] Failed to open {} (0x{:08X})", path.string(),
            static_cast<unsigned int>(result));
        return false;
    }

    IXmlReader* reader = nullptr;
    result = CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(&reader), nullptr);
    if (FAILED(result)) {
        stream->Release();
        LOG_ERROR("[Catalog] Failed to create XML reader (0x{:08X})",
            static_cast<unsigned int>(result));
        return false;
    }

    result = reader->SetInput(stream);
    bool rootSeen = false;
    bool insideItem = false;
    std::string currentField;
    std::string currentValue;
    CatalogItem currentItem;
    currentItem.Kind = expectedKind;
    std::vector<CatalogItem> parsedItems;
    std::set<std::string> uniqueIds;
    XmlNodeType nodeType = XmlNodeType_None;

    while (SUCCEEDED(result) && (result = reader->Read(&nodeType)) == S_OK) {
        const wchar_t* localName = nullptr;
        unsigned int length = 0;
        reader->GetLocalName(&localName, &length);
        const std::string name = Utils::WideToUtf8(localName, length);
        unsigned int depth = 0;
        reader->GetDepth(&depth);

        if (nodeType == XmlNodeType_Element) {
            if (!rootSeen && depth == 0 && name == "catalog") {
                rootSeen = true;
                const std::string type = Utils::ToLower(Utils::Trim(Attribute(reader, L"type")));
                if (!type.empty() && type != KindName(expectedKind)) {
                    LOG_WARNING("[Catalog] Type '{}' doesn't match file {}", type, path.string());
                    result = E_FAIL;
                }
            }
            else if (rootSeen && depth == 1 && name == "item" && !insideItem) {
                insideItem = true;
                currentItem = {};
                currentItem.Kind = expectedKind;
            }
            else if (insideItem && depth == 2 && name == "info") {
                const std::string label = Utils::Trim(Attribute(reader, L"label"));
                const std::string value = Utils::Trim(Attribute(reader, L"value"));
                if (!label.empty() && !value.empty())
                    currentItem.ExtraInfo.emplace_back(label, value);
            }
            else if (insideItem && depth == 2 && currentField.empty()) {
                currentField = name;
                currentValue.clear();
                if (reader->IsEmptyElement())
                    currentField.clear();
            }
            else if (insideItem && !currentField.empty()) {
                LOG_WARNING("[Catalog] Nested fields are not supported in {}", path.string());
                result = E_FAIL;
            }
        }
        else if (!currentField.empty() && (nodeType == XmlNodeType_Text ||
            nodeType == XmlNodeType_CDATA || nodeType == XmlNodeType_Whitespace)) {
            currentValue += ReaderValue(reader);
        }
        else if (!currentField.empty() && nodeType == XmlNodeType_EndElement &&
            name == currentField) {
            ApplyField(currentItem, currentField, Utils::Trim(currentValue));
            currentField.clear();
            currentValue.clear();
        }
        else if (insideItem && nodeType == XmlNodeType_EndElement && depth == 2 && name == "item") {
            insideItem = false;
            currentItem.DisplayName = Utils::Trim(currentItem.DisplayName);
            currentItem.SpawnName = Utils::Trim(currentItem.SpawnName);
            const bool isScene = expectedKind == CatalogKind::Scene;
            if (currentItem.DisplayName.empty() ||
                (isScene ? currentItem.ScenePath.empty() : currentItem.SpawnName.empty())) {
                LOG_WARNING("[Catalog] Ignoring item without displayName/{} in {}",
                    isScene ? "scenePath" : "spawnName", path.string());
                continue;
            }
            const std::string dedupeKey = Utils::ToLower(isScene
                ? currentItem.ScenePath.generic_string()
                : currentItem.SpawnName);
            if (!uniqueIds.insert(dedupeKey).second) {
                LOG_WARNING("[Catalog] Ignoring duplicate {} '{}' in {}",
                    isScene ? "scenePath" : "spawnName", dedupeKey, path.string());
                continue;
            }
            if (isScene && currentItem.ScenePath.is_relative())
                currentItem.ScenePath = dataDirectory / currentItem.ScenePath;
            if (!currentItem.PreviewPath.empty() && currentItem.PreviewPath.is_relative())
                currentItem.PreviewPath = dataDirectory / currentItem.PreviewPath;
            parsedItems.push_back(std::move(currentItem));
        }
    }

    reader->Release();
    stream->Release();
    if (FAILED(result) || !rootSeen || insideItem || !currentField.empty()) {
        LOG_WARNING("[Catalog] Invalid catalog: {}", path.string());
        return false;
    }

    output = std::move(parsedItems);
    LOG_INFO("[Catalog] Loaded {} item(s) from {}", output.size(), path.filename().string());
    return true;
}

bool CatalogRepository::Reload(const std::filesystem::path& dataDirectory) {
    bool success = true;
    std::vector<CatalogItem> parsed;
    if (ParseCatalog(dataDirectory / L"peds.xml", CatalogKind::Ped, dataDirectory, parsed))
        peds_ = std::move(parsed);
    else
        success = false;

    parsed.clear();
    if (ParseCatalog(dataDirectory / L"weapons.xml", CatalogKind::Weapon, dataDirectory, parsed))
        weapons_ = std::move(parsed);
    else
        success = false;

    parsed.clear();
    if (ParseCatalog(dataDirectory / L"vehicles.xml", CatalogKind::Vehicle, dataDirectory, parsed))
        vehicles_ = std::move(parsed);
    else
        success = false;

    parsed.clear();
    if (ParseCatalog(dataDirectory / L"scenes.xml", CatalogKind::Scene, dataDirectory, parsed))
        scenes_ = std::move(parsed);
    else
        success = false;
    return success;
}

const std::vector<CatalogItem>& CatalogRepository::Peds() const {
    return peds_;
}

const std::vector<CatalogItem>& CatalogRepository::Weapons() const {
    return weapons_;
}

const std::vector<CatalogItem>& CatalogRepository::Vehicles() const {
    return vehicles_;
}

const std::vector<CatalogItem>& CatalogRepository::Scenes() const {
    return scenes_;
}
