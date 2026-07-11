# 七路象棋 GitHub Actions 无界面训练器

这个仓库把七路象棋自对弈训练改造成了 **GitHub Actions 可直接运行的命令行项目**。代码推到公开仓库后，只需要登录 GitHub 网页，在 **Actions** 页面点击一次按钮，即可在标准 Windows runner 上训练约 5 小时 40 分钟。

每次正常结束时会：

1. 保存 `models/mini7-current.m7nnue`；
2. 上传模型、棋局日志和运行摘要为 Workflow Artifact；
3. 把最新模型写入 `training-state` 分支；
4. 下次运行自动从 `training-state` 恢复并续训。

> 这里生成的是七路象棋客户端使用的 `.m7nnue`，不是 Fairy-Stockfish 原生 `EvalFile=.nnue`。它参与根节点 MultiPV 候选重排，不会进入 Fairy-Stockfish 的每个搜索节点。

---

## 一、第一次上传到 GitHub

### 方法 A：Git 命令行

先在 GitHub 新建一个 **Public** 空仓库，不要勾选自动生成 README，然后在本项目解压目录执行：

```bash
git init
git add .
git commit -m "Initial public Mini7 trainer"
git branch -M main
git remote add origin https://github.com/你的用户名/你的仓库名.git
git push -u origin main
```

### 方法 B：GitHub Desktop

1. 解压本项目；
2. GitHub Desktop 选择 **Add an Existing Repository from your Local Drive**；
3. 选择解压后的目录；
4. 提交全部文件；
5. 点击 **Publish repository**；
6. 不要勾选 Keep this code private。

不要只把 ZIP 当成单个文件上传。GitHub Actions 必须看到 `.github/workflows/train-6h.yml` 等真实目录结构。

---

## 二、允许工作流保存检查点

通常 `permissions: contents: write` 已足够。若最后一步提示无法 push：

1. 打开仓库 **Settings**；
2. 左侧进入 **Actions → General**；
3. 找到 **Workflow permissions**；
4. 选择 **Read and write permissions**；
5. 保存。

该权限只用于让 GitHub 自己的 `GITHUB_TOKEN` 更新 `training-state` 分支，不需要创建私人令牌，也不要把 PAT 写进仓库。

---

## 三、网页端启动训练

1. 打开仓库；
2. 点击顶部 **Actions**；
3. 左侧选择 **Train Mini7 for a 6-hour window**；
4. 点击右侧 **Run workflow**；
5. 保持默认参数；
6. 再点击绿色 **Run workflow**。

推荐默认值：

| 参数 | 默认值 | 含义 |
|---|---:|---|
| `duration_minutes` | 340 | 实际训练 5 小时 40 分钟，给编译和保存留出余量 |
| `threads` | 4 | Windows 标准 runner 的 4 个 vCPU |
| `hash_mb` | 1024 | 仙鱼 Hash |
| `movetime_ms` | 120 | 每手教师搜索时间 |
| `max_plies` | 240 | 每局最大半回合数 |
| `multipv` | 5 | 仙鱼候选数 |
| `learning_rate` | 0.002 | 客户端网络学习率 |
| `net_blend_percent` | 20 | 当前网络参与自对弈选着的权重 |

GitHub-hosted 单个 job 的硬限制是 6 小时，因此不能把训练本体设成整整 360 分钟。工作流将输入限制在最多 345 分钟，并默认使用 340 分钟，确保模型能在硬终止前正常保存。

不要同时启动多个训练任务。工作流设置了同一并发组，后启动的任务会排队，避免两个 runner 同时覆盖同一个检查点。

---

## 四、训练结束后在哪里拿模型

### 方式 1：Artifact

1. 打开本次 Workflow run；
2. 页面底部找到 **Artifacts**；
3. 下载 `mini7-training-run-数字`；
4. 解压后模型位于：

```text
models/mini7-current.m7nnue
```

