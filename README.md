# Bye!MinecraftDownloader (BMCD) 1.0

**BMCD** 是 Qt 6 / Qt Quick 写的 Minecraft 资源下载与整合包工具：
从官方版本清单下载游戏文件，也能导入、校验、部署和导出 `.mrpack` / CurseForge / FTB 整合包。
界面按「点菜 → 烹饪 → 出餐 → 设置」组织，配色跟随桌面 matugen 色板。

| 点菜 | 烹饪 |
|---|---|
| ![点菜页](docs/preview-order.png) | ![烹饪页](docs/preview-cook.png) |
| **出餐** | **设置** |
| ![出餐页](docs/preview-serve.png) | ![设置页](docs/preview-settings.png) |

## 功能

- **下载**：官方版本清单、正式/快照筛选、并行下载（默认 6）、断点续传、SHA-1 校验、暂停/继续/取消；
  设置页可配置 Modrinth CDN、Mojang 镜像（如 BMCLAPI）、主源失败后的自动回退，以及临时下载目录。
- **整合包**：导入 `.mrpack` / CurseForge zip / FTB；部署到游戏目录（staging、备份、回滚）；
  导出引用式 `.mrpack` / FTB；自包含导出带许可闸门、NOTICE 与 `redistribution.json`。
- **界面**：Qt Quick 四视图（点菜 / 烹饪 / 出餐 / 设置），跟随系统色板，中英文切换。
- **检索**：1.0 只有版本号/类型搜索；1.1 目标为基础查表检索（名称/别名/分类，按 loader/版本过滤），不加载向量模型；RAG 向量知识库延后到 1.6，`--rag-*` 仅保留为实验后端。
- **发行**：Linux Arch 包、Debian 包、Windows 安装包（由版本 tag 触发的 GitHub Release 产出），以及 AppImage/Flatpak 辅助产物。

> 阶段顺序、能力边界、未交付项和验收证据只在
> [`docs/plan.md`](docs/plan.md) 维护；README 不重复状态。

## 安装与运行

正式发行物由 [`.github/workflows/release.yml`](.github/workflows/release.yml) 在推送
版本 tag（如 `v0.1.0`）时构建并发布到 GitHub Release：

- `bmcd-<version>-1-x86_64.pkg.tar.zst`（Arch Linux）
- `bmcd_<version>_amd64.deb`（Debian/Ubuntu，自包含 `/opt/bmcd`）
- `BMCD-Setup-<version>-x64.exe`（Windows 安装包，另附便携 zip）
- `SHA256SUMS`

### AppImage（Linux）

```bash
packaging/build-appimage.sh                 # 构建 build/BMCD-x86_64.AppImage
./build/BMCD-x86_64.AppImage                 # 发布基线为 Ubuntu 22.04 / glibc 2.35
```

AppImage 自带 Qt 运行库，不包含 RAG 实验模型；基础查表检索不依赖模型或索引。
构建机 glibc 高于 2.35 时脚本会拒绝，避免把开发机的 glibc 绑进产物。

### Flatpak

清单的源是固定 commit 的 `type: git`；构建脚本、bundle 和 sha256 说明见
[`packaging/README.md`](packaging/README.md)。

### Windows

在 Windows 构建目录旁运行 `pwsh packaging/build-windows.ps1`，产出
`dist/BMCD-windows-x64.zip` 与 `dist/SHA256SUMS.txt`；无证书时包内
`UNSIGNED.txt` 会明确标记未签名。

### 从源码构建

依赖：CMake ≥ 3.21、Ninja、Qt ≥ 6.5（Core/Gui/Quick/Qml/Network/Concurrent/Sql/Svg/Test）、
zlib（Linux/macOS 用系统包；Windows 没有系统 zlib 时 CMake 自动从源码拉取
v1.3.1 并静态链接）；RAG 实验后端可选 onnxruntime（1.6 再启用，当前基础检索不依赖）。

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

