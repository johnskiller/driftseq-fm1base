# DriftSeq (FM-1) — MIDI Implementation Chart

Date: 2026-10-27 · FW r16j lineage · `MPL-2.0`
Verified by: `tools/midi_probe.mjs` (wire format), `tools/midi_ui_probe.mjs`
(note→UI dispatch), `tools/midi_reaches_fw.mjs` (interrupt→app), all green.
**Plan** items are listed but NOT implemented.

## 1. Mode / 端口

| Item | 状态 |
|---|---|
| 接收端口 | USB-MIDI (PORT_USB=0) + BLE MIDI（fm1-ctrl 双链路，`plat_midi_in` 合流） |
| 发送端口 | 同上（引擎输出同时送 TO_USB\|TO_BLE） |
| Mode | Mode 4 (Mode 3 接收 + Mode 2 发送，固定 ch 由每轨 channel 决定)；**无 Omni**；**无 Local Off**（本地=面板键，恒开） |
| Note on/off | 恒有效（接收不分模式） |
| Clock | 接收=忽略（F8 丢弃）；发送=无 |
| Running Status | 接收端按 USB-MIDI 包解析（CIN 定长），不依赖 RS |

## 2. 接收（ recognized / 忽略 ）

| 消息 | 状态 | 行为 |
|---|---|---|
| `9n kk vv` (vv>0) | **recognized** | 音符即面板键（见 §4）；vv=写入力度（写入/试听都用真实 vv，vv=0 按 note-off） |
| `8n kk vv` | **recognized** | note-off：结束试听/voice（drum off C 侧去重） |
| `FA` Start | **recognized** | 播放（若未播）。edge-triggered：已播时忽略（ds_play 会重置 pos16，不重复触发） |
| `FB` Continue | **recognized** | 同 FA（从 loop_start 播；无中途续播概念） |
| `FC` Stop | **recognized** | 停止（若在播） |
| `F2` Song Position | ignored | — |
| `F3` Song Select | ignored | — |
| `F8` Clock | ignored（丢弃，不入 UI） | 计划：外部 clock 同步（见 §7） |
| `FE` Active Sense | ignored（完全丢弃） | — |
| `F0..F7` SysEx | ignored | **计划**：pattern/设定传输（§7）、MMC 走带（§6） |
| `Bn` CC | ignored（FW 侧零分支） | emu 页面层已映射 FM-1 自己的 CC 约定；硬件 CC 直控参数 = 计划项 |
| `Cn pp` Program Change | ignored (in) | 计划：轨 program 选择 |
| `Dn`/`En` | ignored | — |
| Pitch Bend / AfterTouch | ignored | fm1-ctrl 硬件有 aftertouch 计算但未发送 |

## 3. 发送（ transmitted ）

| 消息 | 状态 | 说明 |
|---|---|---|
| `9n kk vv` | **sent** | 音序器 step 触发（per-track channel）+ 面板/MIDI note 试听 (`ds_live_on`) |
| `8n kk 00` | **sent** | gate 到期（引擎 gate 池）或试听松键 (`ds_live_off`) |
| `Cn pp` | **sent** | ds_play 启动帧，每轨一条（track 的 prog） |
| `Bn` CC | **sent** | 鼓通道换 kit 用的 Bank Select（`CC0=120`、`CC32=0`），见 §3.1 |
| `F8` Clock | 不发送 | 计划：作为 clock master 输出（§7） |
| `FA/FB/FC` | 不发送 | 计划：见 §6（MMC / 走带控制） |
| SysEx | 不发送 | 计划：dump（§7）、MMC（§6） |

### 3.1 鼓通道的 Program Change 必须带 Bank Select

`emit_pc()`（`engine/driftseq.c`）在 ch10 上发三条：`CC0 120` → `CC32 0` → `PC n`，共 8 字节；旋律通​​道仍是单条 2 字节 PC。

原因是鼓通道的 program 选的是 **kit**，不是音色：GM Level 1 只定义一套 Standard Kit，裸 PC 对任何按规范实现的音源都等于没换；GM2 把鼓组放在 **bank 120**，GS 的鼓组同样走 bank select，XG 的 kit 也在自己的 bank MSB 下。CC32 显式归零，否则音源停在 GS 的 variation bank 上会忽略 CC0。

由此 golden 哈希从 `52CF520E290E322D` 变为 `1DFFB74BC111252F`（242 note-ons 不变）；`tests_engine/test_golden.c` 同时断言「剔除鼓通道这两条 CC 后必须逐字节回到旧哈希」，用来证明变化只有 bank select。


## 4. 音符接收映射（note → UI 语义，r16j parity）

