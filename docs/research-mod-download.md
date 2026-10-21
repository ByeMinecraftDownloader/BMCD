# M5 预调研：MOD / 整合包下载

调研日期 2026-10-21。目标：给 M5「集成 MOD 与整合包下载」选路线、定格式、找坑。
**所有结论都用 curl 实测过**（本机可直连 Modrinth / CF CDN / FTB / BMCLAPI，Mojang 同样可达）；
唯一没实测的是 packwiz 的 pack.toml 细节（文档页 404），已在文中标注。

---

## 0. TL;DR

| 源 | 认证 | 能否给出完整可下载清单 | 结论 |
|---|---|---|---|
| **Modrinth** | 只读不需要，公开 REST | ✅ `.mrpack` 里直接带每个文件的 URL + sha1 + 大小 | **首选**，M5.1 |
| **FTB** | 不需要 | ✅ 版本 JSON 直接给 1483 个文件（url+sha1+size+可选标记） | **次选**，几乎零成本复用 TaskPlan，M5.3 |
| **CurseForge** | ❌ 必须 API key（403 实测） | ⚠️ 整合包只给 `projectID/fileID`，要二次查询拿 URL | 最后做，且要过条款/授权，M5.4 |
| **packwiz** | 不需要（git/raw） | ⚠️ pack.toml 只给元数据，每个 mod 是独立的 `.pw.toml` | 不做；要读时按 CF/Modrinth 元数据反推 |

一句话：**M5 先做 Modrinth `.mrpack`（自带全部 URL 与 hash，零依赖解析）→ 再做 FTB（文件清单现成）→
CurseForge 因 API key 与条款问题放最后，且只做「已拿到 manifest.json 的包」这一条降级路径。**

---

## 1. 三种整合包容器格式（实测解剖）

### 1.1 Modrinth `.mrpack`（formatVersion 1）

实测样本：Fabulously Optimized 15.0.0-beta.1（174 KB，49 个 mod）。结构：

```
modrinth.index.json
overrides/…            # 直接覆盖到实例目录的文件树（允许 client-overrides/、server-overrides/）
```

`modrinth.index.json` 实测字段：

```json
{
  "formatVersion": 1,
  "game": "minecraft",
  "versionId": "15.0.0-beta.1",
  "name": "Fabulously Optimized",
  "dependencies": { "fabric-loader": "0.19.5", "minecraft": "26.3" },
  "files": [
    {
      "path": "mods/BetterGrassify-1.8.8+fabric.26.3.jar",
      "hashes": { "sha1": "626f74d3…", "sha512": "14cd361b…" },
      "env": { "client": "required", "server": "required" },
      "downloads": ["https://cdn.modrinth.com/data/m5T5xmUy/versions/8qcmuwZa/BetterGrassify-…jar"]
    }
  ]
}
```

要点：

- **不含 Minecraft 本体**，本体与 loader 由 `dependencies` 声明（`minecraft`、`fabric-loader`/`quilt-loader`/
  `forge`/`neoforge`），M5 需要自己把 loader 装好。
- `files[].downloads[]` 是**直链**（实测全部指向 `cdn.modrinth.com`），下载不需要任何 API 调用。
- **没有 `size` 字段**：要么下载时用 `Content-Length`，要么在进度条里接受阶段不确定；建议 GET 后取
  `Content-Length`（实测 CDN 返回了正确长度），缺失则先跳过体积统计。
- 有 `sha1` 与 `sha512`，正好命中 `DownloadTask` 现有的 sha1 校验。
- `env` 三态：`required` / `optional` / `unsupported`。`unsupported` 的 client 文件不要装。
- 文件路径一律 POSIX 风格、相对实例根；`overrides/` 里可能有任意目录（config、resourcepacks、shaderpacks）。

### 1.2 CurseForge 整合包 zip

实测样本：All the Mods 9 1.1.1（50 MB，1362 个条目，最大的一档真实规模）：

