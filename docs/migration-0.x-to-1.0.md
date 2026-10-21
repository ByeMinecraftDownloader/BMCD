# 从 0.x 迁移到 1.0

## 界面

- 1.0 使用 `qml2` 四视图界面；旧单文件界面已移除，`--ui` 选项不再存在。
- 键盘：`Ctrl+1..4` 切换视图，`/` 聚焦版本搜索，`Esc` 关闭对话框。
- 窗口 chrome 由 Qt 无边框实现：双击标题栏最大化，右上角最小化/最大化/关闭。

## 命令行

- 版本号从 `0.6.0` 升到 `1.0.0`；`bmcd --version` 输出 `bmcd 1.0.0`。
- 原有 `--plan/--download/--target/--tokens/--pack/--format/--to/--verify/--licenses/--rag-*`
  参数保持兼容。
- 新增：
  - `--import <file>`：导入整合包；加 `--to <dir>` 才会下载并部署。
  - `--instances` / `--instances-root <dir>`：查看发现的实例。
  - `--lang <en|zh_CN>`：切换界面语言；也可用 `BMCD_LANG`，默认跟随系统 locale。
  - `--i18n-check`：自检内置英文翻译。
  - `--benchmark`：输出首帧与 RSS。

## 数据与缓存

- 导入的整合包会落到 `~/.local/share/BMCD/bmcd/imports/<hash>/`，并写入 `import.json`
  快照；可用 `--pack <staging>/import.json` 继续部署或导出。
- RAG 索引与 0.7 相同路径；模型仍为可选下载，1.0 未改变格式。
- 部署仍生成 `~/.minecraft/.bmcd-backup-*` 备份目录；`--no-backup` 可关闭。

## 包格式

- 导出的 `.mrpack` 现在包含 `overrides/`（导入时的整合包内置文件会随导出保留）。
- 自包含包继续写入 `NOTICE` 与 `redistribution.json`，并受 `--licenses` / `--confirm` 闸门约束。