无显示环境可用 `QT_QPA_PLATFORM=offscreen`；golden 测试会自动以离屏方式跑一遍 qml2 界面。

## 命令行

无头子命令在创建窗口之前分流，适合脚本与 CI：

```bash
bmcd --version
bmcd --plan 26.3                     # 打印下载计划（不落地）
bmcd --download 26.3 --target DIR    # 下载并校验
bmcd --import pack.mrpack            # 只导入，打印摘要与 staging 目录
bmcd --import pack.mrpack --to DIR   # 导入 + 下载 + 部署
bmcd --pack pack.json --to DIR       # 部署既有清单（--dry-run 预检）
bmcd --pack pack.json --format mrpack --out out.mrpack
bmcd --pack pack.json --format ftb --out out.zip
bmcd --pack pack.json --format self --out self.mrpack --licenses --confirm @project:<id>
bmcd --pack pack.json --verify DIR
bmcd --instances [--instances-root DIR]
bmcd --fetch-model                    # 实验后端：下载可选 RAG 模型（约 135 MB，1.6 前不参与搜索）
bmcd --rag-build --from-docs docs      # 实验后端：构建索引
bmcd --ask "fabric 怎么装" --top 5     # 实验后端：文本提示
bmcd --i18n-check                    # 校验内置英文翻译
bmcd --benchmark                     # 首帧/RSS 性能数字（离屏）
```

开发/截图诊断：`--view order|cook|serve|settings`、`--size WxH`、`--dump-layout`；
它们不改变业务行为。`--help` 有完整选项说明。整合包导入支持：

- **Modrinth**：`modrinth.index.json`，读取 `files[]` 的 path/hashes/downloads/env 与 `overrides`/`client-overrides`；
- **CurseForge**：`manifest.json`，带 `url` 的文件直接使用；没有 URL 的文件在设置
  `CURSEFORGE_API_KEY` 后通过官方 API 解析（不设置则列入 `unresolved` 并跳过）；
- **FTB**：`manifest.json` + `overrides`。

## 数据位置

| 内容 | 路径 |
|---|---|
| 版本清单缓存 | `~/.cache/BMCD/bmcd/version_manifest_v2.json` |
| 下载/部署临时区 | 默认 `~/.cache/BMCD/bmcd/downloads/`；可在设置页指定父目录，实际使用其下 `BMCD/downloads/` |
| 整合包导入 staging | `~/.local/share/BMCD/bmcd/imports/<hash>/` |
| RAG 实验索引（1.6 前不参与搜索） | `~/.cache/BMCD/bmcd/rag/index.sqlite` |
| RAG 实验模型（可选） | `~/.cache/BMCD/bmcd/models/e5-small/` |
| 日志（设置 `MC_LOG`） | 指定文件 |

## 翻译

中文是源语言。新增/修改界面文案后用 Qt Linguist 工具刷新并编译：

```bash
/usr/lib/qt6/bin/lupdate qml2 -ts i18n/bmcd_en.ts
/usr/lib/qt6/bin/lrelease i18n/bmcd_en.ts
```

编译产物 `i18n/bmcd_en.qm` 随源码提交，并由 CMake 资源编进可执行文件，
因此各平台构建不依赖 Qt Linguist；`--i18n-check` 用 6 条探针验证翻译确实被加载。

## 文档

- 阶段、能力边界与验收证据：[docs/plan.md](docs/plan.md)
- 界面契约：[docs/ui-spec-1.0.md](docs/ui-spec-1.0.md)
- 从 0.x 升级：[docs/migration-0.x-to-1.0.md](docs/migration-0.x-to-1.0.md)
- 打包：[packaging/README.md](packaging/README.md)

## 许可证

WTFPL v2，见 [`LICENSE`](LICENSE)。整合包导出另有许可闸门：禁止再分发的文件默认不会进入自包含包。