```
manifest.json
modlist.html
overrides/…
```

`manifest.json` 实测：

```json
{
  "minecraft": { "version": "1.20.1", "modLoaders": [{ "id": "forge-47.4.0", "primary": true }] },
  "manifestType": "minecraftModpack",
  "manifestVersion": 1,
  "name": "All the Mods 9", "version": "1.1.1", "author": "ATMTeam",
  "files": [ { "projectID": 351491, "fileID": 6366217, "required": true } ],
  "overrides": "overrides"
}
```

要点：

- `files` 里**只有 projectID/fileID**，没有 URL、没有 hash、没有文件名 —— 必须有 CurseForge API key
  才能转成下载任务（`GET /v1/mods/{projectID}/files/{fileID}` 或批量 `/v1/mods/{id}/files` 拿
  `downloadUrl`/`hashes`/`fileName`/`fileLength`）。
- CDN 本身是公开的：`https://mediafilez.forgecdn.net/files/{fileID/1000}/{fileID%1000}/{fileName}`
  实测 200（`edge.forgecdn.net` 同样可用）。**难点是 fileID → fileName 的映射，而不是下载本身。**
- 新包（manifestVersion 2）会多 `overrides` 之外的字段，但同样只有 ID 对；逻辑一致。
- 老包里 `overrides` 字段可能缺省，默认目录名就是 `overrides`。

### 1.3 FTB 原生包（API 直接给全清单）

实测：FTB Academy 1.12 v1.1.0 → `files` 1483 个（config 988 / resource 374 / mod 116 / script 5），
每个文件就是：

```json
{ "path": "./animation/08.jpg", "url": "https://dist.modpacks.ch/modpacks/0/…/animation/08.jpg",
  "mirrors": [], "sha1": "78c25d57…", "size": 18988,
  "clientonly": true, "serveronly": false, "optional": false }
```

配套 `targets` 直接给 `forge 14.23.5.2846` / `minecraft 1.12.2` / `java 8.0.312+7`。

**这是和 BMCD 现有 `TaskPlan` 最贴合的数据源**：path/url/sha1/size 全齐，甚至可以完全跳过解析步骤，
直接把 `files[]` 映射成 `PlanFile{path,url,sha1,size,kind}`。代价是 FTB 包量少（18877 个 Modrinth
整合包 vs FTB 两位数量级）且只覆盖 FTB 自己的分发。

---

## 2. 数据源实测细节（可复制的 curl）

### 2.1 Modrinth

```bash
# 搜索整合包（facets 必须 JSON 编码，index=downloads 按下载排序）
curl "https://api.modrinth.com/v2/search?limit=3&index=downloads&facets=%5B%5B%22project_type%3Amodpack%22%5D%5D"
# → total_hits=18877；fabulously-optimized(1KVo5zza, 18,994,206)/zombie-invade-100-days(l9m9tuPN)/cobblemon-fabric(5FFgwNNP)

# 某包的版本列表（可分页，477 个版本）
curl "https://api.modrinth.com/v2/project/fabulously-optimized/version"
# 版本对象含 id/name/version_number/loaders/game_versions/date_published/files[]
# files[] = {url, filename, size, primary, hashes{sha1,sha512}}

# 按游戏版本 + loader 过滤
curl "https://api.modrinth.com/v2/project/sodium/version?game_versions=%5B%2226.3%22%5D&loaders=%5B%22neoforge%22%5D"
# → 3 个版本；dependencies 字段存在（本次样本为空数组；非空时形如 {version_id,project_id,file_name,dependency_type}）
```

- 不需要 token，但**请带 UA**（本项目 `DownloadTask` 一直发 `BMCD/0.3`；Modrinth 官方要求可识别的 UA）。
- 速率限制公开为 300 req/min，M5 做列表页时一次取一页、缓存 10 分钟足够。
- 搜索还能按 `categories`（fabric/neoforge/quilt/forge）、`versions`（游戏版本）过滤，UI 上就是现成的筛选器。

