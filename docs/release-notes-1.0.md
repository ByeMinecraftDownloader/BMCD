# BMCD 1.0 发布说明

日期：2026-10-21。基线：0.4（M4）之后按 `docs/roadmap-1.0.md` 连续完成到 1.0。

## 交付内容

| 里程碑 | 状态 | 证据 |
|---|---|---|
| 0.5 平台层、色板 B、Windows P0、CI | 完成 | `src/platform/`、`--tokens`、`ctest` 5/5、平台守卫脚本 |
| 0.6 PackModel、上菜页、水波 | 完成 | `PackController`、`ServeView.qml`、`RippleCanvas.qml`、部署 staging/回滚 |
| 0.7 RAG v1 | 完成 | `docs/rag-eval.md`：nDCG@10 0.5568 vs BM25 0.0920 |
| 0.8 qml2 四视图、golden、Motion | 完成 | `qml2/`、`tests/ui_check.cpp`、1120×760 离屏 golden 通过 |
| 0.9 包生态 | 完成 | `.mrpack`/CurseForge/FTB 导入、实例发现、三种导出、许可闸门、NOTICE/`redistribution.json` |
| 1.0 发行 | 完成 | 版本 1.0.0、zh/en、性能预算、AppImage 重建、README/迁移/许可证 |

## 0.9 验收记录

- 真实 Modrinth 包 OptiMized 7.2.0（31 文件 / 33.3 MB + 2 个 override）：`--import --to` 成功，
  导入后 `--verify` 报告 33 present / 0 missing。
- 自包含导出：许可闸门对无许可/禁止再分发文件返回 denied 列表；`--licenses --confirm` 可显式放行，
  导出包含 `NOTICE` 与 `redistribution.json`。
- CurseForge：manifest 自带 `url` 时零配置可用；缺失 URL 时设置 `CURSEFORGE_API_KEY` 由官方 API 解析，
  无 key 则列入 `unresolved` 而不是静默丢包。
- 实例发现：默认根为 `.minecraft`、MultiMC、Prism（含 Flatpak 路径），支持 `--instances-root` 覆盖。

## 1.0 验收记录

- 翻译：82 条英文翻译，5 个 QML context；`--i18n-check` 探针通过，`--lang en` 启动英文界面。
- 性能（Linux/Release/offscreen）：首帧 123–138 ms、窗口创建 107–126 ms、RSS 70 MB，均在预算内。
- 打包：`dist/BMCD-x86_64.AppImage` 83 MB，`--version` 为 `bmcd 1.0.0`，离屏启动截图成功；
  CI matrix 另产出 `BMCD-windows-x64.zip`（windeployqt）与 `build/bmcd.dmg`（macdeployqt），均未签名。
- 测试：`ctest` 5/5（platform、theme、pack 19、rag、ui_golden），总耗时约 6.8 s。

## 与路线图的差异（明确记录）

- RAG 模型不内嵌：按 `docs/roadmap-1.0.md` 4.5 的“模型可选下载 + 索引按需”执行，AppImage 保持 83 MB；
  `bmcd --fetch-model` 下载约 130 MB 的 e5-small int8，`--rag-build` 建索引；无模型时 hashing 降级可用。
- v1 单文件界面已删除（符合 1.0 “删除 v1 回退”）；`--ui` 选项不存在。
- Flatpak 仅验证清单与构建脚本，本机缺少 `flatpak-builder` / KDE 6.9 runtime，未产出本地 bundle。
- Windows/macOS 由 CI matrix 构建；本地只在 Linux 做了构建、测试与运行验证。
