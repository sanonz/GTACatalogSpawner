# Catalog Spawner 人物、武器、载具、场景配置手册

本文说明如何配置 Catalog Spawner 的人物、武器、载具和场景菜单。

## 配置目录

插件从 GTA V 根目录下的 `CatalogSpawner` 文件夹读取配置：

```text
Grand Theft Auto V/
├─ CatalogSpawner.asi
└─ CatalogSpawner/
   ├─ peds.xml
   ├─ weapons.xml
   ├─ vehicles.xml
   ├─ scenes.xml
   ├─ Scenes/
   └─ Previews/
      ├─ noimage.png
      ├─ Peds/
      ├─ Weapons/
      ├─ Vehicles/
      └─ Scenes/
```

默认 Steam 安装位置示例：

```text
C:\Program Files (x86)\Steam\steamapps\common\Grand Theft Auto V\CatalogSpawner
```

四个目录 XML 文件建议保存为 UTF-8 编码。修改配置后，关闭菜单并按 `F6` 重新打开，插件会重新读取四个列表，无需重启游戏。

## 通用 XML 结构

每份文件由一个 `<catalog>` 根节点和若干个 `<item>` 节点组成：

```xml
<?xml version="1.0" encoding="UTF-8"?>
<catalog type="ped">
  <item>
    <displayName>菜单展示名称</displayName>
    <spawnName>实际调用名称</spawnName>
    <preview>Previews/Peds/example.png</preview>
    <category>分类</category>
    <description>说明文字</description>
    <info label="作者" value="Example" />
  </item>
</catalog>
```

通用字段：

| 字段 | 是否必填 | 说明 |
| --- | --- | --- |
| `displayName` | 是 | 菜单中显示的名称，可以使用中文。 |
| `spawnName` | 人物/武器/载具必填 | 传给 GTA V 的人物模型名、武器名或载具模型名；场景条目改用 `scenePath`。 |
| `preview` | 否 | 右侧预览图路径，建议使用相对于 `CatalogSpawner` 目录的路径。 |
| `category` | 否 | 右侧显示的分类。 |
| `description` | 否 | 右侧显示的简介。 |
| `info` | 否 | 自定义信息，可添加多行；`label` 和 `value` 均不能为空。 |

注意事项：

- `peds.xml`、`weapons.xml`、`vehicles.xml`、`scenes.xml` 的 `type` 应分别为 `ped`、`weapon`、`vehicle`、`scene`。
- 每个 `<item>` 必须直接位于 `<catalog>` 下，字段必须直接位于 `<item>` 下，不支持嵌套字段。
- 人物、武器、载具文件按 `spawnName` 去重，场景文件按 `scenePath` 去重，均不区分大小写。
- `displayName` 为空的条目会被忽略；普通条目还要求 `spawnName`，场景条目要求 `scenePath`。
- XML 特殊字符必须转义：`&` 写成 `&amp;`，`<` 写成 `&lt;`，`>` 写成 `&gt;`。
- 配置条目只负责列出资源，不会安装人物、武器或载具 MOD；相应资源必须已正确安装到游戏中。

## 人物配置：peds.xml

根节点类型必须为 `ped`。选择人物后，当前主角会切换成对应人物模型。

```xml
<?xml version="1.0" encoding="UTF-8"?>
<catalog type="ped">
  <item>
    <displayName>维托·黑手党</displayName>
    <spawnName>vito_mafia</spawnName>
    <preview>Previews/Peds/vito_mafia.png</preview>
    <category>男性</category>
    <description>Add-On 人物</description>
    <info label="作者" value="示例作者" />
  </item>

  <item>
    <displayName>超人</displayName>
    <spawnName>superman</spawnName>
    <preview>Previews/Peds/superman.jpg</preview>
    <category>男性</category>
    <description>超级英雄人物</description>
  </item>
</catalog>
```

如果人物由 AddonPeds 管理，可以在 `ap_m.xml` 中找到该人物的 `Name`，并将其原样填写到 `spawnName`：

```xml
<!-- ap_m.xml 中的名称 -->
<Name>vito_mafia</Name>

<!-- peds.xml 中对应填写 -->
<spawnName>vito_mafia</spawnName>
```

Catalog Spawner 当前不会直接读取或修改 `ap_m.xml`。如果选择后提示人物模型不存在，请先确认人物 MOD 已注册并能被游戏加载，同时检查 `spawnName` 拼写。

