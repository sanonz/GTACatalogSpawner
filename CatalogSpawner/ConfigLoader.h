#pragma once

#include "CatalogItem.h"

#include <filesystem>
#include <vector>

class CatalogRepository {
public:
    bool Reload(const std::filesystem::path& dataDirectory);

    const std::vector<CatalogItem>& Peds() const;
    const std::vector<CatalogItem>& Weapons() const;
    const std::vector<CatalogItem>& Vehicles() const;
    const std::vector<CatalogItem>& Scenes() const;

private:
    static bool ParseCatalog(const std::filesystem::path& path, CatalogKind expectedKind,
        const std::filesystem::path& dataDirectory, std::vector<CatalogItem>& output);

    std::vector<CatalogItem> peds_;
    std::vector<CatalogItem> weapons_;
    std::vector<CatalogItem> vehicles_;
    std::vector<CatalogItem> scenes_;
};

extern CatalogRepository gCatalogs;
