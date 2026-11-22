# GTAV Catalog Spawner

A ScriptHookV `.asi` plugin whose ped, weapon, vehicle, and scene menus are driven by
local XML catalogs. The menu uses GTAVMenuBase and is rendered with GTA V
natives.

## Features

- Five top-level menus: Peds, Weapons, Vehicles, Scenes, and Settings.
- Switch the current player to a configured ped model.
- Give the current player a configured weapon and ammo count.
- Spawn configured vehicles and optionally enter the driver seat.
- Load configured local Menyoo Spooner XML scenes, including current Placement
  properties, attachments, markers, task sequences, IPL/interior directives,
  weather/timecycle, PTFX, notes, and audio.
- Keep multiple scenes active and unload one scene session or all sessions.
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

## Convert preview PNGs

On Windows, the standalone `tools/ConvertTo-NonInterlacedPng.ps1` script converts
a static PNG to non-interlaced PNG using Windows PowerShell 5.1 or PowerShell 7.
No additional packages are required.

```powershell
.\tools\ConvertTo-NonInterlacedPng.ps1 -InputPath "C:\Images\preview.png"
.\tools\ConvertTo-NonInterlacedPng.ps1 "C:\Images\preview.png" "C:\Output\preview.png"
```

If the output path is omitted, the result is saved beside the input as
`preview.non-interlaced.png`. Missing output directories are created; existing
files are never overwritten. The script preserves dimensions, transparency,
and PNG metadata, and verifies every decoded pixel before writing. Animated or
signed PNGs, and pixel formats/palettes the Windows encoder cannot preserve,
are rejected rather than silently changed. Other image formats are not supported.

## Convert progressive JPEGs

`tools/ConvertTo-BaselineJpeg.ps1` converts an 8-bit progressive JPEG to baseline
JPEG without re-quantizing its DCT coefficients. Baseline input is also accepted.
It uses [libjpeg-turbo's jpegtran](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/doc/usage.txt#L557)
with `-copy all -optimize` to retain JPEG metadata without another lossy encode.

Download [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo/releases)
and put its `bin` directory on `PATH`, place `jpegtran.exe` and its accompanying
DLLs beside the script, or specify the executable explicitly:

```powershell
.\tools\ConvertTo-BaselineJpeg.ps1 "C:\Images\preview.jpg"
.\tools\ConvertTo-BaselineJpeg.ps1 "C:\Images\preview.jpg" "C:\Output\preview.jpeg"
.\tools\ConvertTo-BaselineJpeg.ps1 -InputPath "C:\Images\preview.jpg" -JpegtranPath "C:\Tools\libjpeg-turbo\bin\jpegtran.exe"
```

The default output is `preview.baseline.jpg` beside the input. Existing files
are never overwritten, missing output directories are created, and failed
conversions leave no temporary output. The script checks the output's baseline
SOF0 marker and unchanged dimensions/component count before saving. JPEGs that
cannot be represented as baseline without changing quantization, and non-8-bit
or arithmetic-coded JPEGs, are rejected. PowerShell 5.1 and 7 are supported;
the script does not download or install dependencies automatically.

## Catalog format

完整中文配置说明请参阅
[`CATALOG_CONFIGURATION.zh-CN.md`](CATALOG_CONFIGURATION.zh-CN.md)。

The plugin reads `peds.xml`, `weapons.xml`, `vehicles.xml`, and `scenes.xml`. Spawnable items must
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

Scene entries use `<scenePath>` instead of `<spawnName>`:

```xml
<item>
  <displayName>Example scene</displayName>
  <scenePath>Scenes/example.xml</scenePath>
  <preview>Previews/Scenes/example.png</preview>
  <description>Local Menyoo Spooner scene</description>
</item>
```

Selecting a scene opens a review screen before loading. `ClearWorld`,
`ClearDatabase`, and `ClearMarkers` are shown as destructive directives and
require an explicit confirmation. A failed model or resource does not roll the
whole scene back: successfully loaded parts remain active and the result reports
the failure count.

The loader accepts current Menyoo `<SpoonerPlacements>` XML only (not legacy
`.SP00N`). It handles Ped, Vehicle, and Prop properties, attachments, markers,
all current TaskSequence type IDs 0–54, IPL/interior directives,
weather/timecycle, placement PTFX, notes, and audio. The active-scenes submenu
can unload each scene independently.

Unloading is intentionally best effort. It removes the selected session's
tracked entities, task activity, blips, looping PTFX, markers, and audio, but it
does not restore global weather/timecycle, IPL/interior state, the player's
previous position, or entities removed by a world-clearing directive. When
several scenes alter global state, the most recently loaded value wins; starting
new scene audio stops previous scene audio.

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
