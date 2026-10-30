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

### 6.0 证据分级（重要）

John 2026-10-30 的补充：「mpc也支持标准mmc，不过这些都是没检验过的信息」。

所以本节把两类事实分开，**不要混用**：

| 级别 | 内容 | 来源 |
|---|---|---|
| **A 已核** | MMC 的报文格式与命令码 | MIDI 规范（Universal Real Time SysEx），已对照 Teragon Audio 的规范摘录逐字节核对 |
| **B 未核** | **MPC 是否响应这四条、响应到什么程度** | John 的记忆/推测，**尚未在 MPC 上试过** |

⚠️ **任何实现都必须先在 MPC 上实测这些报文的效果**，不能把 B 当 A 用。本文档后续出现「已验证」字样时，仅指 A 级。

### 6.1 报文格式（A 级，已核）

John 给出的四条（标注为 B 级，即 MPC 响应未验证）：

> - PLAY（播放）: `F0 7F 7F 06 02 F7`（标准 MMC）
> - STOP（停止）: `F0 7F 7F 06 01 F7`（标准 MMC）
> - RECORD（录音）: `F0 7F 7F 06 06 F7`
> - DEFERRED PLAY: `F0 7F 7F 06 03 F7`

通用形式：`F0 7F <deviceID> 06 <command> F7`

- `F0` 起、`F7` 止 —— Real Time Universal SysEx
- `7F` = 通用实时 ID
- 第 3 字节 `<deviceID>`：`7F` = **All-Call**，所有设备都应响应，不管自己的 ID
- 第 4 字节 `06` = Machine Control Command 子 ID
- 第 5 字节 `<command>`，规范值：`01` Stop、`02` Play、`03` Deferred Play、`04` Fast Forward、`05` Rewind、`06` Record Strobe (Punch In)、`07` Record Exit (Punch out)、`09` Pause

**对照结论**：你给的四条与规范完全一致（`02`/`01`/`06`/`03`）。也就是说**报文本身没问题**，唯一未定的是 MPC 收到后做什么。

规范里还有几条本工程可能用得上的，一并记在这里（A 级）：`09` Pause、`05` Rewind、`04` Fast Forward，以及 `Goto` = `F0 7F <id> 06 44 06 01 <hr><mn><sc><fr><ff> F7`（SMPTE 定位，注意它是 `06 44` 后面再跟**长度字节 `06`**，与上面那些「无载荷」命令结构不同）。


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


### 6.4 三个来源，不是一类东西（2026-10-30 追加）

John：「我还有个qy100也能发出播放控制，还有daw，这些都需要测试」。

这句话里的三个来源（MPC / QY100 / DAW）**千万不要当成同一类测试**——它们的走带协议很可能完全不同：

| 来源 | 已知 | 证据强度 | 要测什么 |
|---|---|---|---|
| **MPC** | 你说支持标准 MMC | B（未测） | `06 01/02/03/06` 四条在 MPC 上的实际反应 |
| **QY100** | 你说能发播放控制 | **C（存疑，见下）** | **先确认它到底发哪种** —— 极可能是 `FA/FC` 而非 MMC |
| **DAW** | 各软件设置不同 | C | 先在 DAW 里把 MMC/Clock 发送分别打开，再逐条测 |

#### QY100 的疑点（必读）

查到的证据指向 **QY100 发的是 `FA/FC` + `F8` clock，而不是 MMC**。

Yamaha QY 系列 Data List 里的「(1) TRANSMIT FLOW」是个**家族模板**，我在三个同系列型号的原文里都看到同一串：

- **QY20** Data List：`CONTROL CHANGE BnH` / `PROGRAM CHANGE CnH` / `PITCH BEND CHANGE EnH` / `TIMING CLOCK F8H` / `START FAH` / `CONTINUE FBH` / `STOP FCH` / `SONG POSITION POINTER F2H`，后面接 `F0H 43H 0nH 0AH ...` 的厂商 SysEx
- **QY300** Data List：同样的 `F8H / FAH / FBH / FCH / F2H`，加 `SONG SELECT F3H`、`ALL SOUND OFF`、`RESET ALL CONTROLLERS`