按键位 = `note - 53 - key_shift`（面板 27 键 = F3..G5）。鼓轨不看键位窗，按 GM note 直配。

| 当前页面 | 音符 | 行为 |
|---|---|---|
| GRID + 鼓轨 | GM_DRUMS 音（36 KCK/38 SNR/42 CHH/46 OHH/49 CUP/45 TOM/41 RID/43 PERC/51 SHK，任意八度） | 选中该 lane + 试听（真实力度） |
| GRID + 鼓轨 | 非 lane 音 | 仅试听，不写入 |
| GRID + 旋律轨 | 键位 0..26 内 | **step entry**：清 step→写入该音（真实力度，gate 1）→cursor+1（死停） |
| GRID + 旋律轨 | 键位外 | 仅试听，永不写入 |
| GRID + EDIT 层 | 任意 | 加/删 voice（同音=删；满 4 声部=替换最高音；写入用真实力度） |
| 其它页 | 任意 | 试听（`ds_live_on/off`，音序器跟踪 held note） |
| 任意 | FA/FB/FC | transport（表 §2） |

面板键本身写 vel 100；MIDI 进来写真实力度——**两条入口同一 handler**（`panel_key_handler`），`midi_vel` 是唯一覆盖点。

## 5. 已知限制（v1 现状）

- FW 不响应 CC / Program Change / Song Position / Clock（全部忽略，不崩溃）。
- FB = FA（从 loop_start 起），不是字面 continue-from-position。
- SysEx 未实现（接收即丢；emulator 页面拆包把 0xF0 当 1 字节，未来协议需 USB-MIDI continuation CIN 4/5/6/7 设计）。
- 无 Local Off / Omni / All-notes-off（`Bn cc=123`）处理——外部音源挂起时只能 stop。

## 6. 走带控制：MMC 与 MPC 的 SysEx（**记录，待决策**，2026-10-30）

John 2026-10-30 给出 MPC 一侧的事实：

> mpc的播放控制支持mmc，也有自己的sysex
>
> - PLAY（播放）: `F0 7F 7F 06 02 F7`（标准 MMC）
> - STOP（停止）: `F0 7F 7F 06 01 F7`（标准 MMC）
> - RECORD（录音）: `F0 7F 7F 06 06 F7`
> - DEFERRED PLAY: `F0 7F 7F 06 03 F7`

### 6.1 这几条消息是什么

`F0 7F <device> 06 <cmd> F7` 是 MIDI Machine Control 的 **Real Time System Exclusive**：`F0` 起、`7F` = 通用非实时/实时 ID、第三字节是设备 ID（`7F` = 全体广播）、`06` = Machine Control Command 子 ID、第五字节是具体命令。命令码：`01` Stop、`02` Play、`03` Deferred Play、`06` Record Strobe。

要点：**全部 6 字节，且都在 SysEx 里**。它既不是 `FA/FB/FC` 那几个单字节实时消息，也不是通道消息。

### 6.2 FM-1 现状（实测，非推断）

| 项 | 现状 | 位置 |
|---|---|---|
| 接收 `FA/FB/FC` | **已实现**（边沿触发，重复触发不重置相位） | `ui/ui.c` `ui_midi_dispatch()` |
| 发送走带消息 | **无**。PLAY/STOP 只动引擎（`ds_play`/`ds_stop`），一个字节都不外发 | `ui/ui_edit.c` `case B_PLAY` |
| SysEx 接收 | **丢弃** | `app.c` `midi_in()`「SysEx -> ignored for now」 |
| SysEx 发送 | **无**；`engine_drain()` 按字节切 6 字节 SysEx 恰好正确，但 `app.c:354` 用它现算 CIN 会产出 `CIN F`/`CIN 0`（详见 6.3②） | `app.c`, `engine/driftseq.c` |
| 发送 API 粒度 | `plat_midi_send(uint32_t pkt, uint32_t to)`，**一次一个 packet**，即最多 3 字节 | `plat.h` |

### 6.3 决定做法之前必须先解决的四件事

**① 6 字节要两个 packet。** `plat_midi_send` 一次只能送一个 USB-MIDI event packet（3 字节）。一条 MMC 是 6 字节，按 USB-MIDI 打包惯例要拆成 `CIN 4`（`F0 7F 7F`）+ `CIN 7`（`06 02 F7`）。所以要么在引擎里按两个 packet 入队，要么给平台加一个「多 packet 一次发」的入口。

**② 真正的堵点在 CIN，不在字节。** 我先怀疑 `engine_drain()` 会把 6 字节拆错，实测**不是**：它按 status 高 4 位判长度（`C0/D0` → 2 字节，其余 → 3），`F0` 与 `06` 都落进「3 字节」，于是 6 字节正好被切成 `F0 7F 7F` + `06 02 F7` —— 字节边界是对的。

