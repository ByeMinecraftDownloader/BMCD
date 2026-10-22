# RAG 检索评测（0.7 实测）

**范围**：本文只证明 **0.7 检索后端**（语料→索引→混合排序）的历史指标，不证明产品级模组目录、
搜索或选择。RAG 产品路径已延后到 [`plan.md`](plan.md) 的 1.6“模组向量知识库”；当前 1.1
只做基础查表检索（名称/别名/分类，不加载向量模型）。本文数字不得当作 1.6 验收证据。

本文记录 **已经跑出来的数字**，不是目标值。所有命令都在 Linux / Qt 6.12 / onnxruntime 1.29
环境执行，模型为 `Xenova/multilingual-e5-small` int8（`model_quantized.onnx` 118 MB +
`tokenizer.json` 17 MB，位于 `$MC_MODEL_DIR`，默认 `~/.cache/BMCD/bmcd/models/e5-small`）。

## 1. 语料与索引

```
$ MC_MODEL_DIR=/tmp/model ./build/bmcd --rag-build --limit 100
indexed 100 documents, 231 chunks
embedder: multilingual-e5-small-int8
index: ~/.cache/BMCD/bmcd/rag/index.sqlite
```

| 项 | 值 |
|---|---|
| 语料来源 | Modrinth `/v2/search`（按下载量，mod + modpack 合并 facet）→ `/v2/projects` 取正文 |
| 项目 / 片段 | 100 / 231（每块 ≤1600 字符、重叠 240，前缀 `# <title>`） |
| 嵌入 | e5-small int8，384 维，L2 归一化；`passage: ` / `query: ` 前缀 |
| 索引体积 | 1.2 MB（SQLite + FTS5 + int8 向量） |
| 冷构建耗时 | 38.5 s（231 次嵌入，含 100 次网络取正文） |
| 纯 FTS 构建 | 2.7 s（无模型时的 hashing 降级路径） |

中文语料（`--rag-build --from-docs docs`，4 篇文档 / 20 片段）用于快速冒烟。

## 2. 评测方法

- 标注集：`tests/data/qrels-modrinth.json`（10 条中文查询）与 `tests/data/qrels-docs.json`（7 条）；
- 每条查询给出**项目级**相关集合；一个项目多个片段只计一次排名（`Eval.cpp:ndcg` 去重）；
- 指标：nDCG@10（二元相关）；基线为同索引上的 FTS5 `bm25()` 排序；
- 命令：

```
$ MC_MODEL_DIR=/tmp/model ./build/bmcd --rag-eval tests/data/qrels-modrinth.json
queries: 10 (10 有结果)
nDCG@10 fused: 0.5568
nDCG@10 bm25:  0.0920
```

## 3. 结果

| 语料 | 查询数 | nDCG@10 混合（FTS+向量+RRF+MMR） | nDCG@10 BM25 |
|---|---|---|---|
| Modrinth 100 项目 / 231 片段 | 10 | **0.5568** | 0.0920 |
| 本地 docs 4 篇 / 20 片段 | 7 | 1.0000 | 0.9885 |

- 通过 `docs/roadmap-1.0.md` 的 0.7 硬门槛：nDCG@10 ≥ 0.55，且远高于 BM25 基线
  （0.5568 vs 0.0920，6 倍）；docs 语料太小，两者都接近满分，只作链路冒烟；
- 延迟（同一进程内 10 条查询串行）：1.69 s 总耗时，含模型冷加载约 1.2 s → 稳态单查询约 50 ms，
  低于 300 ms 门槛；
- 全量 231 片段向量扫描是当前瓶颈（`Retriever::search` 会把候选 chunk 全读出来排序）；
  5 万级语料需要 ANN（roadmap §4 已列为 0.7+ 优化项）。

## 4. 已知局限（不粉饰）

1. **标注集小且由作者手工编写**：10 条查询覆盖意图型（「让游戏更流畅」「自动整理背包」）、
   中英混排（「光影 shader」）、黑话（「小地图」「机械动力」）；但不构成统计显著性。评测器
   已进 CI（`tst_rag` 覆盖 nDCG 手算与 qrels 解析），扩充标注只需往 JSON 里加条目；
2. **相关集合只收当前语料里的项目**：早期版本把语料外项目写进 qrels，nDCG 被压到 0.38；
   现在是「语料可达集合」上的相对排序质量，不代表全站召回；
3. **MMR 的相似度用 int8 余弦**，与 fp32 有 ~1e-3 偏差，对排序影响未单独测量；
4. **中文 FTS 仍弱**：查询靠 e5 向量兜底；同义词表（roadmap §4.3 的 200 条）尚未实现，
   中文意图查询的召回上限受此限制；
5. **索引增量更新未实现**：重建是整库重来（2.7–38.5 s），5 万项目时需要按 `updated` 做 delta。
6. **语料规模是 100 项目 / 231 片段**：roadmap 的 5 万项目目录未构建；不能把本评测外推为
   “5 万模组可检索”。
7. **产品链路未评测**：`RagController.suggest()` 只返回 `QStringList`，没有 project/file id、
   没有 loader/版本过滤；不存在“提示→候选→勾选→下载”的端到端用例。

## 5. 复现

```
# 1) 准备模型（一次性；也可让用户手动放进默认目录）
MC_MODEL_DIR=/tmp/model ./build/bmcd --fetch-model

# 2) 建索引 + 评测
cd /path/to/bmcd
MC_MODEL_DIR=/tmp/model ./build/bmcd --rag-build --limit 100
MC_MODEL_DIR=/tmp/model ./build/bmcd --rag-eval tests/data/qrels-modrinth.json
MC_MODEL_DIR=/tmp/model ./build/bmcd --ask "让游戏更流畅" --top 5

# 3) 单元/集成测试（无模型时 e5 用例自动 QSKIP）
ctest --test-dir build --output-on-failure
```
