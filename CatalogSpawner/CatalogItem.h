#pragma once

#include <inc/types.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

enum class CatalogKind {
    Ped,
    Weapon,
    Vehicle,
};

struct CatalogItem {
    CatalogKind Kind = CatalogKind::Ped;
    std::string DisplayName;
    std::string SpawnName;
    std::filesystem::path PreviewPath;
    std::string Category;
    std::string Description;
    int Ammo = 300;
    std::vector<std::pair<std::string, std::string>> ExtraInfo;

    Hash ModelHash() const;
};

