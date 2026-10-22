# BMCD 后续阶段规划（单一事实来源）

状态快照：2026-10-22。本文只记录**尚未完成的工作**；已完成的能力不再重复列入计划，
历史实现与验收证据以 Git 历史为准。README、设计档案、研究文档只引用本文，不自行声明完成度。

## 0. 规则

| 标记 | 含义 | 判定要求 |
|---|---|---|
| ✅ | 已完成并从计划中移除 | 有代码、有可复现命令/测试证据；不再占计划行 |
| ◐ | 部分实现，本轮继续 | 列出缺口与验收 |
| ○ | 未开始 | 排到后续；不得写成“支持” |

- 已完成项从表中删除，不保留“已完成清单”；计划只服务于下一步。
- 没有验收证据的，不得标 ✅；用户交互被判定不可用的，只能 ◐。
- 功能变更必须同步本表的范围/验收；新增完成项先补证据，再删计划行。

## 1. 阶段表

| 阶段 | 主题 | 交付范围 | 出口/验收 | 状态 |
|---|---|---|---|---|
| **1.3** | 出餐 | 部署到游戏文件夹、导出 mrpack、自包含包、许可闸门、覆盖前备份；复用已有 Deployer/Renderers | 见 §1.3；烹饪完成前拒绝出餐；许可不绕过 | ◐ |
| **1.4** | 安装链路与真实包验收 | Java 探测；Fabric/Quilt/Forge/NeoForge 安装器；profile 合并；真实整合包导入/部署/导出；自包含 A（仅包内容 + loader 库，需同版本原版）离线导入 | 每种格式 20 个真实包中 ≥19 成功；loader 安装后由外部启动器启动；离线机器在已装同版本原版的前提下导入/部署成功 | ○ |
| **1.5** | 发行补齐（Linux/Windows） | Flatpak、Windows 真机、代码签名、CI 打包恢复；macOS 发行暂缓 | Linux/Windows 干净机器安装/启动；产物签名与校验说明 | ◐ |
| **1.6** | 模组向量知识库（RAG，延后） | 模组实体知识库、多视角向量、QueryPlan、混合召回、重排、评测；设置页开关 | 见 §1.6；多说法查询的家族召回达标，不做 query 特判 | ○ |

**依赖**：1.3 主体完成，待验收；1.4 依赖 1.3 的导出链路；1.5 依赖 1.4 代码 freeze；
1.6 在 1.3–1.5 稳定后再启动，数据层可提前离线准备，但不进入 1.3 的搜索路径。

**明确范围外**：Mojang 账号登录、自建 mod 托管/CDN、内置 Java 运行时安装、壁纸取色、
macOS GUI 打磨。macOS dmg/签名/公证/发行本轮暂缓；macOS 编译与 CI 保留，但不作为 1.5 交付物。

## 1.3 出餐

- 复用 Deployer / Renderers / LicenseGate，重新设计点菜流程下的出餐交互。
- 只有 `cooked` 为真（烹饪完成）才允许出餐；出口：游戏文件夹、mrpack、自包含包。
- 版本页已下载完成的普通版本，出餐直接采纳 `VersionDetail.targetDir` 作为来源，
  版本 json 按磁盘原字节校验，不另建烹饪目录、不重复下载。
- 烹饪时的已缓存文件校验分批让出主线程，启动、暂停、继续和完成状态都实时反馈到进度，
  不出现“点了下载但进度不动”。
- 许可闸门在自包含包前显示，按父级模组聚合授权；未授权文件不得绕过。部署覆盖前自动备份，可回滚。
- 自包含采用 A 方案：只带整合包文件、overrides、可再分发的 loader 库与 NOTICE；
  不带 Mojang client jar、原版 libraries、assets。离线导入的前提是目标机已有同版本原版，
  不把“完整离线含本体”混入本出口。
- 验收：`ctest` 8/8；`tst_packcontroller` 覆盖未烹饪拒绝、下载→部署→导出全链路、
  已下载目录采纳与缺失计数、按父级模组聚合授权、缓存文件校验进度；
  四视图 × 880/1120/1920 × 中英文无 QML 错误；许可/缺失文案可见。
- 当前状态：已实现并自测，**待用户验收**；验收通过后从计划移除。

## 1.4 安装链路与真实包验收

