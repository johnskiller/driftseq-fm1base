# 进展

> 给下一个会话（或明天的自己）看的。只写实测过的事实；没验证过的单列在"未验证"。
> 最后更新：2026-10-27

## 一句话

driftseq 的逻辑全部搬进 MPL 项目 `driftseq-fm1base` 并能跑；**真机一次没刷过**，所以"不变砖 / USB-C MIDI / BLE MIDI 双向"这三个核心承诺仍是纸面。

## 仓库

| 仓库 | 路径 | HEAD | 作用 |
|---|---|---|---|
| `driftseq-fm1base` | `/home/john/.hermes/workspace/driftseq-fm1base` | `244a998` | 固件 + 全部 UI。MPL-2.0，`git init`，**无 remote** |
| `driftseq` | `/home/john/.hermes/workspace/driftseq` | `8e379aa` | 旧 GPL 仓库，**只收 spec/docs，不再收固件代码** |

两边工作树都干净。

## 提交链（新项目，旧→新）

```
0b316cc  4bb7ff7  8888791  d13799a  4ae7c24  d5d6e2c  dda724a
76172d8  bfd238f  3a775cb  1fce1e7  b50f18b  0be1e69  9744689
cee387d  9781d21  f55129e  997b972  244a998
```

最近 5 个：
- `9781d21` 恢复鼓页光标步号，迁移零缺失验证
- `f55129e` pattern 按 track 稀疏保存格式
- `997b972` `ds_note` 删死字节 4→3 B，加 `ds_step.tie`
- `244a998` tie 的 UI 手势 + 可视

## 当前尺寸 / 内存（实测）

```
sizeof(ds_note)    3 B
sizeof(ds_step)   17 B     v[4]=12 + nvoices + on + prob + cond + tie
sizeof(ds_track)   1,101 B   64 steps + 13 B meta
sizeof(ds_pattern) 17,616 B
```

固件构建（`build.py`，最后一次）：

```
image 250,508 B
RAM   .data+.bss 68,276 B of 98,304   余 30,028
pool  155,192 B of 344,064            余 188,872
fwsc  611,496 B, identity FM-1_9200
```

`eng`（`ds_state`）在 `.pool`，不在 DRAM。这是 `9744689` 的成果：DRAM 94,436 → 68,276。

## tie（已完成，`997b972` + `244a998`）

**数据结构**：`ds_step.tie` 独立 1 bit，不是 `gate=0xFF` 的写法。理由：三态可逆要求不删数据。

**引擎**（`engine/driftseq.c` `land()`）：
- 开头 `if (st->tie) return;` —— 在消耗 RNG **之前**返回，所以加 tie 不改变其他步的随机序列
- 起音后若 `gate==0xFF`，向后扫**跳过 tie 步**，找下一个攻击步，把连续 tie 格数累加进 gate
- 连续音就是这样来的

**手势**：`GLO+OCT+`，光标步三态循环
```
play --(1)--> tie   : on=0, tie=1    声部保留
tie  --(2)--> rest  : tie=0, on=0    声部仍保留
rest --(3)--> play  : on=1           仅在还有声部时
```
没声部的空步按第三下不动作。

**视觉契约**：
- 攻击：色相线，两侧各 2px 空隙，宽 9px
- tie：**同色相同音高**，横跨整格并向左**压过格边界 1px**，宽 14px
- rest：无线

"连没连"就是全部语义。没有字形、没有模式字。

**关键实现点**：tie 步保留自己的 voices（为了 rest→play），但**必须画被延长那一步的音高**，否则会宣称一个没人听见的音。用 `prev_attacking_step()` 回走 loop 窗口（绕圈、跳过 tie），一段里所有 tie 解析到同一个攻击步 —— 这就是"融成一条"的机制。

**踩过的坑**：`prev_attacking_step()` 里 `(start + rel + len - d)` 应为 `(start + rel + 64 - d)`。多加了 `loop_len`，走到 loop 长度位置找到空步。症状极具迷惑性：**手势完全正常**（rest 变空、第三按恢复 9px），只有 tie 不画线。

## step 状态机（四种状态，spec 01 已写）

| `on` | `nvoices` | 状态 | 含义 |
|---|---|---|---|
| 1 | >0 | play | 正常发声 |
| 1 | 0 | bare gate | euclid 骨架 / 鼓 lane 占位 |
| 0 | >0 | silent-keep | 静默保留，单击复活 |
| 0 | 0 | rest | 空 |

