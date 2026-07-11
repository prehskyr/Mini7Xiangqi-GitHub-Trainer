# 七路象棋原生 NNUE：GitHub 托管版 v0.1.0

这套目录与旧的 `.m7nnue` 根节点重排训练器并存，但技术路线不同：

- `native/engine`：修改后的 Fairy-Stockfish 源码；
- 变体名：`mini7xiangqi`；
- NNUE 在搜索树内部执行；
- 导出文件由引擎通过 `EvalFile` 直接加载；
- 编译、数据生成、CPU 训练和 Windows 打包全部运行在 GitHub Actions；
- 本地电脑不承担训练计算。

## 已实现的规则

- 7×7 棋盘；
- 初始局面：`rcnkncr/p1ppp1p/7/7/7/P1PPP1P/RCNKNCR w - - 0 1`；
- 车、炮、马、将、兵的七路规则；
- 兵从开局起可前、左、右走，不可后退；
- 九宫与将帅照面；
- AXF 追逐规则；
- 同一方连续三个自己的回合使用车将军，该方立即判负。

## 四个工作流

### 1. Native Mini7 engine CI

自动编译 Linux 与 Windows 引擎，并验证：

- `mini7xiangqi` 已注册；
- 初始局面 `perft 1 = 19`；
- 第三次连续车将会判负；
- 引擎存在 `EvalFile` 外部网络接口。

### 2. Generate native Mini7 NNUE data

手动运行。它会：

1. 固定到官方 `variant-nnue-tools` 的指定 commit；
2. 注入相同的七路规则；
3. 用固定搜索深度生成训练集和验证集；
4. 压缩后保存到滚动 Release `mini7-native-data`；
5. 同时生成与七路变体对应的 `variant.h`、`variant.py`。

第一次测试建议：

```text
train_positions       = 5000
validation_positions  = 1000
search_depth          = 4
threads               = 4
hash_mb               = 1024
```

通过后，正式分片建议：

```text
train_positions       = 100000–500000
validation_positions  = 10000–50000
search_depth          = 5–7
```

### 3. Train native Mini7 NNUE on GitHub CPU

手动运行。它会：

1. 下载所有已发布的数据分片；
2. 固定到官方 `variant-nnue-pytorch` 的指定 commit；
3. 加入纯 PyTorch CPU 稀疏特征累加器；
4. 恢复上次 `latest.ckpt`；
5. 训练一轮；
6. 导出真正的 `mini7xiangqi-candidate.nnue`；
7. 编译原生引擎，并实际加载网络搜索；
8. 将 checkpoint 和网络保存到 Release `mini7-native-nnue-state`。

第一次测试建议：

```text
epoch_positions       = 10000
validation_positions  = 2000
batch_size             = 32
lambda_eval            = 0.80
```

正式 CPU 轮次建议从：

```text
epoch_positions       = 200000
validation_positions  = 20000
batch_size             = 64
lambda_eval            = 0.80
```

开始。每次再次运行时会自动恢复 `latest.ckpt`。

### 4. Package native Mini7 Windows engine

训练工作流至少成功一次后运行。输出：

```text
Mini7-Fairy-Stockfish-Windows-x64.zip
├─ Mini7-Fairy-Stockfish.exe
├─ mini7xiangqi-candidate.nnue
├─ BUILD_INFO.json
├─ LICENSE-GPLv3.txt
└─ README.txt
```

## 正确运行顺序

```text
Native Mini7 engine CI
        ↓
Generate native Mini7 NNUE data（先小规模）
        ↓
Train native Mini7 NNUE on GitHub CPU（先小规模）
        ↓
重复生成数据和训练
        ↓
Package native Mini7 Windows engine
```

## 当前边界

v0.1.0 已建立“原生引擎 → 原生数据 → CPU checkpoint → 标准 `.nnue` → 引擎加载验证”的链路。

尚未把候选网络自动晋级为正式网络。正式 Elo/SPRT 对战门禁将在候选网络成功产出后加入；在此之前，`candidate.nnue` 只能视为候选，不应仅凭训练损失认定棋力提高。
