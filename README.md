# GTAV Catalog Spawner

A ScriptHookV `.asi` plugin whose ped, weapon, and vehicle menus are driven by
local XML catalogs. The menu uses GTAVMenuBase and is rendered with GTA V
natives.

## Features

- Four top-level menus: Peds, Weapons, Vehicles, and Settings.
- Switch the current player to a configured ped model.
- Give the current player a configured weapon and ammo count.
- Spawn configured vehicles and optionally enter the driver seat.
- Right-side preview image and basic information for every item.
- Runtime language selection with English fallback.
- Localizable footer branding through the `footer.brand` language key.
- Catalogs are reloaded whenever the menu is opened.

## Build

Open `CatalogSpawner.sln` in Visual Studio 2022 and build `Release|x64`, or run:

```powershell
msbuild CatalogSpawner.sln /m /p:Configuration=Release /p:Platform=x64
```

The output is `CatalogSpawner/bin/CatalogSpawner.asi`.

## Install

Copy these into the GTA V directory:

```text
CatalogSpawner.asi
CatalogSpawner/
```

The prepared data directory is under `stage/CatalogSpawner`. Press F6 or enter
the `catalogspawner` cheat to open the menu. ScriptHookV is required.

## Catalog format

完整中文配置说明请参阅
[`CATALOG_CONFIGURATION.zh-CN.md`](CATALOG_CONFIGURATION.zh-CN.md)。

The plugin reads `peds.xml`, `weapons.xml`, and `vehicles.xml`. Each item must
contain a menu `displayName` and the actual game `spawnName`:

```xml
<item>
  <displayName>Menu name</displayName>
  <spawnName>actual_model_or_weapon_name</spawnName>
  <preview>Previews/Peds/example.png</preview>
  <category>Category</category>
  <description>Description</description>
  <info label="Author" value="Example" />
</item>
```

Weapon entries may also include `<ammo>300</ammo>`. Preview paths are relative
to the `CatalogSpawner` data directory. PNG, JPEG, and WebP dimensions are
recognized; a missing preview falls back to `Previews/noimage.png`.

Malformed catalog files do not replace the last successfully loaded in-memory
list. Invalid game models remain visible but produce an actionable subtitle and
log entry when selected.

## Known behavior

Changing player models replaces the player Ped. The plugin reacquires the new
Ped handle, applies the model's default component variation, and restores the
driver seat when possible. Health, armor, weapons, and custom clothing are not
explicitly preserved across model changes.