加上 `tie` 是第五个维度，与 `on` 正交：`tie=1` 时 `on` 应为 0。

`on` 必须保留（用户裁定）：`ds_euclid_fill` 只写 `on` 不写 voice，所以 bare gate 是 euclid 的骨架。`ds_note.on` 才是死字段（零引用），已删。

## 保存格式（`engine/pack.c/.h`，`f55129e`）

按 track 稀疏，未用的 track 只花 1 B。

```
hdr bit0-2  nvoices (0..4 直存, 3 bit)
    bit3    on
    bit4    prob != 100
    bit5    cond != DS_C_NONE
    bit6    tie
    bit7    (free)
run = gap1 + hdr1 + prob1 + cond1 + 4×(note1+vel1+gate1) = 16 B
```

`nvoices` 必须能表达 0。原来 2 bit 存 `nv-1`，**表达不了 0** → bare gate / tie 步解包时凭空多读一个 voice。改 3 bit 直存 + `nv > DS_VOICES` 防越界。

`step_has_content()` 必须认 `on || tie`。tie 步 `on=0` 且无 voice，旧判据会**静默丢弃**整个 tie。

实测容量：
```
空 pattern      16 B
demo            146 B
单轨单音         30 B
最坏          15,392 B
上限/track     1,036 B
16 patterns 全 demo 密度 2,336 B；最坏 246 KB；硬上限 259 KiB
```

**RAM 里的 live pattern 保持 flat struct**，稀疏只是保存格式。UI 以 40fps 按索引改 step，稀疏结构会让每次绘制都要解码。

## 测试（全部实测通过，2026-10-27 重跑）

主机 C 测试（`cc -std=c99 -O2 -Iengine -I. <test>.c engine/... `）：

| 目标 | 构建 | 结果 |
|---|---|---|
| golden | `tests_engine/test_golden.c engine/driftseq.c engine/demo_pattern.c` | `420C4824C85B79CB` / 242 note-ons |
| tie | `tools/tie_test.c engine/driftseq.c` | 19/19 PASSED |
| pack | `tools/pack_test.c engine/pack.c engine/driftseq.c engine/demo_pattern.c` | PACK OK，上限 1,036 B |
| pack_engine | 同上换 `tools/pack_engine_test.c` | ENGINE ROUND TRIP OK |
| euclid | `tests_engine/test_euclid.c engine/driftseq.c` | PASSED |
| engine | `tests_engine/test_engine.c engine/driftseq.c` | PASSED |

**注意**：`pack_test` / `pack_engine_test` 需要链 `engine/demo_pattern.c`，否则 `undefined reference to ds_load_demo`。

emulator 探针（帧缓冲读像素，`PATH=/home/john/.hermes/node/bin:/usr/bin:/bin node tools/<p>.mjs`）：

```
tie_probe        11 checks  TIE PROBE PASSED
glo_probe                    GLO OK
migration_probe 11 checks   MIGRATION OK
header_stable    3 checks   HEADER STABLE
knob_probe      13 checks   KNOB VALUE OK
pill_probe                   PILL OK
ui_probe        16 checks   UI PROBE OK
engine_probe                 ENGINE PROBE OK
web_resume_probe 23 checks
```

两个探针验证过**能失败**：关掉 tie 绘制 → tie_probe 挂 4 条；关掉手势 → 挂 3 条。

## 构建命令

```bash
# 固件
cd /home/john/.hermes/workspace/fm1-ctrl
AC79_SDK=/tmp/fw-AC79_AIoT_SDK JIELI_TOOLCHAIN=/home/john/.jieli/toolchain \
  python3 platform/tools/build.py /home/john/.hermes/workspace/driftseq-fm1base

# 加 --web 出 emulator
# 必须先 export PATH=/tmp/emsdk/upstream/emscripten:$PATH 才找得到 emcc
```

**坑**：不要 `source /tmp/emsdk/emsdk_env.sh`，它会覆盖 `/home/john/.hermes/node/bin/node`。构建后跑探针要单独给 PATH。

## 本地服务（都是 gateway 的子进程，重启 gateway 会全死）

| 端口 | 命令 | 用途 |
|---|---|---|
| 8931 | `cd driftseq/firmware/web && python3 -m http.server 8931` | 旧仓库 webemu |
| 8932 | `cd driftseq-fm1base/build/emu && python3 -m http.server 8932` | 新项目 HTTP（手机用不了） |
| 8933 | `cd driftseq-fm1base && python3 tools/serve-https.py 8933` | 新项目 HTTPS，手机可用 |