### 2.2 CurseForge

```bash
curl -s "https://api.curseforge.com/v1/mods/search?gameId=432&classId=4471&pageSize=1"
# → HTTP 403 "Forbidden: API Key missing or invalid"
```

- 官方 API **必须** `x-api-key` 头，且 key 需要向 Overwolf 申请、有独立条款（限制第三方分发 mod 文件、
  要求缓存策略、禁止转售）。这是 M5 里唯一需要「外部审批」的一步。
- 没有 key 时能做的降级只有「用户已从官网下好 zip → 拖进来导入」，此时不需要任何 API；
  但 `manifest.json` 里的 ID 对仍然无法解析 → 只能装 `overrides/` 部分，mods 需要联网补，
  体验会有缺口。
- 第三方只读接口 `api.cfwidget.com/minecraft/modpacks/{slug}` 实测可用（返回 153 个 file 的
  id/name/version/filesize），但它是非官方、有限流、随时可能变，只可作为「诊断/兜底」，
  不能写进产品主路径。

### 2.3 FTB

```bash
curl "https://api.modpacks.ch/public/modpack/search/8?term=academy"   # 路径段是 limit
curl "https://api.modpacks.ch/public/modpack/1"                       # 含 versions[] 与 targets[]
curl "https://api.modpacks.ch/public/modpack/1/2"                     # 版本 2：完整 files[]（1483 个）
```

- 无认证，但接口语义偏「launcher 内部 API」；`/public/modpack/1/version` 这种写法会返回
  `{"status":"error"}`，正确路径是 `/public/modpack/{id}/{versionId}`。
- `path` 会带 `./` 前缀（`"./animation/08.jpg"`），拼路径前要规范化。
- 有 `clientonly` / `serveronly` / `optional`，正好对应 `env` 语义。

### 2.4 packwiz（不做，但要知道长什么样）

`pack.toml`（元数据）+ `index.toml`（索引）+ 每个 mod 一个 `mods/<name>.pw.toml`，里面是
`[download] mode = "metadata:curseforge"` / `"modrinth"` + project/file id。也就是说它把「解析」这一步
外包给了别人的 API，自己只存 ID 对；对 BMCD 来说等于绕回 CurseForge/Modrinth 两条路，没有独立价值。
（本次 `packwiz.infra.link/reference/pack-format/` 返回 404，细节按文档口径记录，未实测。）

---

## 3. 与现有架构的映射（关键结论：可以复用 80%）

现有 `TaskPlan`/`PlanFile`（`src/core/Plan.h`）与三种格式的对齐情况：

| 字段 | `.mrpack` | FTB | CF manifest |
|---|---|---|---|
| `path` | `files[].path` | `files[].path`（去 `./`） | ❌ 需 API 查 fileName |
| `url` | `files[].downloads[0]` | `files[].url` | ❌ 需 API 查 downloadUrl |
| `sha1` | `files[].hashes.sha1` | `files[].sha1` | ❌ 需 API 查 hashes |
| `size` | 缺失（用 Content-Length） | `files[].size` | ❌ 需 API 查 fileLength |
| `kind` | `mod` / `override` | `mod` / `config` / `resource` | `mod` / `override` |

需要给 `PlanFile` 补两个字段才能表达整合包语义：

1. `optional: bool` —— FTB 的 `optional`、mrpack 的 `env.client == "optional"`；
   失败**不应**让整个任务 `failed`。
2. `env: {client, server}` 或简化为 `clientOnly: bool` —— 决定是否跳过。

`DownloadTask` 目前失败即计入 `m_failures`，最后 `finishIfIdle()` 直接 `emit failed`（`DownloadTask.cpp:370-373`）。
整合包场景需要「可跳过的软失败」：加 `PlanFile.optional`，`failOne` 里判断并只记 warning。

