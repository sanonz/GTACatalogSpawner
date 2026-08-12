#pragma once

#include "CatalogItem.h"

namespace Spawner {
bool SwitchPlayerModel(const CatalogItem& item);
bool GiveWeapon(const CatalogItem& item);
bool SpawnVehicle(const CatalogItem& item, bool enterVehicle);
}