手机访问 `https://192.168.31.129:8933/`（自签证书，要接受）。iOS 的 audioWorklet 只在 secure context 下工作，所以 HTTP 不行。

## 未验证（真机一次没刷过）

- **不变砖**：9/10 信心，无实测
- **USB-C class-compliant MIDI**：9/10，无实测
- **BLE MIDI 双向**：9/10，无实测
- `.ram_text` flash 时序
- `PLAT_POOL` 在真机的 cache / DMA / 启动清零行为
- `ble_heap_kb` 默认 16 KB 不敢下调，需要真机 `shim_heap_peak` 日志

## 待办

1. **realtime record** —— 用户明确说"明天再仔细说"，**不要自行设计**。已知三层缺口：① MIDI in → pattern 写入器（零）；② 子步偏移（`pos16` 只有整数格，无存储位）；③ note-off → gate（UI 恒写 `gate=1`，需跨 tick 配对）
2. **GM 音源容量评估** —— 用户要求过，没做。手上只有 pattern 存储账 + erik37 的 406 KB 采样 bank 参考（`/tmp/erik37`，只读）
3. **euclid meta 内联** —— 现在 `ds_euclid_fill` 把 `{on,k,n,rot}` 展开成 64 个 step；存 4 字节加载时展开，能让 demo trk0/1/2 从 ~20 B 降到 ~5 B
4. **`device_include` + shim** —— erik37 的 `app/dsp/compat/string.h` 比现在 `pack.c` 的 `#if __has_include` 写法干净，值得采纳

## 环境变量 / 已知坏的东西

- **`vision_analyze` 401**：`config.yaml:137-150` 的 `auxiliary.vision` 配置是对的，`api_key: env:LIANDISYS_API_KEY`。问题是**运行中的 gateway 进程环境里是旧 key**（`sk-uZv…ku6j`），`.env` 里是新的（`sk-0Lk…46Fd`）。新 key 实测能识图。修法：`systemctl --user restart hermes-gateway.service`（重启会杀掉本 session 和 8931/8932/8933）
- **`browser_exec` 不可靠**：多次 300/120s 超时
- **iOS emulator 音频挂起**：切 App 回来画面冻，用户裁定不追

## 设计约束（用户原话，别违反）

- **`ble_central` 必须保持 `true`**（`fm1.toml:24`）
- **`fm1-ctrl` 仓库只读**，不得改动（idea 6 的 thread 在用）
- **第三方仓库不得留改动**：`/tmp/fm1-ctl` 是上游副本，`/tmp/erik37` 只读
- **存储不用 malloc**："当然malloc那一套动态分配行不通"；"至少可以按track存，没用的trk不消耗"
- **`ds_step.on` 必须保留**："哦，那么on得保留"
- **tie 独立状态**："tie先用独立状态吧，play时是连续音就可以"
- **realtime 录音不自行设计**："realtime录音我有些想法，明天再仔细说"
- **hero 只显示 loop 内 bar**；每轨独立 playhead；melody 纵向按音高；LED dim 每 4 step 一个、与 DIV 无关；圆环点可用方块
- **header**：24px 单行、实心药丸、药丸里只写数字、宽度不随位数变化、圆角 r=6
- **不要 `source /tmp/emsdk/emsdk_env.sh`** 之后 build/run
- 凭证一律不写入文件

## 关键坐标（改 UI 时要用）

```
header      cv_rect(0,0,240,24,TB_BG)，分隔线 y=23，SEP=0x2946
药丸        pill_w=24 h=18 y=3 x=6 r=6；内缩 4,2,1,1,0,0 … 0,0,1,1,2,4；正文 x=38/39
鼓页        cv_begin(240,160)、y0=24、NDRUM=9、lh=10、gap=3、cellh=7、卡片 cv_blit(0,115)
            cardRow=115 是鼓页判据；demo 鼓在 track 0/1/2
旋律页      格 x0=18+s*13、y0=16、格 13×24
```

## 结构提醒

`fm1-base` 是 **unity build**（`platform/device/unity.c:355` → `#include "app.c"`）。**没人 `#include` 的 `.c` 静默不编译** —— 曾经 `pack.c` 根本没被编进去过。

目标端无 `<string.h>`，libc 自己声明 `memcmp`。