切换人物会替换玩家 Ped，并应用该模型的默认组件。生命值、护甲、武器和自定义服装不会被专门保留。

## 武器配置：weapons.xml

根节点类型必须为 `weapon`。选择武器后，当前主角会获得并立即装备该武器。

```xml
<?xml version="1.0" encoding="UTF-8"?>
<catalog type="weapon">
  <item>
    <displayName>手枪</displayName>
    <spawnName>WEAPON_PISTOL</spawnName>
    <preview>Previews/Weapons/pistol.png</preview>
    <category>手枪</category>
    <description>游戏原版半自动手枪</description>
    <ammo>120</ammo>
  </item>

  <item>
    <displayName>MG42</displayName>
    <spawnName>WEAPON_MG42</spawnName>
    <preview>Previews/Weapons/mg42.png</preview>
    <category>机枪</category>
    <description>Add-On 武器</description>
    <ammo>300</ammo>
    <info label="弹药类型" value="7.92 mm" />
  </item>
</catalog>
```

武器专用字段：

| 字段 | 是否必填 | 说明 |
| --- | --- | --- |
| `ammo` | 否 | 给予的弹药数量，必须是大于或等于 `0` 的整数；省略或无效时默认为 `300`。 |

原版武器名称通常使用 `WEAPON_` 前缀。Add-On 武器必须填写其 MOD 实际注册的武器名称。无效武器仍会显示在菜单中，但选择时会提示武器无效。

## 载具配置：vehicles.xml

根节点类型必须为 `vehicle`。选择载具后，插件会在玩家右侧生成对应载具；是否自动进入驾驶位由设置菜单中的“生成载具后自动进入”控制。

```xml
<?xml version="1.0" encoding="UTF-8"?>
<catalog type="vehicle">
  <item>
    <displayName>Adder</displayName>
    <spawnName>adder</spawnName>
    <preview>Previews/Vehicles/adder.png</preview>
    <category>超级跑车</category>
    <description>游戏原版超级跑车</description>
  </item>

  <item>
    <displayName>日产 GTR</displayName>
    <spawnName>gtr</spawnName>
    <preview>Previews/Vehicles/gtr.jpg</preview>
    <category>跑车</category>
    <description>Add-On 载具</description>
    <info label="品牌" value="Nissan" />
    <info label="年份" value="2024" />
  </item>
</catalog>
```

`spawnName` 必须是载具 MOD 定义的实际模型名称，而不是安装包名称、文件夹名称或菜单展示名称。无效载具仍会显示在菜单中，但选择时会提示模型不存在。

## 场景配置：scenes.xml

根节点类型必须为 `scene`。场景目录项使用 `scenePath` 代替 `spawnName`，选择后先进入加载确认页，再读取指定的本地 Menyoo Spooner XML。仅支持当前 Menyoo 的 `<SpoonerPlacements>` XML，不支持旧版 `.SP00N`。

```xml
<?xml version="1.0" encoding="UTF-8"?>
<catalog type="scene">
  <item>
    <displayName>屋顶聚会</displayName>
    <scenePath>Scenes/rooftop_party.xml</scenePath>
    <preview>Previews/Scenes/rooftop_party.png</preview>
    <category>聚会</category>
    <description>在屋顶摆放人物、载具和道具。</description>
    <info label="作者" value="Example" />
  </item>
</catalog>
```

场景字段：

| 字段 | 是否必填 | 说明 |
| --- | --- | --- |
| `displayName` | 是 | 场景在菜单中显示的名称。 |
| `scenePath` | 是 | Menyoo Spooner XML 路径；相对路径以 `CatalogSpawner` 目录为基准，也可使用绝对路径。 |
| `preview` | 否 | 场景预览图，建议放在 `Previews/Scenes`。 |
| `category` | 否 | 右侧显示的场景分类。 |
| `description` | 否 | 右侧显示的场景介绍。 |
| `info` | 否 | 与其他目录相同的自定义信息。 |

场景加载器支持：

- `Type=1` 人物、`Type=2` 载具、`Type=3` 物体及其当前 Menyoo 常用属性，包括外观、改装、状态、证明属性和实体 Attachment。
- Marker 及传送目标、Placement 循环 PTFX、Note、AudioFile。
- `IPLsToRemove`、`IPLsToLoad`、`InteriorsToEnable`、`InteriorsToCap`、`WeatherToSet`、`TimecycleModifier`、`ImgLoadingCoords` 和 `ReferenceCoords`。
- 当前 Menyoo TaskSequence 的全部类型编号 `0–54`，并读取 `Duration`、`KeepTaskRunningAfterTime` 与 `IsLoopedTask`。