按“Java → 加载器 → 烹饪会话 → 真实包/离线”四段推进，前一段没有可复现证据就不进入下一段。

### 1.4.1 Java 探测

- 新增 `JavaLocator`：`JAVA_HOME` → PATH → 平台常见 JDK 目录；用 `<java> -version`
  解析主版本，结果缓存；设置页可手动指定 `java` 可执行文件并持久化。
- 找不到满足版本要求的 Java 时，UI 明确提示并给出“选择 Java / 重新检测”，
  不自动下载 JRE（范围外），不把安装失败伪装成下载失败。
- 验收：存在/缺失/多版本三种场景都有单测；手动指定路径优先于自动探测。

### 1.4.2 加载器安装与 profile 合并

- 新增 `LoaderInstaller` 状态机：
  `idle → resolving → downloading → verifying → installing → merging → done/error`。
- 覆盖 Fabric、Quilt、Forge、NeoForge；只运行官方安装器，使用显式参数，
  不拼 shell；安装器 SHA 必须来自官方 meta，下载域名限制在允许列表。
- 新增 `ProfileMerger`：读写 `launcher_profiles.json`，保留未知字段，原子写入并备份；
  profile id 稳定（如 `bmcd-<mc>-<loader>-<ver>`），重复安装只更新 `lastVersionId`。
- 安装结果必须同时满足：`versions/<id>/<id>.json` 存在、loader libraries 完整、
  profile 可被外部启动器识别。
- 安装器输出写入 `~/.cache/BMCD/bmcd/logs/loader-*.log`；失败态可查看日志和重试当前步骤。

### 1.4.3 烹饪会话与 UI

- 新增 `CookSession`，统一拥有“本体 / 加载器 / 模组”三个子步骤，向 QML 暴露
  `state`、`progress`、`steps`、`currentStep`、`error`；不在 QML 里继续合并三套状态。
- 第一版串行：本体下载/校验 → 安装加载器 → 下载模组；稳定后再评估并行。
- 点菜页增加加载器/加载器版本选择与 Java 状态；模组需要非原版加载器时提示切换，
  不静默修改用户选择。
- 烹饪页显示三步状态、总进度、当前步骤日志入口；失败只重试当前步骤。
- 出餐页显示加载器徽标与 profile 名；加载器未安装时禁用“出菜到文件夹”并说明原因。
- 验收：四视图 × 880/1120/1920 × 中英文无 QML 错误；步骤状态不只靠颜色表达。

### 1.4.4 真实包与离线验收

- 语料：mrpack / CurseForge / FTB 各 20 个真实包，manifest 记录 URL、SHA、期望
  loader/版本；包体不进仓库，CI 下载到缓存。
- 每个包执行：导入 → 部署 → 导出 → 校验 profile/classpath；失败必须记录原因，
  不从分母剔除。
- 自包含 A 的离线验收：目标机已安装同版本原版，断网导入/部署，由外部启动器启动；
  不要求“无原版也能启动”。
- 验收：每种格式 ≥19/20 成功；loader 安装后由外部启动器进入主菜单；
  离线机器导入/部署成功。

### 1.4 验收汇总

| 编号 | 验收 | 证据 |
|---|---|---|
| I1 | Java 探测/手动指定/缺失提示可用 | `tst_javalocator` + 设置页截图 |
| I2 | 四种加载器安装、profile 合并且可重复执行 | `tst_loaderinstaller` + `launcher_profiles.json` fixture |
| I3 | 三种格式各 20 个真实包，≥19 成功 | `tests/pack-corpus/` 报告 |
| I4 | 自包含 A 在已装原版的离线机器导入/部署/启动 | 干净 VM 录屏/截图 + 命令输出 |
| I5 | 点菜/烹饪/出餐/设置四视图的加载器与 Java UI 在中英文/三尺寸无 QML 错误 | `ui_golden` / `ui_check` |

## 1.5 发行补齐（Linux/Windows）

macOS dmg/签名/公证本轮暂缓；macOS 编译与 CI 保留，不作为交付物。1.5 不先改 UI，
先让产物可重复、可校验、可在干净机器安装。

### 1.5.1 Linux

- 保留 AppImage，补 Flatpak：manifest 的 `type: dir` 改为固定 commit 的 `type: git`；
  在干净容器里 `flatpak-builder --no-install`，再 `flatpak run` 冒烟。