Artifact 还包含：

```text
logs/games.jsonl
state/latest.json
```

### 方式 2：`training-state` 分支

第一次成功训练后，仓库会自动出现 `training-state` 分支，里面长期保存：

```text
models/mini7-current.m7nnue
state/latest.json
state/history.md
```

下一次从网页再次运行时，工作流会自动读取该模型继续训练。

---

## 五、把 GitHub 训练结果带回桌面客户端

可以在客户端训练窗口选择 **导入模型**，导入下载的：

```text
mini7-current.m7nnue
```

也可以关闭客户端后，将它覆盖到：

```text
%LOCALAPPDATA%\Mini7Xiangqi\training\mini7-current.m7nnue
```

重新启动客户端后会自动加载。

---

## 六、项目的实际训练流程

命令行训练器执行：

```text
Fairy-Stockfish MultiPV 教师搜索
              ↓
七路象棋规则层过滤立即判负着法
              ↓
教师分数 + 当前 .m7nnue 评价混合选着
              ↓
逐局面教师蒸馏更新
              ↓
终局胜负结果回灌
              ↓
定期保存 .m7nnue
```

规则层包括：

- 7×7 初始排布；
- 车、炮、马及蹩马腿；
- 将帅九宫；
- 将帅不得照面；
- 兵卒始终可向前、向左、向右，不能后退；
- 将死和困毙；
- 连续三次用车将军判负；
- 单方长将循环判负；
- 保守长捉识别；
- 普通三次重复判和；
- 120 半回合自然和。

训练器每 20 个半回合保存一次模型，每局结束再次保存；运行时间到达后还会执行最终保存。正常让工作流自行结束，不要在网页上强制 Cancel。

---

## 七、本地构建和测试

### Windows / Visual Studio 2022

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

执行一次模拟训练更新：

```powershell
.\build\bin\Mini7TrainerCLI.exe `
  --engine .\build\bin\MockUciEngine.exe `
  --variants .\config\variants.ini `
  --model .\models\smoke.m7nnue `
  --game-log .\logs\smoke.jsonl `
  --summary .\state\smoke.json `
  --duration-seconds 30 `
  --max-training-steps 1
```

### Linux 测试核心逻辑

Linux 不能运行仓库里的 Windows 仙鱼，但可以编译规则、网络和跨平台 CLI，并用模拟 UCI 引擎完成一更新测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/bin/Mini7TrainerCLI \
  --engine ./build/bin/MockUciEngine \
  --variants ./config/variants.ini \
  --model ./models/smoke.m7nnue \
  --game-log ./logs/smoke.jsonl \
  --summary ./state/smoke.json \
  --duration-seconds 30 \
  --max-training-steps 1
```

---

## 八、许可证与 Fairy-Stockfish 源码义务

本仓库训练器代码按 GPL-3.0 发布，见 [`LICENSE`](LICENSE)。Fairy-Stockfish 也采用 GPLv3。

GPL **不要求仓库必须公开**；但当你向别人分发 GPL 二进制时，需要同时提供对应源码，或者按许可证规定提供有效的源码获取方式。公开仓库通常是最方便的做法，但“公开”本身不能替代“对应源码必须与所分发二进制匹配”的要求。

仓库内 `engine/fairy-stockfish.exe` 是用户提供的二进制，SHA-256 为：

```text
f894e6db3e5f2842da57dbeab33505aabf976f55afccd30bb87c78cb8bcf2bb3
```

目前无法从该剥离符号的 EXE 确定精确 Git commit。若它是未修改的官方 Fairy-Stockfish 构建，应在 [`ENGINE_SOURCE_NOTICE.md`](ENGINE_SOURCE_NOTICE.md) 中补充对应版本或 commit；若它经过修改，则必须公开生成该二进制的完整对应源码和构建脚本。不要只链接一个不对应的最新版仓库然后声称已经履行 GPL。