**两份原文里都没有 `F0 7F ... 06 xx` 这一族 MMC。** 发的是单字节实时消息 + Yamaha 自己的 SysEx。（QY70 我只查到「同族」的说法，没读到它的 Data List 原文，不计入证据。）

需要说清楚的边界：**我没直接读到 QY100 自己的 Data List**（manualslib 反爬，Yamaha 只公开了 QY300 的 PDF 直链）。所以这是「同系列两份原文 + 强家族一致性」的推断，不是 QY100 的原文证据。仍然是 C 级，只是支撑比之前那个论坛帖硬得多。

另外：有第三方帖是让 **QY100 当 MMC 的从机**（电脑发 MMC 给它），说明「接收 MMC」和「发送 MMC」是两件事，**不能由前者推出后者**。

所以：**「QY100 能发播放控制」大概率指的是 `FA/FC`，而 FM-1 的 `ui_midi_dispatch` 已经能收这两个**。如果成立，QY100 这条的**走带**部分已经能工作，只是没人验证过。

但**不要因此认为 QY100 这条就通了**，还有两个前提没验证：

1. **FM-1 根本无法验证，除非接上 Mac**。FM-1 的显示只认 `midi_last`/`midi_count`（`app.c`），**屏幕上不显示收到的报文**（PROGRESS 已记：设备 log 不在屏幕，走 `fm1_remote.py log`）。所以「FM-1 收到 FA 了吗」这个问题，在设备上**看不出来**，要么接 UART log，要么在 Mac 上分接一路监听。
2. **clock 不跟**（见下节）。QY100 发 `F8` 但 FM-1 全丢，所以两者只能同时开始、速度各跑各的。如果 QY100 的曲子是 120 BPM 而 FM-1 设 100，**按下去就立刻分开**，表现像是「收不到」但其实是「收得到、不跟拍」。

这两点合起来意味着：**QY100 的测试必须同时抓报文和看时钟漂移**，只按一次 PLAY 看有没有动，会得出错误结论。

证据边界再说一次：**这是 C 级推断，不是 QY100 原文**。**测的第一步是确认它输出哪种报文**，而不是假设。

#### FM-1 对 clock 的现状（实测，A 级）

`app.c` `midi_in()` 里：`int keep = st != 0xF8u && st != 0xFEu;`，F8 与 FE 都被判为「不保留、不显示、不分派」，直接丢。所以：

- **F8 clock：完全丢弃**，不影响引擎相位
- **FA/FB/FC：收，且已实现**（边沿触发，重复触发不重置相位）
- **F2 Song Position：丢弃**（两处：`app.c` 未保留、`ui_midi_dispatch` 无分支）

这意味着 FM-1 现在是**只跟走带、不跟拍**：按下外部 PLAY，FM-1 从自己的 loop_start 起，然后按自己的 BPM 跑，与主机的速度**没有任何绑定**。QY100/DAW 当主机时这会明显不同步——如果它们发的是 120 BPM 而 FM-1 是 100 BPM，两者会立刻分开。

**这大概是这条线里最该先决定的事**：只要不接 F8，任何「播放控制」都只能让两者同时开始，不能保持在一起。

#### DAW 的额外坑

DAW 侧发送 MMC 和发送 Clock 是**两个独立开关**，位置各软件不同，且**Ableton Live 根本不发 MMC**（它的 Sync 设置只做 MIDI Clock，没有 MMC 发送项）。