错的是 CIN。`app.c:354` 用 `pkt = (st >> 4) | st << 8` 现算 CIN，实测这两帧得到：

```
bytes F0 7F 7F -> CIN F (single-byte realtime)   pkt=7F7FF00F
bytes 06 02 F7 -> CIN 0 (misc)                   pkt=F7020600
```

而 USB-MIDI 规范要的是 `CIN 4`（SysEx start/continue）+ `CIN 7`（SysEx ends with 3 bytes）。`CIN 0` 这一帧 macOS 会直接丢（§3 那条注释已经踩过同样的坑），`CIN F` 更糟 —— 它宣告「这是单字节实时消息」。

所以 SysEx 发送需要的是**能显式指定 CIN 的入队方式**，而不是复用 `st >> 4` 这条给通道消息用的捷径：`F0` 起头写 4、`F7` 结尾那一帧写 7（或按实长 5/6）。


**③ BLE 发送侧对 SysEx 的 CIN 处理有洞（已读代码确认）。** `le_midi.c` 的 `ble_midi_send_many()` 里：

```c
if (cin == 0xF || cin == 5) {                    /* 不拷任何数据字节 */
} else if (cin == 2 || cin == 0xC || cin == 0xD) { m[k++] = pkts[i] >> 16; }
else { m[k++] = pkts[i] >> 16; m[k++] = pkts[i] >> 24; }
```

`CIN 5` 是「SysEx 以 1 个字节结尾」，其唯一的数据字节就是终止 `F7` —— 这一支直接把它丢了。`CIN 4`/`CIN 7` 只是碰巧落进 default 分支，字节数对得上，但随后 `if (st < 0xF0) run = st;` 会把 SysEx 的数据字节 `0x06` 记成 running status。**BLE 上发 SysEx 之前要修这里**，USB 上不受影响。

**④ 仿真器页面层会把 SysEx 整个吃掉**（`web/emu/worklet.js` 的 out 循环，实测读代码确认）：它把每个 packet 都当成「从一个 status 字节开始」的消息，`st < 0xF0` 才按通道消息取数据字节，`F2` 特例取两个，**其余一律「its one byte and no more」**。所以 `F0 7F 7F` 只会送出 `F0`，`06 02 F7` 连状态位都没有、`st < 0xF0` 判为假且不是 `F2`，也只送 1 字节。这个循环是给通道消息写的，SysEx 需要单独按 CIN 解包，否则 emu 里永远看不到也发不出 MMC —— 而 emu 是我验证这类功能的唯一手段。


### 6.4 待 John 拍板的问题（未决定，不自行扩功能）

1. **要不要发？** FM-1 作主机，按面板 PLAY/STOP 给下游发 MMC；还是回头做 `FA/FC` 那套单字节（更简单、更多设备认）；还是两者都发。
2. **谁指挥谁？** 是 FM-1 按键去控 MPC（FM-1 = 主机），还是 MPC 的走带去同步 FM-1（MPC = 主机，FM-1 收 MMC）。方向不同，代码量差很多：后者只需要在 `midi_in()` 里加 SysEx 收集 + 命令分派。
3. **RECORD 发不发。** `06 06` 是 Record Strobe，误触会把 MPC 推进录音。要不要绑在 REC 上，还是要二次确认。
4. **DEFERRED PLAY**（`06 03`）与 PLAY 的区别只在「是否等设备就绪」，FM-1 侧要不要区分。
5. **设备 ID** 用 `7F` 广播还是可配（多台设备时）。
6. **MPC 自己的 SysEx** 你还提到有——那部分消息格式未给，等你的清单再排。

## 7. 计划（planned，未排期）

| 项 | 说明 |
|---|---|
| SysEx pattern/settings dump | 一首歌 = 16 pattern × 16 轨 × 64 step。协议要点：通用 7-bit 打包、continuation CIN、帧校验；沿用 store 序列化已有格式 |
| 外部 clock 同步 | F8 接收 → `ds_tick` 相位锁定（现引擎按 ms 自走）；FA=对齐 loop_start，FC=停；slave 模式开关 |
| Clock master 输出 | 发送 F8（24/拍）+ FA/FB/FC，作为下游设备 clock 源 |
| FW 侧 CC | 至少 Master 音量（CC27 约定已有）；旋钮映射待拍板 |
| Program Change 接收 | 外部选轨音色 |
| All Notes Off | `Bn 123` 清 live 池 |
| note range 转调入口 | OCT± 已有 `key_shift`（面板）；MIDI 侧跟随同一 shift |