如果某个模型、附件目标、PTFX 或音频加载失败，插件会继续加载其余内容，成功部分不会回滚；完成提示及“已加载场景”菜单会显示失败数量。

`ClearWorld`、`ClearDatabase` 和 `ClearMarkers` 属于破坏性指令。场景确认页会逐项列出，只有再次选择“确认并加载场景”才会执行。其中 `ClearDatabase`/`ClearMarkers` 只清理由 Catalog Spawner 跟踪的场景会话和 Marker；`ClearWorld` 会调用游戏世界清理原语，造成的删除无法在卸载时恢复。

可以同时加载多个场景。“已加载场景”菜单支持按场景卸载或全部卸载。卸载会尽力删除该会话记录的实体、任务、Blip、循环 PTFX、Marker 和音频，但不会恢复天气、Timecycle、IPL、Interior、玩家原位置或 `ClearWorld` 已删除的世界实体。多个场景修改全局状态时，以最后加载的场景为准；新场景开始播放音频时会停止上一个场景的音频。

`AudioFile` 使用相对路径时，插件依次在场景 XML 所在目录、`CatalogSpawner/Audio` 和 `menyooStuff/Audio` 中查找，因而可以复用 Menyoo 常见的音频目录结构。

## 预览图

推荐按类型存放预览图：

```text
CatalogSpawner/Previews/Peds/
CatalogSpawner/Previews/Weapons/
CatalogSpawner/Previews/Vehicles/
CatalogSpawner/Previews/Scenes/
```

配置示例：

```xml
<preview>Previews/Vehicles/gtr.png</preview>
```

- 支持识别 PNG、JPEG/JPG 和 WebP 图片尺寸，建议优先使用 PNG 或 JPG。
- 图片不存在或路径为空时，会使用 `Previews/noimage.png`。
- Windows 路径也可使用反斜杠，但推荐统一使用 `/`，便于阅读和迁移。
- 建议所有预览图使用相近的宽高比，例如 `16:9`。

## 自定义基础信息

可以在任意条目中添加多个 `<info />` 节点，它们会显示在右侧信息面板：

```xml
<info label="作者" value="Sanonz" />
<info label="版本" value="1.2" />
<info label="来源" value="自定义 MOD" />
```

`info` 必须使用属性形式，不能写成嵌套节点。

## 修改和排错

1. 保存 XML 文件。
2. 在游戏中关闭 Catalog Spawner 菜单。
3. 按 `F6` 重新打开菜单，触发配置重新加载。
4. 如果列表仍为空或选择项目失败，查看 GTA V 根目录下的：

```text
GTAVCatalogSpawner.log
```

正常加载日志示例：

```text
[Catalog] Loaded 2 item(s) from peds.xml
[Catalog] Loaded 2 item(s) from weapons.xml
[Catalog] Loaded 2 item(s) from vehicles.xml
[Catalog] Loaded 1 item(s) from scenes.xml
```

常见日志和处理方式：

| 日志内容 | 原因与处理方式 |
| --- | --- |
| `Failed to open` | 文件不存在、文件名错误，或文件正被其他程序独占。 |
| `Invalid catalog` | XML 结构或结束标签错误，使用 XML 编辑器检查格式。 |
| `Type ... doesn't match file` | 根节点 `type` 与文件类型不一致。 |
| `Ignoring item without displayName/spawnName` | 条目缺少必填字段。 |
| `Ignoring duplicate spawnName` | 同一文件中存在重复的实际调用名称。 |
| `Invalid ped model` | 人物模型未安装、未注册或 `spawnName` 错误。 |
| `Invalid weapon` | 武器名称无效或 Add-On 武器未加载。 |
| `Invalid vehicle model` | 载具模型未安装、未加载或 `spawnName` 错误。 |
| `[Scene] Invalid Spooner XML` | `scenePath` 指向的文件不是有效的 `<SpoonerPlacements>` XML。 |
| `[Scene] Model not found` | 场景引用的模型未安装或当前游戏版本无法加载。 |
| `Image not found` | `preview` 指向的图片不存在，会改用默认图。 |

如果某份配置在游戏已经成功加载过，之后修改成无效 XML，插件会保留该文件上一次成功加载的内存列表；首次加载即失败时，对应菜单为空。