| DAW | Clock 发送 | MMC 发送 |
|---|---|---|
| Logic Pro | `Project Settings > Synchronization > MIDI` | 同页有 `Transmit MMC` 勾选框 |
| Ableton Live | `Preferences > Link/Tempo/MIDI` 的 MIDI Ports 表，Output 的 `Sync` 勾选框 | **无**（Live 不做 MMC） |
| Reaper | `Preferences > MIDI Devices` 右键端口 → `Send clock to output` | 需装 MMC 脚本/虚拟端口 |

所以**如果你的 DAW 是 Ableton，MMC 这条路对它直接不成立**，只能是 `F8 + FA/FC`（Logic 两个都支持）。这也反过来提示：**`FA/FC` 才是三家共通的公共集**，MMC 是 MPC 那类设备的额外能力。这一点会影响 6.6 第 1 问的答案——先做 `FA/FC` 就已经覆盖 QY100 和多数 DAW。

测的时候要分开勾，否则分不清是哪条在起作用。

而且 DAW 常同时发 `FA` 和 MMC Play。FM-1 一旦两者都实现，**同一次播放会收到两个 PLAY**，必须做去重（对 `FA` 已经做了边沿判断，MMC 侧要同样处理，否则 `ds_play` 会把相位重置两次）。

### 6.5 上机测试方法（**动手前先做这一步**）

这些报文在 MPC / QY100 / DAW 上到底有没有用，只能上机试。三个办法，从省事到麻烦：

**办法一：先当接收方测，用 `sendmidi` 发（推荐，最省事）**

（测试方向：Mac → 对方。用来确认对方**作为接收方**具备 MMC 从机能力。）

```bash
brew install sendmidi
sendmidi list                                   # 找到 MPC / QY100 / DAW 的端口名
sendmidi dev "MPC ..." hex  F0 7F 7F 06 02 F7   # PLAY
sendmidi dev "MPC ..." hex  F0 7F 7F 06 01 F7   # STOP
```

注意：MPC 若通过 MD-BT01 无线连，macOS 上会作为 BLE MIDI 端口出现，`sendmidi` 认它；USB 直连更稳。

**办法一之二：当发送方测（这才是「QY100/DAW 发出的是什么」的答案来源）**

（测试方向：对方 → Mac。用来确认对方**实际发出**哪种报文。）

反过来接：**QY100 / DAW 的 MIDI OUT → Mac**，然后在 Mac 上把收到的原始字节日志下来。

```bash
# 任何能打印原始 MIDI 输入的工具都行
sendmidi list
# 用 macOS 自带很好用的办法：
#   1) 打开「音频MIDI设置」→ 窗口 → 显示 MIDI 监视器（MIDI Monitor 窗口）
#   2) 选中来源端口，按 QY100/DAW 的 PLAY
#   3) 看它打出的是 F0 7F ... 06 02 F7 还是 FA
```

这一步是**整件事的分水岭**：三种来源各跑一遍，就知道各自发的是什么。**在此之前不要写任何实现代码**——尤其别假设 QY100 发 MMC。

**三种来源各自跑一遍，结果记在这张表里**（测完再填，别猜）：

| 来源 | 测什么 | 结果 | 日期 |
|---|---|---|---|
| MPC | 收 `06 02` 是否播放 | *(待测)* | |
| MPC | 收 `06 01` 是否停止 | *(待测)* | |
| MPC | `7F` vs 具体 ID 差异 | *(待测)* | |
| MPC | 连发两次 PLAY 是否重头 | *(待测)* | |
| QY100 | **发的是 `FA/FC` 还是 `F0 7F ... 06 xx`** | *(待测——这一步决定后面还需不需要 MMC)* | |
| QY100 | 是否发 `F8` clock、是否发 `F2` | *(待测)* | |
| DAW | 只勾 MMC 时的报文 | *(待测)* | |
| DAW | 只勾 Clock 时的报文 | *(待测)* | |
| DAW | 两者都勾时 FM-1 是否双触发 | *(待测)* | |

**办法二：任何能发原始 SysEx 的软件**（MIDI-OX、Pocket MIDI、Logic 的 environment 都行），粘贴上面那串 hex 即可。关键是软件必须真的发 SysEx，不能把它当普通 MIDI 消息吞掉。