- AppImage 以 Ubuntu 22.04 为 glibc 基线重建，避免开发机 Arch 的 glibc 绑定。
- 验收：AppImage 与 Flatpak 都能 `--version`、`--i18n-check`、离屏启动；
  产物附 `SHA256SUMS`。

### 1.5.2 Windows

- `windeployqt` 便携包 + 可选安装器；补 VC runtime/Qt 插件检查。
- 干净 Windows 11 VM 测路径含空格/中文、长路径、代理、杀软；启动后完成一次
  版本清单读取和设置页打开。
- 有证书则 `signtool` 签名并验证；无证书则明确标记“未签名”，1.5 不判完成。

### 1.5.3 CI 与验收

- CI 产出 Linux AppImage/Flatpak、Windows zip/安装器；上传 `SHA256SUMS` 和构建日志。
- macOS job 只保留编译/单测/离屏冒烟，不产出 dmg，不进入 1.5 验收。
- 验收：Linux/Windows 干净机器安装/启动；签名产物通过系统校验；未签名项单独列出。

### 1.5 执行记录（2026-10-23）

- 已完成：AppImage 脚本的 glibc 2.35 门禁、payload symbol 检查、SHA256SUMS 和冒烟；
  Flatpak 清单改为固定 commit 的 `type: git`，构建脚本支持 `--no-install` /
  `--working-tree`；Windows 便携包脚本（windeployqt、VC runtime/Qt 插件检查、
  可选 signtool、SHA256SUMS.txt）；CI 三平台打包和 Flatpak job。
- 本机 AppImage 可用 `BMCD_ALLOW_NEW_GLIBC=1` 构建并通过 `--version`/`--i18n-check`/
  离屏冒烟，但开发机是 Arch/glibc 2.44，只能作为本地证据，不能当作 2.35 发布产物。
- 未完成：本机没有 KDE Flatpak runtime，Flatpak 真机构建/安装未跑；没有干净
  Windows 11 VM，L2 未跑；没有证书，签名状态只能是“未签名”。因此 L1/L2/L4
  仍未签收，1.5 保持 ◐，不放行发布。

### 1.5 验收汇总

| 编号 | 验收 | 证据 |
|---|---|---|
| L1 | Flatpak 干净安装/启动 | `flatpak-builder` 日志 + `flatpak run` 输出 |
| L2 | Windows 干净 VM 便携包/安装器启动 | VM 截图 + 启动日志 |
| L3 | AppImage/Flatpak/Windows 产物与 SHA256SUMS | CI artifact + 校验命令 |
| L4 | Windows 签名状态如实标注；Linux 产物可校验 | `signtool` 输出或“未签名”说明 + `SHA256SUMS` 校验 |

## 1.6 模组向量知识库（RAG，延后）

### 目标

让检索器先理解“小地图”是一个地图类意图，再在当前 loader/版本约束下找回可安装的模组；
不是给文档加一层向量搜索，也不是用固定 query 查表。

### 知识库实体

```text
mods
  project_id, slug, title, author, family_id
  aliases[]            // 小地图、Xaero's Minimap、Xaero 小地图
  categories[]         // map, minimap, performance, shader, ui
  summary, body
  loaders[], game_versions[], client_side, server_side
  downloads, follows, updated_at, license

mod_versions
  project_id, version_id, loaders[], game_versions[]
  dependencies[], conflicts[]

mod_relations
  from_project, to_project, kind(required|optional|conflict|alternative|same_family)
```

向量按语义视角拆：`name_view`（标题+别名）、`intent_view`（分类+功能+场景）、
`description_view`（长描述切块）。版本和文件不进向量，只做过滤。

### 查询与召回

1. `QueryPlan`：语言、意图、loader/版本/客户端约束、实体链接、别名扩展。
2. 先按约束过滤，再多视角召回；FTS + 向量 RRF 融合。
3. 项目去重、`family_id` 聚类；兼容性重排（resolver 可解析性/冲突）。
4. 可选 cross-encoder 只重排 top-k；CPU 延迟预算内才启用。

### 反过拟合

