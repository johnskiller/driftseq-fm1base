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
| `F8` Clock | ignored（丢弃，不入 UI） | 计划：外部 clock 同步（见 §6） |
| `FE` Active Sense | ignored（完全丢弃） | — |
| `F0..F7` SysEx | ignored | **计划**：pattern/设定传输（§6） |
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
| `Bn` CC | 不发送 | — |
| `F8` Clock | 不发送 | 计划：作为 clock master 输出（§6） |
| `FA/FB/FC` | 不发送 | — |
| SysEx | 不发送 | 计划：dump（§6） |

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

## 6. 计划（planned，未排期）

| 项 | 说明 |
|---|---|
| SysEx pattern/settings dump | 一首歌 = 16 pattern × 16 轨 × 64 step。协议要点：通用 7-bit 打包、continuation CIN、帧校验；沿用 store 序列化已有格式 |
| 外部 clock 同步 | F8 接收 → `ds_tick` 相位锁定（现引擎按 ms 自走）；FA=对齐 loop_start，FC=停；slave 模式开关 |
| Clock master 输出 | 发送 F8（24/拍）+ FA/FB/FC，作为下游设备 clock 源 |
| FW 侧 CC | 至少 Master 音量（CC27 约定已有）；旋钮映射待拍板 |
| Program Change 接收 | 外部选轨音色 |
| All Notes Off | `Bn 123` 清 live 池 |
| note range 转调入口 | OCT± 已有 `key_shift`（面板）；MIDI 侧跟随同一 shift |