**办法三：本工程自己的 emu**——**现在不行**。见 6.3④，`web/emu/worklet.js` 的 out 循环会把 SysEx 吃掉，得先修。

**四条各自要观察什么**：

| 报文 | 期望 | 若无效说明什么 |
|---|---|---|
| `06 02` PLAY | MPC 开始播放 | MPC 不认 All-Call ID，或压根不支持这条 |
| `06 01` STOP | MPC 停止 | 同上 |
| `06 03` DEFERRED PLAY | 同 PLAY（可能延后一拍） | MPC 区分了这两条 |
| `06 06` RECORD STROBE | MPC 进入录音待命/开始录 | **危险**：可能直接覆盖已有内容，先存盘 |

**如果 All-Call（`7F`）没反应，试具体 ID**：把第 3 字节换成 MPC 的 MMC 设备号（MPC 设置里通常有，默认多为 `00` 或 `7F`）。这一步能把「不支持」和「ID 不匹配」区分开——两者现象一样，原因完全不同。

**还要确认沿触发还是电平**：连发两次 PLAY，MPC 是重头播还是忽略第二条；STOP 后发 DEFERRED PLAY 与 PLAY 是否有行为差异。这些结果直接决定 FM-1 侧要不要做边沿去重（`FA/FB/FC` 那边已经这么做了）。

### 6.6 待 John 拍板的问题（未决定，不自行扩功能）

**先答第 1、2 问，其余都依赖它们。**

1. **谁指挥谁？（最关键的岔路）**
   - **A. 外部当主机**（QY100 / DAW / MPC 发，FM-1 收）：`ui_midi_dispatch` 已经收 `FA/FB/FC`，**基本已具备**。要补的只有：SysEx 收集 + MMC 命令分派（若确认需要 MMC）、以及要不要跟 `F8`（见第 3 问）。**这条最省力，且立刻能让 QY100/DAW 用起来。**
   - **B. FM-1 当主机**（面板 PLAY/STOP 发出去控别人）：要发 `FA/FC`（简单）或 MMC（要先修 CIN、BLE SysEx 终止字节、emu 分帧，见 6.3）。**工程量是 A 的数倍。**
   - **C. 双向都做**：A + B，且必须处理「自己发给自己」的回灌（BLE 回声那事已定案不修，但走带会重新触发它）。

2. **MMC 到底要不要做？** 依据 6.4 的证据，**`FA/FC` 已覆盖 QY100 和多数 DAW，MMC 只对 MPC 有用**。所以合理的顺序是：**先只做 A 的 `FA/FC`，把 QY100/DAW 跑通；MMC 等确认 MPC 真的认了再加。** 若同意，6.3 那四件事（CIN / BLE 终止字节 / emu 分帧 / SysEx 组包）全部可以推迟。

3. **要不要跟 `F8` clock？（决定「同步」还是「同时开始」）**
   - 不跟（现状）：只听走带，速度各跑各的。**够用于「一起开始并按各自的表走」**，但会漂。
   - 跟（需要 `ds_tick` 相位锁定）：PLL 或简单计数，改动在引擎核心，风险高。**这是独立的一大块，建议单独一轮。**
   - 折中：跟 `F8` 只用于**测速**（临时算 BPM 显示/对齐），不锁相位。风险小、能看出漂移量。

4. **RECORD 发不发。** `06 06` 是 Record Strobe，误触会把 MPC 推进录音。要不要绑在 REC 上，还是要二次确认。

5. **DEFERRED PLAY**（`06 03`）与 PLAY 的区别只在「是否等设备就绪」，FM-1 侧要不要区分。

6. **设备 ID** 用 `7F` 广播还是可配（多台设备时）。

7. **MPC 自己的 SysEx** 你还提到有——那部分消息格式未给，等你的清单再排。

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