- 评测集放 `tests/eval/`，分 `dev` / `holdout`；holdout 开发期不读，发版轮换。
- 计划只写能力契约和阈值，不写“输入 X 必须返回 Y”的固定答案。
- 验收用同一意图的 ≥5 种中英文说法，按家族/意图桶看分布，不看单句。
- `src/` 禁止 query 特判；别名/分类放数据文件；`grep` 检查评测 query 不进代码。
- 指标：`Success@5`、`nDCG@10`、`Compatibility@10`、零结果率、p95；RAG 开/关同集对比。

### 验收

| 编号 | 验收 | 证据 |
|---|---|---|
| R1 | 同一意图 ≥5 种说法，top-5 至少命中目标家族一个项目；不兼容结果不进 top-5 | `tests/eval/holdout` + 分桶报告 |
| R2 | `Xaero`/`小地图`/`minimap`/`1.21 Fabric 小地图` 都能召回 Xaero 家族，且不依赖单句 | 多说法脚本 + 家族断言 |
| R3 | `src/` 无评测 query 特判；别名/分类只在数据文件 | `grep` + 评审清单 |
| R4 | RAG 开/关同集对比有稳定增益，否则不默认开 | 对比报告 |

## 2. 术语

| 术语 | 唯一定义 | 不要写成 |
|---|---|---|
| 点菜（Order） | 先选底包版本，再选模组、组成菜篮，并进入备菜 | “版本页”“选择下载内容” |
| 菜篮 | 当前选中的底包 + 模组集合，可增删但不下载 | “下载队列”“模组清单” |
| 备菜（Prep） | 点菜页内解析依赖/冲突的阶段，不是第四个导航 | “预检页”“RAG 分析” |
| 烹饪（Cook） | 按备菜结果下载并校验 | “队列页”“下载管理” |
| 出餐（Serve） | 部署到文件夹或导出整合包 | “导出页”“上菜”（历史叫法） |
| 基础查表 | 名称/别名/分类的精确、前缀、子串匹配；无向量、无模型 | “RAG”“语义搜索” |
| RAG / 向量知识库 | 1.6 的多视角语义召回与重排；当前不启用 | “基础检索”“查表” |
| 清单（Pack） | 一次点菜/备菜后解析出的文件计划 | “模组列表” |
| 出口 | 部署到文件夹 / 导出整合包 | “打包”（歧义） |

## 3. 文档职责

| 文档 | 角色 | 是否声明阶段/状态 |
|---|---|---|
| `docs/plan.md` | 唯一阶段表 + 验收 + 边界 | 是 |
| `README.md` | 产品入口：功能、安装、CLI、数据路径 | 否；只链接本文 |
| `docs/ui-spec-1.0.md` | 视觉契约；1.1–1.3 返工后同步点菜/烹饪/出餐 | 否；未实现项列 §9 |
| `docs/rag-eval.md` | 0.7 检索后端历史评测；不代表 1.6 产品 | 否 |
| `docs/roadmap-1.0.md` | 1.0 设计档案与历史决议 | 否 |
| `docs/migration-0.x-to-1.0.md` | 0.x 迁移操作 | 否 |
| `docs/research-mod-download.md` | M5 历史预调研 | 否 |
| `docs/windows-stability.md` | 0.4 历史审计 | 否 |
| `packaging/README.md` | 打包操作与限制 | 否 |

## 4. 口径修订记录

2026-10-22（1.4/1.5 重排）：

- macOS 发行（dmg/签名/公证）暂缓，1.5 只验收 Linux/Windows；macOS 编译/CI 保留。
- 自包含采用 A 方案：只带整合包内容/overrides/可再分发 loader 库与 NOTICE，不带
  Mojang client/libraries/assets；离线验收前提是目标机已有同版本原版。
- 1.4 增加 Java、LoaderInstaller、ProfileMerger、CookSession 四个子项；真实包语料
  每种格式 20 个，≥19 成功才算过。

2026-10-22（RAG 延后、基础查表先行）：

- 已实现能力从计划中删除，只保留未完成阶段。
- 1.1 的检索改为**基础查表**：本地别名/分类表 + Modrinth 名称/分类查询，无向量、无模型、
  无 RAG 开关；设置页本轮 4 个控件。
- RAG 移到 1.6：模组实体知识库、多视角向量、QueryPlan、家族聚类、反过拟合评测；
  1.3–1.5 稳定前不进入搜索路径。
- 评测集与计划分离：计划不写死 200 条 query，验收用多说法/家族桶；`src/` 禁止 query 特判。