另外 `DownloadTask::absolutePath()`（`DownloadTask.cpp:37-40`）用 `m_root + "/" + file.path` 拼；
整合包会带 `overrides/` 解压，建议统一走 `QDir(root).filePath(path)` 并按 `/` 分段校验。

---

## 4. 整合包安装流水线（建议实现顺序）

```
导入（拖拽 / 选择文件 / 从 Modrinth 搜索直接装）
  ├─ 识别：zip 里有 modrinth.index.json → mrpack
  │        zip 里有 manifest.json (manifestType=minecraftModpack) → CF 包
  │        其它 zip → 未知包（可提示"按普通 zip 解压？"）
  ├─ 读清单 → 得到 { minecraftVersion, loaderId, files[] }
  ├─ 1) 先保证实例可启动：复用 M2/M5 的"版本 + loader"安装（见 §5）
  ├─ 2) 再下 mods：TaskPlan(optional 语义) → 现有下载管线
  └─ 3) 最后解压 overrides/（一次性、可覆盖、带路径安全）
```

**路径安全（必做，Windows 上尤其致命）**：解压前对每个 entry 做
`QDir::cleanPath` + 拒绝绝对路径 / 拒绝 `..` / 拒绝盘符 / 拒绝反斜杠 / 拒绝 Windows 保留名
（`CON`、`NUL`、`AUX`、`COM1`…）、拒绝大小写冲突（`mods/A.jar` 与 `mods/a.jar` 在 NTFS 上是同一文件）。
zip 里出现 `\` 分隔符是常见现象，必须先归一化成 `/`。

**其他落地细节**：

- `.mrpack` 的 `overrides/` 直接铺；`client-overrides/` 只在客户端装；`server-overrides/` 忽略。
- 覆盖策略：同名文件默认「包内优先」，但要在 UI 提示覆盖了多少个文件（防止吃掉用户自定义配置）。
- 磁盘预检：整合包动辄 1–2 GB，落地前用 `QStorageInfo` 检查剩余空间。
- 并发：mods 来自同一 CDN，`QNAM` 每主机 6 连接已是上限，默认并发 6 即可，不要提到 12。

---

## 5. Loader 安装（整合包能不能跑起来的真正门槛）

M5 的「继承版本」正好是这一步的地基。各 loader 的实测入口：

| Loader | 入口 | 实测结果 | 装法 |
|---|---|---|---|
| Fabric | `GET https://meta.fabricmc.net/v2/versions/loader/{mc}/{loader}/profile/json` | 200，2857 B，**完整 version json**：`id=fabric-loader-0.16.9-1.20.1`、`inheritsFrom=1.20.1`、8 个 libraries、`mainClass=net.fabricmc.loader.impl.launch.knot.KnotClient` | 与本体 version json 合并（父版本 `inheritsFrom`）即可，**不需要跑 installer** |
| Quilt | `GET https://meta.quiltmc.org/v3/versions/loader/{mc}` | 200（892 KB 列表；同样有 profile json 接口） | 同 Fabric |
| NeoForge | `https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml` | 200，`<latest>26.3.0.95</latest>` | 需要跑 `installer.jar --installClient`（或自行解析 installer 的 processors，不建议） |
| Forge | `https://maven.minecraftforge.net/net/minecraftforge/forge/maven-metadata.xml` | 200，`<latest>26.3-66.0.13</latest>`；`files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json` 200（`"1.20.1-latest"` 这类键） | 同 NeoForge（installer 子进程） |

结论与建议：

- **Fabric/Quilt 走「profile json 合并」**，纯 JSON，零子进程，与 BMCD 现在「只下载不执行」的设计一致。
- **Forge/NeoForge 必须起 JVM**（installer 会改 `libraries/`、写 `versions/<id>/<id>.json`）。
  这引入新风险：需要探测 Java、子进程输出解析、失败回滚。建议做成 M5.5 独立一件事，
  并且**默认不阻塞**整合包导入（先装 Fabric 类包，Forge 类包给出「需要 Java 才能安装」的明确提示）。
