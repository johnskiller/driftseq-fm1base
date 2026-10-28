# 进展

> 给下一个会话（或明天的自己）看的。只写实测过的事实；没验证过的单列在"未验证"。
> 最后更新：2026-10-28

## GM 音源（Sonivox EAS）—— 2026-10-28 实测

- `eas_ok=1`，host sim `expect gm 1`、pool peak 21,648 B（96 KB 池）。
- **已修**：`gm_render` 收到的 gain 是 codec 窗口增益（~8.3e6）而不是 0..1 —— pcm 已 `<<8`，再乘一次窗口 = 32 000×溢出，host peak 恒 48.2 dBFS（int32 满轨 2^31/8.3M 的数学痕迹）。修法：调用点传 `gain_now / 8300000.0f`。修后 host：demo 播放 -19 dBFS、停止 -45 dBFS、MIDI 单音 -23.8 → off+CC123 → -57.3 dBFS。
- **web 端客观测过**（`tools/webkick2.mjs` / `webdemo.mjs`，128 帧 quantum 同 worklet）：无削波（demo peak 0.467）、无镜像噪声（20 kHz 处 ~0）、频谱干净。"音质差"不是渲染 bug。
- **kick"release 很长"的真因（数据确认）**：`ui/ui.c` 鼓格 `panel_key_handler` 的 M_GRID 鼓分支 `if (!down) return;`（r16j parity），键抬起**不发 note-off**，live note 靠 `ds_tick` 的 2-cell 自动 gate 兜底；demo 鼓轨 division=DS_DIV_8(0)@120bpm → 2 cell = **500 ms**，Sonivox 鼓样本在 note 持续期间**循环 sustain**（host 实测按住 kick 900 ms 恒 -20 dBFS），松开后 release 再 200-400 ms。面板点击鼓格 = 每次 ~700-900 ms 的 kick。MIDI 通道的鼓音（`ui_midi_note` 鼓分支）有 off，不受影响。
- **已修（John 拍板：抬键发 off，与 melody track 同型）**：drum 格子 down/up 改为 on/off 成对（写入仍是 press-edge，up 只发 off 不碰写入）。host 实测：fast tap 音长 ~230ms（原来 700-900）；按住 300ms，线上 `99 24 64` → `89 24 00` 成对，无 2-cell 兜底延迟。
- **为什么"鼓会循环 sustain"不是 bug 而是 bank 数据**：drum kit = rhythm 通道 program 0 → bank 表 program 0 起始 region 291；region 291（keys 21-42，含 kick 36）`flags=0x0001`（REGION_FLAG_IS_LOOPED，无 ONE_SHOT 位参与逻辑），loop 1047..1229 样本（22050 Hz 下 ~8 ms 环），EG1 有 sustain → 按住 = 环里循环。整个 kit 291-301 每个键区都 LOOPED。host 实测按住 note 36/38/42 1.2-1.5 s 全部恒 -20 dBFS（185 Hz，样本自己的 body 音高）。**任何按 GM 规范发长 gate 的音源都会这样**；一般 GM 序列器鼓轨 gate 短所以没人听见。
- **对比 jartificer/fm1-gm v1.1.0（其鼓无 sustain 问题）**：它不是 EAS——自研 ADPCM 波表引擎（`dsp/gm.c`），bank 由 `tools/sf2_reduce.py` 从 FluidR3 SF2 离线裁出。其鼓无 sustain 的两层防御：drum 键区 ring_time > DRUM_LONG 的（长鼓/镲）**把样本自身衰减烘进样本**（`envelope_fade`：sustainVolEnv=1000=SF2 的 0 电平），短鼓直接裁成 one-shot（无 loop）；固件侧 `drum_hit` 设 `x->held = 0`——**鼓对 note-off 完全免疫**。EAS 的 wt_200k_G 没这层处理。
- **EAS 来源查证（2026-10-28，逐文件 md5 验证）**：`eas/` 82 个 tracked 文件的直接来源是 **charlesvestal/fm1-ctl@`3bab7b7ee5`（2026-10-16，"CTL-1 on fm1-base"）的 `app/dsp/eas/`** —— 79/82 个文件 md5 与我们完全一致（2 个差异：`eas_host_fm1.c` 一处 `#ifdef` guard 名 `CT_HOST`/`PLAT_HOST`；`wt_200k_samples.c` 我们保留了完整原始文件、ctl 删了 16-bit 分支；eas_unity/config_unit/wtlib_unit 三个是我们新写的 unity wrapper）。再往上：charlesvestal/fm1-polly@`c0769dc6f5`（2026-10-08 "GM: a General MIDI sound module on Sonivox EAS"）引入，eas_wtsynth/filter/midi/public/wt_22khz/wt_200k_G/eas.h 与 polly@`c0769dc6f5` md5 相同；voicemgt 另含 polly 的 every-key 修复（`45041a5f23`），但**不含** Polly 的 CC74/71 滤波与 sample-accurate startDelay。最深上游 = **Sonic Network 2009 EAS 发行版**（`eas_build.h` GUID `1feda229…`、`_BUILD_TIME_ 0x4743badd` 与 AOSP `arm-wt-22k/host_src/eas_build.h` **完全一致**；wt_200k 系文件头 `$Revision: 960 $ 2009-03-18`）。**注意**：AOSP 的 `wt_22khz.c` 是旧的 1-program stub，**AOSP 从未带过 wt_200k bank**——bank 属于 Sonic 的 JET/EASEllerKit GM set（phoneME（Java ME）树里的同版本 `wt_200k_G.c` 与我们 md5 相同）。"从 Android 代码来"不准确：引擎骨架与 AOSP 同源同 build，但 200KB GM bank 和多个文件（eas_host_fm1.c 是 valerie 为 FM-1 写的 shim）非 AOSP 所有。
- **对比 CTL-1（John 实测其 drum 无 sustain 问题）**：结论对——是我们实现的问题，且已定位到唯一差异点。CTL-1 的键就是 press 发 `0x90`/release 发 `0x80`（fm1-ctl `app/ui.c` key_up:288），音序器每 step gate 1/8 音符后显式发 off（`engine.c` SEQ_GATE=3 tick）；引擎/bank 与我们同源（79/82 md5 一致，reverb/chorus 仅注释改名差异，32-bit clamp + tail fade 我们都有）。我们当初缺的只是 `ui/ui.c` 鼓格 `if (!down) return;` 这一行（r16j parity 移植时带进来的）——修复后行为与 CTL-1/Polly 一致。
- **melody"一直 sustain"的根因（2026-10-28，John 指出后实测定位）**：`app.c midi_in()` 里每条 MIDI-in note-on 同时触发 ① 模板 beep voice（`note_on`，单音方波预览）和 ② EAS **硬编码 ch0** 的 `gm_on(0,…)`。而引擎的 live auto-gate（2-cell）发的 off 走 `engine_drain → gm_off(选中轨的 channel)`——**beep voice 永远收不到 off**，ch0 的 EAS 声部与选中轨的 off 通道不匹配 → 按住键 = 永久 sustain。web 端 node 探针实测：held note 在 auto-gate 后仍 -13 dB 恒定；CC123（ch9 或 ch0）都压不住 beep。**修复**：midi_in 里删掉 beep 的 note_on/note_off 和硬编码 ch0 的 gm_on/gm_off（音符只走 ui_midi_dispatch → engine → drain，一条路、通道一致），保留 CC/PC passthrough。修后 web 实测：held note -10.8 → -45.0（auto gate 后）→ -59.9 dBFS，线上 on/off 成对。**补充**：序列播放（demo）的 melody 音符 on/off 本来就成对（host 8s wire 统计 on=32/off=31，差值=快照时刻未到期 gate），John 看到的 sustain 全部来自 MIDI-in/点键预览路径。
- **音质次要因素**：EAS 是 22050 Hz 引擎（`_SAMPLE_RATE_22050`），app.c 线性 2× 上采样。高频软、attack 钝是结构性的。真机可评 `_SAMPLE_RATE_44100`（RAM/CPU 代价未测）。Polly 音质好的可借鉴点（未迁移）：CC74/71 滤波（eas_filter/eas_wtsynth/eas_voicemgt）、sample-accurate note start（eas_wtengine/wtsynth）。
- **host 端注意**：EAS ch9（rhythm 通道）在**没有先发 PC** 时 note-on 不出声（-180 dBFS）；先 `midi c9 00 00` 后同一 note -49.6 dBFS。web 端无此现象（web boot 路径不同）。play/stop 回归：demo -14.1 dBFS ✓、stop 后 -48.6 ✓。
- **live auto-gate 移除（2026-10-28，John 拍板选项 A）**：hero 键盘按住 250ms 就被引擎 auto-gate 发 off（`ds_tick` live 分支 2×cell，所有轨默认 DIV_16=0.125s@120bpm），读起来像坏乐器。已删：live 音符按住=延音、松键=off（`ui.c` down/up → ds_live_on/off 不变）。兜底：漏 key-up（页面隐藏/焦点丢失）时 ds_stop 对每轨发 CC123 并清 live[]，不会挂死。回归：hold 600ms 期间 wire 只有 on ✓；key-up 后 off ✓；PLAY demo -14.0 dBFS、stop -48.3 ✓；web hold 2s 全程 -12.8→-29.5（EAS 自然衰减）+ 松键 -40.8、wire 期间只有 on、up 后 80 3f 00 ✓。
- **MIDI out 首次端到端验证（John，2026-10-28，web emu）**：浏览器 emu 经 Web MIDI out → TUS 的 SC-8850 模拟器正常发声。页面 Mute 按钮只关 app 音频（worklet→gain→destination 链上置 0），MIDI out 继续发——即"只听外部音源"的预期行为。**真机 USB-C/BLE MIDI 仍未刷写验证**（三项核心承诺之一仍纸面）。
- **K3 solo 三态滑条（2026-10-28，John 定"左=ON 右=MUTE 中间宽段=SOLO"，完成）**：K3 = 每轨一条虚拟 10-detent 滑条（`ui.c` k3_pos[]，端点死停不循环）：0=ON、1..8=SOLO（宽中段好盲摸）、9=MUTE。`k3_sync` 在滑条与轨状态不一致时（如载入带 mute 的 pattern）重新对位；SOLO 不写盘（pack.c 不含 solo 字段，性能手势 live-only）。引擎 `ds_state.any_solo` 每 tick 重算，track 循环里 `any_solo && !t->solo` 跳过（并集语义：solo 的几条都响，其余全静；被 mask 轨已 scheduled 的 gate off 照常发，不挂音）。hero 轨行加 "S" 徽标（与 M 同橙色）、旋钮读出 ON/SOLO/MUTE。GLO+K3=DIVISION 不变；EDIT 页 K3=PROB 不变。回归（host）：solo 空轨 -57.3 dBFS、回 ON -13.8、solo trk0 时 wire 只剩 ch9、死停 -20 det 不越界；（web 探针 tools/webk3solo.mjs）solo 空轨尾音衰减后 -60.9、回 ON -11.4。
- **固件首包（2026-10-28）**：真机 target 构建通过——`build/driftseq.fwsc` 611,496 B（md5 223accbcdf0b6c5a892c0d5d846d541f，identity FM-1_9200），bin 502,276 B，RAM .data+.bss 68,580/98,304、pool 254,696/344,064。含 BLE MIDI + central + 力度/压力，不含 WiFi/USB audio/CPU2。修 `eas_inc/string.h` 声明（unsigned 而非 unsigned long，匹配设备 libc）。**尚未刷写**。
- **真机首刷（2026-10-28，John 用 M-Flasher 直写）**：刷写正常，PLAY 出 demo 声，hero 弹奏正常，**BLE MIDI 实测工作**。
- **屏幕闪烁修复（2026-10-28）**：现象=grid 下半部（drum 卡片 KCK/VEL 行、melodic 四 voice 行）+ footer 顶部每帧闪，不做操作也闪。根因**不是撕裂**：每个屏幕第一个 painter 的 `cv_blit(0,0)` 发整张 canvas，而 `cv_begin` 刚把 cv_px 清黑——edit panel（melodic y112..211）/drum card（y115..214）/footer 顶 8 行被黑一帧再被各自 painter 重画。修=gfx.c 加 `cv_blit_band(x,y,rows)`，grid/home painter 只发自己拥有的行（melodic 112 / drum 115 / hero+arp 216），每帧 DMA 107KB→~64KB。commit：driftseq-fm1base（ui 两文件）+ fm1-ctrl/platform 934e40d 后（gfx.c）。新包 `build/driftseq.fwsc` md5 e8bedb33113c54aa49e458c79c0e27bc。**待重刷验证**；playhead 移动时可能残留 1px 撕裂线（面板无 TE 线，全帧 blit 19.5ms > 16.7ms scanout，数学上不可避免）——修后看是否可感再议。
- **USB MIDI 修成（2026-10-28，John 真机实测）**：重刷 **8412daad**（CIN 修复包）后 USB-MIDI 收到 note on/off ✓（7a529754 只是缩短了 log 诊断行，非验证包）。诊断线数据链：`usb up 1 cfg 1 rst 01 su 0D tx 13`（SysEx 应答走 EP1 成功 → 通路证明）+ 音符 CIN 0 被 macOS 丢 = 最后定案 app 层。注意 macOS 对 CIN 0（misc）包直接丢弃、SysEx/0x03 宽容——**任何 base 应用组 USB-MIDI 事件包 byte0 必须给 CIN（=status 高半字节）**，plat_host 的 `mk_pkt` 是参考实现，`expect_out` 掩掉 byte0 比较是探针盲区（未修，留观察）。
- **jam SAVE 环形次序修复（2026-10-28）**：John 实测 merge/replace 功能通但次序错——save 必须存"最后的 64 个 step"（按演奏时序），不是 ring 物理布局。机理：capture 写在 `jam_abs&63` 然后前进，wrap 后最老保留 step = `(jam_abs+1)&63`（playhead 的下一个受害者）；旧代码从物理 slot 0 直接照搬 → 迟到的 overdub 回到 loop 开头。修 = merge/replace 都从 `(jam_abs>=64) ? ((jam_abs+1)&63) : 0` 起 k=0..63 顺序走（未 wrap 时锚定 slot 0，保持听到的位置）。host E2E 验证：A(slot 60)/B(slot 3) 存后 A 在前、B(最后按的)落 loop 末尾；demo 回归 ✓。commit a4b6ab5。
- **SCALE 页 v3.2（2026-10-28，John：C# Maj 的 C 没灰 / A min 白键错灰 / 黑键灰看不出）**：三个问题三个根因。① **SC_SET 掩码错 5 个**（MIN 0x09AD 有大七度位 bit11 缺 b7 位 bit10 → A min 灰了 G；MIX/HMI/PMAJ/BLU 同错）——首次诊断时 diff 打印过这批错值但被一句巧合的 MAJ 验证带过，**教训：批量常量必须逐个验证，一个过不代表批过**。② C# major 的 C 白键：pc 语义下 C=B# 调内（教科书也点亮 C/F），但 John 要**调号语义**——改成拼写制：7 音级 scale（popcount==7）每级一个字母、从 tonic 约定字母（表 [C,C#,D,Eb,E,F,F#,G,G#,A,Bb,B]）强制升序走，key 点亮 = 字母+音高双匹配（黑键取左右两个字母都算，D#=Eb）；C# maj → 11 黑键全亮、16 白键全灰 ✓。五声/布鲁斯（无调号可言）保持 pc 语义（C BLU 的 F 和 F# 都亮——都听得对）。③ 调外黑键 C_LINE(32)→**C_DIM(74)**：0 vs 74 与 255 vs 150 同等可辨。中间走过贪婪找字母（|acc| 最小）弯路——C# maj 会拼出 D# E# F F# G# A# C（F、C 自然音 |acc|=0 反而赢），**调号拼写是五度圈约定，不是最少变音记号**，强制升序字母才对。像素验证三张截图。commit `e39f0d6`；wasm `b1724727`、fwsc `e06b34ba`。
- **SCALE 页 v3.1（2026-10-28，John：C major 灰了两个白键）**：keybed 图示 pc 计算 bug——key id 是从 F3 起的半音序（id 0 = F3 = pc 5），正确 pc = (k+5)%12，painter 错写成 k%12，所有 pc 平移 5（B 被当成三全音灰掉、F# 反而保持调内色）；tonic 描框同 bug。同时把灰 mask 查表改为 **tonic 相对**（(pc-tonic)%12），K2 转 tonic 灰键图案正确跟随。像素级验证：C major 16 白键全亮+11 黑键全灰 (32,32,41)；D major F/C 白键灰、调内黑键 (0,0,0) vs 调外黑键 (32,32,41) 对比可辨。**教训：vision 模型会顺着提问词确认错误断言（v3 首轮 C minor 截图我提示词写"E/A/B 应灰"它就答 yes）——色彩类验证必须用 PIL 像素扫描**。commit `baa0b67`；wasm `9c675d53`、fwsc `276661b0`。12 格 interval strip 删除。keybed 图示直接显示 scale：调内键 = 真钢琴色（白亮/黑暗），**调外键灰掉**（白键灰 C_GRAY、黑键暗灰 C_LINE）；tonic 音高类在键盘上每个出现处（C4+C5）加琥珀描框。CHROMATIC 档全键正常（无灰）。K1 转动 = 灰键图案实时变。host 截图验证（C minor：E/A/B 白键灰、F#/G#/A# 黑键暗灰、C4/C5 框）。commit 见 git log；wasm `675284a4`、fwsc `c11e86bd`。**K3 OFC 开关改死停**（John：不循环，左扭 off 右扭 on）——与 K3 solo 滑条同一手感语言；探针验证 right×2 不变、left×2 不循环；wasm `d2148bf9`、fwsc `de094d9b`，commit `999dc5b`。
- **SCALE 页 v2（2026-10-28，John：黑白键排列 + 三行全称）**：三行设置全称（SCALE MAJOR / TONIC C / CHORD KEYS OFF，SC_FULL 全名表 MAJOR…CHROMATIC，SC_NAME 缩写表删除）。keybed 图示改真实钢琴排列：16 白键一排、黑键骑在相邻白键边界上（2+3 分组），区段括号在键下（DEG=白 0..3 / ROOT=白 4..10 / CHORD=白 11..15，**括号 x 坐标必须用白键序数算，不能用 key id**——19*13=247 出屏的教训），held root 琥珀高亮，成分图例两行四列。PNG 像素级验证括号 x 范围（14..63 / 66..154 / 157..219）。commit `db1ea11`；wasm `6eda7d97`、fwsc `fda13cdd`。
- **级数走位（2026-10-28，John 定左手 7 键）**：左手 7 键（key 0..6）= I..VII 级数。root+和弦按住时按下级数键 → **和弦根音沿 scale 走 + 性质按堆叠三度重算**（大调 I=maj ii=min iii=min IV=maj V=maj vi=min vii=m7b5），抬键回原 root（suspension 手感，John 认可）。右手成分键=修饰：MAJ/MIN 强制三音、DIM 降五音、SUS4 换四音、7/M7 加七音、6 换五音、ADD9 加九音；**修饰音不 snap**（故意半音，如大调 ii+MAJ=D F# A）。**级数走位是 live-only**（pattern 只保留锚点和弦）。keybed OCT± 键废除——面板 OCT± 按钮全局移调整个 keybed（root 区吃 key_shift）。SCALE 页加了 keybed 图示（三区着色+held root 高亮+成分键图例+级数提示行），**页面即说明书**；修了 draw_scale 忘调 ui_draw_footer 导致的 footer 残影。host 探针验证：C maj → IV=Fmaj7（scale 自身七音，**注意 IMaj7 是调内正确结果**）→ ii+MAJ=D F# A → 抬键回 C maj，diff 应用全对。commit `aa947a0`；wasm `e16453a4`、fwsc `579f8b14`。
- **SCALE 页 + 单指和弦（2026-10-28，John 拍板组合键方案）**：SEL 进 M_SCALE（K1 scale 类型 MAJ/MIN/DOR/MIX/HMI/P.MAJ/P.MIN/BLU/CHR，K2 tonic，K3 OFC 开关）。**snap 在 input 时做**（hero=数据，play 时 snap 会让格子说谎），鼓轨与 EDIT 层不 snap。OFC 键位分区：key0/6=OCT∓（root 区八度）、7..18=12 个半音 root（C4..B4）、19..26=8 个成分键 MAJ MIN DIM 6 7 M7 SUS4 ADD9（物理 ≤3 键）。和弦=槽位表（third/fifth/7th/9th），无效组合静默（7+M7、MIN+DIM、6+7th），5 音降 5th（爵士惯例）；**voice diff 应用**（sus4 换三音会真发 note-off）；root 按住右手换组合实时跟 live+jam；GRID 写入只在组合有效时发生一次（整格替换+cursor+1）。live-only 不 pack（K3 solo 先例）。host 探针 tests/scale.txt 全验证（F9 降 5th、sus4 diff、snap tie 取低、无效组合静默、state readback ofc_mask/step0 全对）。commit 见 git log；wasm `bdf2f19e`(372,413 B)、fwsc `0f48d27b`(611,547 B)。注意 host 探针 harness：相邻 key 边沿无 wait 时 out dump 会少帧（时序伪影，间隔 ≥1 帧即正常），expect 比较本身坏（"== =="）但 GOT 值可信。
- **断链即静音（2026-10-28，John 真机报 + 猜中方向）**：真机上只有 USB 或 BLE 连着才有声，BLE 断开立刻没声、USB 断开过一会儿没声；屏幕/playhead 正常。根因：`plat_midi_out_room(TO_USB|TO_BLE)` 在**所有请求链路都 down 时返回 0**（每个 down 链路不参与 room，全 down 塌缩成 0），而 `engine_drain` 用它做 `if (!room) break`——room=0 时连 **GM 喂入**都停了（engine_drain 是 EAS 的唯一入口）→ 引擎照走、合成器饿死。BLE 断开 `ble_midi_out_room` 立即归 0（"0: none is"）→ 秒静；USB 断开等 SIE 报出 bus reset 使 `usb.config`→0 → 稍后静。**修**：drain 不再因 room=0 停——GM 恒喂、事件恒消费，只有 wire 发送等 room（平台 send 本身在满环时丢弃，断线无听众）。首刷时能单独响是因为刷完 USB 线还插着（macOS 已配置）。commit 见 git log。新包 fwsc `dddc1303`、wasm `3fea956d`。附带 APP_LOG_HOOK：remote log 尾部多一行 app 状态（render 心跳/EAS 块数与峰值/MASTER），将来"为什么不响"一条 log 就能定位。
- **jam take = current pass（2026-10-28，John 第二轮纠正）**：逐音 replace 仍会留旧 take 残影（CEG 后 wrap 弹 ABAB → G 留存、同 slot 叠音）。John 拍板：**ring = 一遍 take（current pass）**。实现：`jam_slot_pass[64]`（每 slot 最后写入的 pass 号 = `jam_abs>>6`，64 B RAM，不 pack）；capture 命中旧 pass 的 slot → **整格清**再写；SAVE（merge/replace）只取 current-pass 的 slot。pass 从 slot 0 开始 → slot 序 = 演奏序，**取代 a4b6ab5 的旋转**（rotate 代码已删）。epoch 字节 256 pass（16,384 步 ≈ 34 min @120bpm）回绕一次，超老 slot 可能误判 fresh 一次（无害，注释已记）。`jam_rehit_test.c` 重写 8/8（pass 边界整格清、同 pass 和弦 mate 存活、MERGE 跳过 stale、滚奏替换）；jam_test/golden/tie/pack 全绿。新包 fwsc `05732c5b`、wasm `fc285826`(366,594 B)。**遗留 spec 疑问（John 未定）**：driftseq.h:172 说 "PLAY from stop calls ds_jam_reset"、ds_play 注释说 "phase re-anchors"——代码两者都没做，jam 相位与 bar 线开机后各自漂；要不要在 ds_play 重锚待定。
- **预览合成器修复（web/emu/index.html）**：① note-off 查表 bug——voices 以 note-on 的 status（0x90|ch）为 key，0x80 off 查 0x80|ch 永远 miss → 方波永不停止（John"Message 栏有 off 但音不停"的真正元凶）；② 该 app 自带 GM，`PREVIEW_NOTES=false` 预览不再播 note on/off（CC/PC 仍回显），无内部音源的 app 可重开。build 戳改为 build.py 自动盖章 `{{BUILD}}`（`dev · preview off for GM notes 2026-10-28 (sustain fixes)`）。
- **web emu 服务器规范（2026-10-28 定）**：一律用 `tools/nocache_server.py <build/emu 目录> <端口>` 起服务（发 no-cache 头 + 静音日志——裸 http.server 的 stdout 管道写满会把服务器卡死，8933 旧进程就是这么僵的，John 由此测到旧构建）。当前 8933 = no-cache 服务器。
- 待 John 定：鼓 audition 的修法（键抬起发 off / 鼓轨固定短 gate / 别的）。→ 已定已修，见上。

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