- Java 要求：26.3 版本 json 的 `javaVersion.majorVersion` 是 **25**；Forge/NeoForge installer 自身
  也要同级别 JVM（NeoForge 26.3 的 installer 用 Java 21+ 起，具体以 installer 内 `MANIFEST` 为准，需实测）。

---

## 6. 中国网络与镜像（实测）

- **BMCLAPI 可用**：`https://bmclapi2.bangbang93.com/version/{id}/json` → 200（34,974 B，完整版本 json）；
  `https://bmclapi2.bangbang93.com/mc/game/version_manifest_v2.json` → 200（277,821 B，HEAD 可探测）。
  也就是说「清单 + 版本 json」的镜像路径是通的，M5 可以做一个 `MC_MIRROR=bmclapi` 开关。
- 资源/库镜像的路径约定（BMCLAPI 文档口径，**本次未逐条实测**）：`/assets/{hash前2}/{hash}`、`/maven/…`、
  `/libraries/…`。做开关前建议先单测 3 个真实 URL。
- Modrinth CDN / CF CDN / FTB CDN 在本机都能直连（本次全部 200），国内实际速度差异大，
  但**这些是纯 CDN**，加镜像意义有限；真正需要镜像的是 Mojang 的 `piston-*` 与 `resources.*`。
- HMCL 门控的本质是「按地区禁掉游戏本体下载」，所以 BMCD 只要保证**本体走镜像也 200**，就已经解决
  主人最初的痛点；MOD 下载是锦上添花。

---

## 7. 分阶段计划与验收

| 阶段 | 范围 | 验收（可脚本化） |
|---|---|---|
| **M5.1** | Modrinth 搜索 + `.mrpack` 导入（仅下载 + overrides 解压，不装 loader） | `--import-pack <file.mrpack> --target build/pack-test` → 49/49 文件 sha1 通过，overrides 完整；重复执行 0 downloaded |
| **M5.2** | `PlanFile.optional` + env 过滤 + 路径安全 + 磁盘预检 | 注入一个 URL 404 的 optional 文件 → 任务仍 `finished`，日志 1 条 warning |
| **M5.3** | FTB 源（搜索 + 版本 → 直接 TaskPlan） | `--ftb-pack 1 --ftb-version 2` 与官方 launcher 文件数一致（1483） |
| **M5.4** | Fabric/Quilt loader 安装（profile json 合并） | 装完 `versions/fabric-loader-…/` 存在且 JSON 可被 BMCD 自己解析；`--plan` 能列出合并后的文件 |
| **M5.5** | CurseForge（需 key） + Forge/NeoForge installer | 仅在拿到 key 后启动；installer 子进程失败要能回滚 `versions/<id>` |

---

## 8. 未验证 / 风险清单

1. **CurseForge 条款**：申请 key 的资格、是否允许 BMCD 这种「下载器」形态、缓存要求 —— 需要主人去申请时确认，
   我没法替判断。
2. `.mrpack` 的 `size` 缺失 → 进度条总量估算不准；建议 M5.1 先接受「文件数进度」为主、体积进度为辅。
3. FTB API 无文档承诺（launcher 内部接口），字段可能变；建议加「响应形状校验 + 明确报错」。
4. Forge/NeoForge installer 需要 JVM：M5 之前 BMCD 完全没有「执行外部程序」的能力（全项目无 `QProcess`，
   本次 grep 确认），这是新增的攻击面，必须限定 installer 来源（官方 maven）+ 校验 sha256。
5. 覆盖删除：升级整合包时 `overrides/` 里被删掉的文件不会自动删，建议 M6 再考虑「清单 diff」。
6. 大小写不敏感文件系统（Windows/macOS）上 `mods/A.jar` 与 `mods/a.jar` 会互相覆盖，解压要做冲突检测。
