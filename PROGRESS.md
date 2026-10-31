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
- **和弦名改简写记法 + FONT_M 小写（2026-10-29）**：①`fm1.toml font_full=["M"]`：FONT_M 从 95 字符扩到 126（小写），flash +33,602B（image 507,324→513,356；slot 581,564，余 68,208）；②名字改演奏者简写 `C/Cm/C7/CM7/Cm7/C6/Csus4/Cdim/Cdim7/Caug/Cadd9/Cmadd9/C13/CM13/Bm7b5`；③由查表改**规则生成**（40 种根音路径可达集合全覆盖，实测 sono 一致：Cdim+7→Cm7b5、Cm+6→Cm6、deg vii+7→Bm7b5）；④音名按功能拼写（Cm = C Eb G 而非 C D# G）。tests/scale.txt 全绿（ofc_name 期望更新为 CM7）。wasm `081aea9f`、fwsc `25a08fb0`。
- **和弦名行放大 + LED 档位核实（2026-10-29）**：①和弦名行改 FONT_M(24px) 主显、音名 FONT_S(16px)；宽度用 text_w() 实测（非估算），超 232px 自动降级到 FONT_B+FONT_T；②核实硬件 LED 只有三档（plat.h: 亮 / mid ~12% / dim ~8% 两档），不可连续调；OFC 用「亮/dim」两档。wasm `5cdf78bf`、fwsc `6db9c627`。
- **旋律轨步进录音在 CHORD KEYS 关闭时无效（2026-10-31，feat/step-record-ofc，docs/UI_GRAMMAR.md §6）**：John「melody grid的step record有问题，只有在ofc on的情况下step record才有效」。报告准确，影响面比报告更大。**根因**：`scale_key_hook()` 在 `panel_key_handler()` 最上层，OFC-off 分支只试听不写字，`return 1` 把键完全吃掉，GRID 的 stepEntry 永远收不到；而 **OFC off 是开机默认**（`ui_edit.c:749` 置 `sc_ofc = 0`），所以从开机起每条旋律轨都是坏的。**同一分支还漏两道门**（OFC-on 分支也漏了 ui_mode 那道）：①`focus_layer` → EDIT 层在 OFC off 时整层失效，voice 加不了删不掉；②`ui_mode` → TRACK SET（试听所选 PROGRAM 的页面，其注释明写"录音会把这页塞满你只是用来听的音"）在 OFC on 时照写。两道门现在提到 hook 顶部，与 OFC 分派无关，不会再分叉。**写入时机**（John「选3」）：一个手势一格，**最后一个键抬起时写** —— 按下时写不出和弦（第二个音已经晚一格）；抬起时写让同时按住的键自然成为一格，不需要开 CHORD KEYS；两种情况下听到的都是实时试听，写盘时刻在演奏中不可见。写入契约复用 GRID 的 stepEntry（覆盖该格、复活 gate、cursor+1 死停），放在 `sc_off_write()`。**两个集合两件事**：`sc_held[]` 是页面命名的音（发声音高），`rel[]`/`cap[]` 是要写进格子的音（按下顺序）——snap 到已按下音高的键是同一个音不是一个新按键，不能吃掉一格。**实测**：`tests/steprecord.txt` **49**（含反向验证：pre-fix 16 失败）、`tools/steprecord_probe.mjs` **7** 像素项（同样反向验证：pre-fix 2/7 失败）、host 全量 **656**、引擎 5 套、golden 不变、九探针全绿。image **527,472**、RAM 68,728、fwsc **`fd793132263c48fdaed5197909d54c6f`**。
- **TRACK SET 第二页：DIVISION + CLEAR TRACK（2026-10-31，docs/UI_GRAMMAR.md §5）**：John「track set页面需要增加两个项目：1 change div，现在的div修改位置不是很明显，增加一个更明显的修改方式。2 clear track，这个危险操作需要弹出确认。页面可能容纳不下，需要第二页」。**DIVISION 不是新功能，是变显眼**：它原来只在 GRID/HOME 的 `GLO+K3` 和弦上（John 原话"没人找得到"），而且那两页页脚本就写着 `DIV` 指向一个修饰键；现在它是页 2 的一行，和页 1 五个身份行同尺寸（FONT_S 标签 + FONT_B 值，x100 值列，实测值列 199 px ink）。**CLEAR TRACK 只清音符**（John 拍板「清一个轨不该把轨静默搬到 ch1 / prog0」）：`ds_clear_track()` 是对的实现的反面——它 memset 整个 ds_track，channel/program/division/route/voice/swing 全回出厂值，轨会静默跑到 ch1 弹钢琴。所以按名字清音符字段（voice note 0xFF、prob 100、cond none、on/tie 0），euclid 骨架 eu_on/k/n/rot 一并清（否则 `ds_euclid_fill()` 会把旧骨架重画回来），**正在响的音不动**——它们各自的 gate-off 早就在 `eng.gates[]`（按 track 索引），会按时结束，panic 反而切出一个可听的咔哒。**弹窗通用化**：原 `jam_menu` 变成 `pm_kind/pm_sel` 单一装置（`PM_JAM`/`PM_CLRTRK`，`jam_menu` 保留为宏别名，JAM 侧代码一行未改），`popup_draw()`/`popup_act()` 按 kind 分派；CLEAR 弹窗**开在 NO**、两项**死停**（不许从 NO 绕过 CLEAR，JAM 的四项仍循环）、K2 左拧不打开任何东西（"一个刚在试探旋钮的手，左拧就武装了一次擦除"是最坏的意外）。页面说规则（清什么、留什么），弹窗说账单（哪一轨、多少步）——同一句话不说两遍。**实测**：`tests/trkset2.txt` **111** 条（含非默认 PROGRAM 85/VOICE MONO/ROUTE BLE 清后逐项保留——track 0 的 program 本就是 0，单靠它抓不到"只重置 program"的擦除）、host 全量 **592**、引擎 5 套、golden 不变、`tools/trkset2_probe.mjs` **24** 像素项（页 2 是 1492 px vs 页 1 1860、header 239→280、取消后 y216 以上逐字节相同）、其余九探针全绿（`trkset_probe` 23→25，加"页 2 行数少于页 1"）。image **527,472**（+1,584）、RAM 68,728、fwsc **`fd793132263c48fdaed5197909d54c6f`**。真机仍 `8412daad`，现攒 **6** 个提交未刷。
- **页面键统一（2026-10-31，docs/UI_GRAMMAR.md §4）**：John「页面打开逻辑也不一致：现在home按钮是在hero和grid间切换，arp和seq是进入页面，再次按无效，env和scale都是在相应页面和hero间切换，比较混乱。统一改为按钮就是进入相应页面，重复按进入同一功能的不同页面…和glo+home的逻辑也一致」。改前四种行为并存：HOME = HOME↔GRID **切换**、SEQ/ARP = 进入且再按**无效**、ENV/SEL = 页面↔HOME **切换**、GLO+HOME = 进入且再按**翻页**。现在一条规则：`ui_page_key(mode, cur)` —— 不在该页就去，已在就 `ui_page[mode] = (ui_page[mode]+1) % ui_pages[mode]`；`ui_pages[]` 全为 1 所以今天一律停留，GLOBAL 为 2 所以仍是翻页。**GLOBAL 不再是特例**：`gl_page` 变成 `ui_page[M_GLOBAL]` 的宏别名，它自己的代码和测试一行未改。**HOME 保留双向含义**（John 已拍板）：别的页按 HOME = 回 hero，hero 上按 HOME = 停留；GLOBAL 页独占 HOME 所以它仍能关页。**顺带改掉一个旧设计**：打开 GLOBAL 原来强制回第 1 页（注释理由「opening never lands on page 2」），现在**记在离开时的那页**——第 2 页是扫描列表，看它就开始扫，玩家在那页走开再回来却落在设置页得再翻一次，不成立。实测：`tests/pages.txt` **46** 条新断言（每颗键到自己的页、再按停留、HOME 从每页都能进能出、GLOBAL 页号跨往返保留），host 全量 **479**（+46，`global.txt` 97→98 并显式标注起始页），引擎 5 套、golden 不变，八探针全绿；改了三处探针的「离开用 SEL/ENV」为 HOME，`preset_probe` 的 GLOBAL 页墨量断言改为不钉页号（rule 4 允许它记住）。image **525,888**（+288）、RAM 68,728、fwsc **`ce17552e5c566f22f9988a1ccc0ad251`**，已拷进 `build/emu/`。真机仍 `8412daad`，现攒 **5** 个提交未刷。
- **UI 语法统一（2026-10-31，docs/UI_GRAMMAR.md）**：John「设定页面k1 row，k2 value，所以ble连接不应该用save按钮，而应该用k2来连接/断开。多个选项统一用类似preset的POPUP，用OCT+确认、oct-取消」。三条规则：①设定页 K1 选行 / K2 改值（动词也是值，所以 BLE 连接=K2 右拧、断开=K2 左拧）；②多选项一律 POPUP，K1 选、**OCT+ OK / OCT- Cancel**，SAVE **彻底不参与确认**；③SAVE 只剩开菜单。**JAM 菜单重做**：原来是页脚条（24px 五列，highlight 是反色小方块）+ 3 秒自关 + SAVE 确认；现在铺满屏幕（同 PRESET popup 的apparatus）、四行 FONT_M、K1 循环选、OCT+/OCT-、**无超时**。**同时修掉三个真实缺陷**：(a) 光标在 BLE 列表行数收缩时被钳到 row 0（开关行），玩家本想断开、下一格 detent 却关掉了蓝牙 —— 钳位下限改为设备行存在时的 1；(b) HOME 取消弹窗在 GLOBAL 第 2 页失效，因为 `global_button` 排在 `jam_menu` 之前把 HOME 吃掉了 —— 弹窗判断提到 `ui_button` 最前（弹窗优先于页面）；(c) `jam_menu_ttl`/`JAM_MENU_FRAMES` 整套删掉。实测：像素扫描弹窗覆盖 52597/57600 px、取消后与打开前**逐字节相同**、页脚三段文字 8-36 / 104-135 / 176-211 不重叠；`tests/jam.txt` 37 条断言（含 `jam_menu`/`jam_sel`/`key_shift` 三个新读数），`global.txt` 91→97，`global_probe` 52→54。**未处理**：`platform/device/unity.c` 的 `RESTORE FROM USB? SEL yes / HOME no` 属平台代码且先于 app 上屏，已记入 spec 的 open items。
- **SCALE 页 OFC 关闭时的音名（2026-10-30）**：John「关闭和弦的时候，弹奏也希望能显示音名，如果能组成和弦也要显示出来」。原来 OFC 关只做 snap 放音、屏幕零反馈（y186 那行是空的，只有 OFC 开才画 Active Chord）。现在按住键时把**实际发出的音**收集成集合，排序后走与 Active Chord 同一套命名规则（`chord_name_of` / `tone_name_of` 已参数化成接受任意音集），行内先和弦名后音名，松手即消失。单音只写音名；纯五度写 `C5` 而不是 `C`。**实测**：CHROMATIC 下 C-E-G→`C`/`C E G`、C-Eb-G→`Cm`/`C Eb G`、C-Eb-G-Bb→`Cm7`、C-G→`C5`；像素扫描 y189..204 出现 278 px、松手后与基线**逐字节相同**（`tests/scale_names.txt` 26 条断言）。**自然音阶下黑键弹不出变化音**（C MAJOR 里 id 10 = Eb 被 snap 成 D），所以真和弦要在 CHROMATIC 下验证 —— 这是 snap 的既有行为，不是缺陷。GRID 页 header 的 `chord_name(sel_track, cursor)` 未动（John: grid先保持不动）。
- **SysEx 打通（2026-10-30，emu 侧补齐）**：John「先补奇emu」。三条线（log / MMC 走带 / pattern backup）共用 SysEx，emu 是唯一能自动化验证的地方，所以先修它。**四处真实缺陷**：①`web/emu/worklet.js` 出向按 **status 字节**猜长度，SysEx 第二包起是三个数据字节、没有 status，一律被截成 1 字节 → 整个帧烂掉；改为按 packet 自己的 **CIN**（byte0 低四位）查 `CIN_BYTES` 表；表最初写错（8..11/14 填 0，应为 3），被 `worklet_midi_probe` 当场抓到（note-on/note-off/pitchbend 三条）。②`web/emu/index.html` 入向 `if (st >= 0xf0) return;` 直接丢 SysEx；改为组帧 + 按 CIN 5/6/7 打包，含**跨事件分片**（`sxIn`）与**帧内 F8 clock 过滤**（真实端口会在帧内插时钟，`tools/emu_sysex_split_probe.mjs` 抓到）。③host/emu 平台侧 `sx_take()` 的 `b++, k--` 在「F0 独占一包」时 `k` 归零仍读 `b[k-1]`（越界）；补 `if (k == 0) return;`。④`app_sysex()` 对所有 device id 都应答；探针问 id 2 也回 → 一台机器在扫描器眼里变成 128 个身份。改为只答 `7F`(all call) 与 `7D`(自己)。**emu 构建走 `emu_web.c`，它 `#include "plat_host.c"`** —— 同一份组装代码，host 脚本与 wasm 都生效，不是两套。验证：`tests/sysex.txt` **7** 断言（含 7D 也答、id 2 不答）；`tools/emu_sysex_e2e.mjs` **14** 项（把 `build/emu/app.wasm` 装进真 worklet，读固件自己的输出流：15 字节 identity reply、CIN 4×4+7、未知帧静默、CIN 5 短尾）；`tools/emu_sysex_probe.mjs` **26**；`emu_sysex_split_probe.mjs` **16**；`sysex_cin_probe.py` **18 帧**（2..13 字节逐一走完三种结尾 CIN）；`worklet_midi_probe` 13。host 合计 **361** 断言全过，引擎 6 套全过、golden `1DFFB74BC111252F` 不变。image 525,024 B（+48，`app_sysex`），RAM 68,692 不变，fwsc **`2c1f2d2794a9f9c240580efd2f7bb76e`**。**尚未刷机**（John「今天暂时不刷了」）。注意：`midi_reaches_fw.mjs` 有 2 条一直失败（note 电平 0.0007 < 阈值 0.005），**已用 git stash 验证基线同样失败**，与本次无关。
- **OFC 五项交互定版（2026-10-29，John 1b/2a + 按键规则 + 和弦名/键灯）**：①级数音高挂在 tonic（root 不再转调走位，OCT 管音区）：degree i 单按 = C E G、root G 持住 + i 仍是 C4 E4 G4、OCT+ 两次 = C6 E6 G6，实测 ✓；②OFC 下 root 不 snap（Eb 单按 = 3F 真 Eb，Ab = 44），OFC 关时整键盘仍 snap；③成分键规则：激活=整和弦重击+回写 GRID、取消=只停该音不回写（探针：ADD9 on 出 4 个 on，ADD9 off 只 80 4A）；④和弦名行：左侧和弦名（读回实际音程集，未知集打印 root+"?"）右侧音名，如 `Cmaj7 C E G B`；⑤键床标出和弦音（白键脚 5px、黑键脚 4px 琥珀条，sc_marks 位掩码），PIL 验证 Cmaj7 = keys 7/11/14/18。tests/scale.txt 全绿。wasm `64f6454d`、fwsc `353dd6e2`。
- **SCALE 页 UI 改版 + 级数三和弦（2026-10-29，John 四项要求）**：①和弦名行（y186 琥珀，AUG 提示下方）显示 Active Chord 实际音名 C E G / C E G B（PIL 验证 ink x7-37 三段/四段）；②K4 的 gate/latch 收进 K3：单一旋钮三档 OFF/ON GATE/ON LATCH 死停（K4 腾空）；③键盘灯：成分区+级数区 dim 常亮底、选中/走位键点亮（CHR 下级数区不点）；④**级数改三和弦**（bug 修复）：sc_degree_chord 由固定 1-3-5-7 改 1-3-5，7/M7 才加七音——C 大调 i=Cmaj ii=Dmin iii=Emin iv=Fmaj 探针全对；MAJ 只强制三度（ii+MAJ=D F# A=Dmaj 三和弦，不再是 Ddom7）。tests/scale.txt 全绿。wasm `c3cab596`、fwsc `78a013b6`。
- **开放问题两连定（2026-10-29，John 拍板）**：①CHR 级数键=方案 c（对象不存在，级数键在 CHR 下无效，和弦输入只剩 root 路径；后续考虑级数 7 键做转位——新议题未设计）②6+7/6+M7 支持（删 has6&&sevens veto，n=5 砍五音自然得出 C13(no5)=0,4,9,10 / Cmaj13(no5)=0,4,9,11）。probe 验证 + tests/scale.txt 补 CHR-dead 与 13-voicing 两节全绿。
- **OFC Latch 实现（2026-10-29，John 拍板"K4 切 Gate/Latch"）**：sc_latch（K4 右=LATCH/左=GATE，SCALE 页死停，K4 原空闲）；LATCH：root 松开不打断（gate 锁存）、成分松开无操作/再按 toggle 重击、级数松开不打断；K4 切回 GATE = 边界静音（防挂音）；停止 = K3 OFF 或切回 GATE；probe A/B2/C/D/F + tests/scale.txt LATCH 节全过（含 expect DSL 修正为 'expect X N'）；basic.txt 失败 = 旧 DSL token 遗留（先在）。wasm `260c9b98`、fwsc `174acb3b`。
- **OFC 评审重构（2026-10-29，John 书面评审 docs/OFC_SPEC.md）**：按评审实现锁存选择模型——sc_root_key/sc_mask = 持久 Selection（松开不清），sc_gate = 物理按住的 root（Gate 发音）；成分键改 toggle（再按亮键=取消回基础和弦）；按住 root 按另一个 root = 新 root 接管（旧和弦 off→新和弦 on）；root-down 也写 GRID step（单 snap 音）；K3 OFF 全清修挂音 bug；EDIT 层 bypass（hook 查 focus_layer，原来被 OFC 吞键）；C6 补五音 0,4,7,9；DIM+6=dim7 0,3,6,9（原静默忽略）；DIM+7 文档明确=半减七。tests/scale.txt 重写全过 + demo 回归 -12.0。开放问题（待 John 拍板）：①CHR 级数键=半音堆叠簇音乐性 ②Latch 停止手势（HOME/再按 root/K4）③级数在 Latch 下的行为 ④6+7/6+M7 维持静音。OFC_SPEC.md v2 + commit `749bf71`/`0d093e2`（已 push）；wasm `054799eb`、fwsc `d0ce22d0`。
- **OFC_SPEC.md（2026-10-29，John 要求全面审查）**：docs/OFC_SPEC.md——键位三分区、级数走位、root/成分状态机、oc_resolve 组合表（含 MAJ+DIM=Aug）、GRID 写入条件、SC_SET 掩码、状态生命周期表、8 条"奇怪但有意"审查清单。写时逐段核对源码，纠出文档初稿两错（级数同按=直接切换非忽略；MIN+ADD9=Cmadd9 非 Cm9）。探针验证级数直接切换（ii 按住按 iv → 直接 Fmaj7）。
- **Aug 和弦（2026-10-29，John 问 Aug 是否=6→否，拍板方案 2：MAJ+DIM 组合）**：Aug = 增三和弦 0,4,8（#5），与 6（0,4,7,9）完全不同。oc_resolve：DIM 分支拆 aug 变量——DIM+MAJ 位 → 三度回大（4）+ 五度 #5（8）；DIM 单按不变（b5=6）。图例下方加灰字提示行 "MAJ+DIM = AUG"（y174-181，与标签无碰撞）。探针：MAJ+DIM → C E G# ✓、DIM 单按 → C Eb Gb 不变 ✓。commit `1c3e10c`；wasm `e1f7b547`、fwsc `ff34f3e3`。
- **图例全名恢复（2026-10-29，John：ADD 实际是 Add9，空间够就写全）**：v3.8 摊开后槽位 41px，4 字符（14px）完全放得下——ADD→ADD9、SUS→SUS4 恢复全名。像素验证：ADD9 ink 151-169（中心 160=槽位中心），与相邻 DIM/6 间隙 28-31px。commit `6c51441`；wasm `641cce93`、fwsc `7c01c99b`。
- **松手不发声统一（2026-10-29，John 纠正：不是 DM 是 DIM，且立原则"松手不应该触发发声"）**：先前的"级数抬键回 root 和弦"与成分抬键 revoice 都是"松手触发发声"。统一为：**按下=打击发声（成分按下还全和弦 re-strike），任何抬键（root/级数/成分）= 静音**。探针：MAJ+C → DIM 按下全重击 C dim → 松开静音 → ADD 按下全重击 CMajAdd9 → 松开静音。commit `146cefc`；wasm `7c44e933`、fwsc `d9e3339b`。
- **OFC 手感两修（2026-10-29，John 报两问题）**：① "松开DM又发了一次CMaj"——他说的 DM = 左手**级数键 ii**（非 D 根音）。级数抬键原设计"回原 root 和弦"（aa947a0 拍板），耳朵读作幽灵和弦；改 = **级数抬键 → 静音**（root 和弦只在重新按下时发声）。② "C+Maj 时按 ADD 发出单音"——sc_apply 纯 diff，只发新增音（D5 一个音）。改 = **成分变化带 root 时全和弦重新打击**（sc_restrike：先全 off 再全 on）；探针验证 C maj + ADD → C E G D 四音齐发。commit `af5f2ab`；wasm `e27b8745`、fwsc `5a9917b4`。
- **级数走位反向 bug 修复（2026-10-29，John：右手按住不放，左手切换就不行了）**：根因 = root-up 处理器 `sc_mask = 0`——左手的 root 一抬键，右手仍按住的成分位被一并抹掉，下一个 root 落下时 oc_resolve(0) 只出根音单音。修 = root-up 保留 sc_mask（mask 语义改为"物理按住的成分键"，只在成分键自身抬起时清位）；root-up 照旧清 deg、清声。探针复现 John 原流程：MAJ 按住，root C4→G4→C4 连续切换，voice = C maj → G maj → C maj 全对；反方向（root 先按住、成分切换）回归正常。commit `cfeaca6`；wasm `b4d3902f`、fwsc `825306f1`。
- **SCALE 页 v3.8（2026-10-28，John：文字还是挤在右边互相重叠）**：钉键布局的数学死局——白键 13px 间距 vs 标签实测 10-14px（FONT_T 步进按字形 4-7px 不等，"M7" 14px），1-4px 间隙在手机缩放下读作重叠。**解法 = 把 CHORD 区的 5 个白键列映射到整条键床宽度**（208px / 5 = 41px 槽位）：黑键成分上排、白键成分下排，左右顺序仍沿键序，标签间隙 28-34px。ADD9→ADD、SUS4→SUS（≤3 字符进槽）。中间状态修了一个 `}}` 双大括号编译错。顺序对应键位靠"从左到右 = 键从低到高 + 黑上白下"约定。commit `50c6270`；wasm `f408664b`、fwsc `c6276464`。
- **SCALE 页 v3.7（2026-10-28，John：标签挤在右边重叠 + 键位要顾常用组合）**：① 标签钉键后白键 13px 间距放不下 4 字标签——加**列钳制**（标签 x 限制在自己键列 ±2px 内），像素验证无互相重叠（间隙 1-4px，紧但分得开）。② **键位按常用组合人体工学重排**（John：考虑常用组合的使用方便性；他此前定过"先做出来再看"授权）：新序 **MAJ 7 M7 MIN DIM ADD9 SUS4 6**（键 19..26 = C5..G5，W B W B W W B W 不变，只换成分）。理由：MAJ+7（属七，#1 常用）相邻、MAJ+M7 相邻、MIN+7 落在相邻两个黑键（D#5+C#5 夹 D5，一指滚按）、MIN+DIM 相邻（m7b5 半减七！大调 vii 级）、MIN+ADD9 相邻、ADD9 距 MAJ = C5→F5 自然手跨；代价：MAJ+SUS4 / MAJ+6 变远跨（SUS4 和 6 牺给了 MIN 侧）。实现 = 重排 OC 枚举 + OC_NAME（oc_resolve 全部按 OC_* 名字索引位，自动跟随）；tests/scale.txt 期望值 0x107/0x180A 语义不变（MAJ 仍 slot0、MIN+DIM 仍是 bits 3,4）✓ 回归全绿。commit `8a294a7`；wasm `9457ed55`、fwsc `076b2cf2`。
- **SCALE 页 v3.6（2026-10-28，John：黑键按下去亮的是 MIN 6 SUS4，图例却写着 MAJ MIN DIM）**：3+5 两排当初理解错了——John 的 3-5 = **3 个黑键 + 5 个白键**（C5..G5 = W B W B W W B W），不是"顶簇/其余"。成分键位定案（key 19..26 顺序）：MAJ(W) MIN(B) DIM(W) 6(B) 7(W) M7(W) SUS4(B) ADD9(W)。图例改为**标签钉在自己的键上**：黑键成分（MIN/6/SUS4）上排、各自黑键 x 处；白键成分（MAJ/DIM/7/M7/ADD9）下排、各自白键 x 处——钢琴黑键高白键低的空间语言。FONT_T 步进实测 5px（4px 估小了导致 MAJ/DIM 重叠）。vision 后端挂了（llm-prime 路由错误），全程 PIL 连通域分析验证标签 x 范围落键内。commit `8973459`；wasm `560dfad9`、fwsc `daf6d83d`。
- **SCALE 页 v3.5（2026-10-28，John：emu 上按 MAJ 文字不亮）**：成分键按下原本**要求 root 已按住**（`sc_root_key != 0xFF` 才置 mask 位）——emu 单指针无法同时按 root+MAJ → 单按 MAJ 毫无反应。改成：成分按下立即置 sc_mask 位（文字立刻亮、LED 亮），无 root 时不发声不写 GRID；root 落下时已有 mask 直接成和弦。ofc_mask 读回编码注意：无 root = 0x17F（mask+0x7F 占位）≠ 旧 0x7F（无 mask）。commit `b681d45`；wasm `ee2d0664`、fwsc `e32b65c9`。
- **SCALE 页 v3.4（2026-10-28，John：默认 CHR + 图例 3+5 + 按键亮字）**：① sc_kind 默认 SC_CHR（进页面不意外改音）。② 成分图例 4+4 → **3+5 两排**（MAJ MIN DIM / 6 7 M7 SUS4 ADD9），对应 CHORD 区物理簇（key 19-21 = C5 C#5 D5 顶簇、22-26 = D#5..G5），每排沿键床宽度均布。③ **按住成分键 = 对应文字点亮**（painter 用 sc_mask，LED 块跟随）；sc_last_valid 删除。**探针坑再踩一次：shot probe 忘了 `turn SELECT 3`**（demo 0-2 鼓轨绕过 OFC 层，key 事件被吞、ofc_mask 读回 127）——所有 SCALE 页按键探针的第一行必须是 turn SELECT 3。commit `f10fd3d`；wasm `5ea42b8c`、fwsc `afbe6c4d`。
- **SCALE 页 v3.3（2026-10-28，John 终审：B#=C 同键，点亮是对的）**：拼写制撤销，回到 **tonic 相对 pc 语义**（sc_spell_keys 整个删除，-58 行）——John 的推理：C# maj 含 B#，B# 和 C 共用白键，该键承载调内音，点亮正确。调外黑键 C_DIM(74)→**C_GRAY(148)**，与灰白键同族；A HMI 混合对照验证（G# 黑键 0 vs 其余 148 vs 白键 255，三级清晰）。**语义定案：keybed 图示 = 音高集合，与 snap 引擎一致（display=data）**。commit `6954528`；wasm `b1f13901`、fwsc `d5ccf619`。
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

固件构建（`build.py`，最后一次 = Track Settings 之后）：

```
image 524,528 B of 581,564   余 57,036
RAM   .data+.bss 68,612 B of 98,304   余 29,692
pool  254,776 B of 344,064            余 89,288
fwsc  611,547 B, identity FM-1_9200  md5 9a07cb18（未刷机）
wasm  401,952 B  md5 11cbc614（8930 HTTPS / 8933 HTTP 均在发）
```

Track Settings 一页的代价（实测，9807888 → 现 HEAD 两次真建）：**image 513,500 → 518,124 = +4,624 B flash**，**pool 254,760 → 254,776 = +16 B RAM**（`mono_note[16]` + gate/live 各 +1 字节的 `ch` 字段）。构成：128 条 GM 名表 + 9 条鼓组名 ≈1.5 KB、页面绘制 ≈1.2 KB、`ds_route_sync`+MONO+capture-channel ≈0.5 KB、探针与 hint 其余。

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
| golden | `tests_engine/test_golden.c engine/driftseq.c engine/demo_pattern.c` | `1DFFB74BC111252F` / 242 note-ons |
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

## 和弦命名（2026-10-29，唱名规则化 + 小写）

字体原本 CAPS ONLY（`felucca_font_t {h,pad,first,last}`，全部 role `last=95`，`glyph()` 把 a-z 折叠成 A-Z）。
`fm1.toml` 加 `font_full = ["M"]` → FONT_M 带到 126（+33,602 B flash），`C` `Cm` `CM7` 这种大小写对比才画得出来。

命名从查表改为规则（`ui/ui_scale.c: sc_chord_name()`）：按三度/五度/六度/七度/九度的有无与升降逐项拼后缀。
- 记法＝演奏者简写：`Cm` `C7` `CM7` `Cm7` `CmM7` `C6` `Cm6` `C13` `CM13` `C9` `Cm9` `Cadd9` `Cmadd9` `Cdim` `Cdim7` `Cdim(maj7)` `Caug` `Csus4` `C7sus4` `C9sus4` `Cm7b5` `CM7(#5)` `C6add9`
- sus 的扩展写在前面（`C7sus4` 不是 `Csus47`）；13 = `6+7`，maj13 = `6+M7`
- 名称来源是**实际发声音程集**，不是选择猜测 —— 名字与键床掩码永远一致
- 音名按功能拼写：`Cm` = `C Eb G`（此前是 `C D# G`，全域升号的表）
- 探针 `ofc_name`（djb2-16 哈希）+ `ofc_keys`（键床位掩码）**穷举 256 种成分键组合**：全部有名字，哈希与独立 Python 重算逐条一致（0 mismatch）

和弦行排版：名字与音名固定 12px 间隙；名字 `text_w()` 实测超 232px 降档 FONT_B+FONT_T（`Cdim(maj7)` 这类长名走降档，实测名 x8..108 + 音名 x123..175 不重叠）。

实测尺寸：image **513,500 B** / slot 581,564 → 余 **68,064 B**；fwsc 611,547 B。

## Track Settings 页（`9c3e89a`，2026-10-29）

入口 = **ENV 键**（`B_ENV`，此前无任何页面使用）。五字段全做 + MONO + global swing。

| 行 | 字段 | 范围 / 死停 | 页面绘制 |
| --- | --- | --- | --- |
| CHANNEL | `ds_track.channel` | 0..15（屏显 1..16），改到 9 即鼓轨 | `CHANNEL  10` |
| PROGRAM | `ds_track.prog` | 0..127 | 旋律轨：`039 SYNTH BASS 1`（自建 128 条 GM 缩写名表）；**鼓轨（CH=10）：`025 ELECTRONIC KIT`**（program 在鼓通道选的是鼓组，不是音色） |
| ROUTE | `ds_track.route` | USB / BLE / USB+BLE（**只管线缆**，GM 永远收） | `USB` `BLE` `USB+BLE` |
| ~~SWING~~ | ~~`swing_amt`~~ | **已移到 GLOBAL 页**（2026-10-30）：global×trk 是同一个数，放在轨道页读成了轨道控制 | — |
| VOICE | `ds_track.voice_mode` | POLY / MONO | `POLY` `MONO` |
| SWING | `ds_track.swing_amt` | 0..100 步进 5 | `100%` |

- **K1 移行**（死停 0..4）、**K2 改值**、**K3 仍是 MUTE/SOLO**（肌肉记忆）、**K4 = global swing**（0..100 步进 5）
- 没有 global swing 入口时 `swing_amt` 恒乘 0 —— 这是它此前全线失效的原因，现在 K4 直接写 `eng.global_swing`
- 下半屏 = **CHANNEL MAP**：16 格，实心=本轨，亮数字空心=别的轨占着，暗=空闲
- 底行只回答一个本页其它地方答不出的问题：**谁的 PROG 在这条通道上算数**（`2c21d00` 之后的 owner 规则，见下节）。本轨是 owner → `SHARED WITH T1 T2` / `NO OTHER TRACK SOUNDS`；本轨不是 owner → `T3 SETS THE SOUND`（或 `EMPTY, T3 SETS THE SOUND`）。空轨不算"共用"：11 条从没录过的默认轨和 bass 同占 ch0，但它们不出声也不该占通道的音色
- 顶栏 `h.page = 3`；**HDR_DOTS 5 → 6**（GLOBAL 占第 6 点）：点距 6px→5px，六点落在 211..236；6px 会算到 241 被切掉
- **MUTE/SOLO 改成单字母底标**（`hdr_t.badge` / `badge_c`，2026-10-30）：原先拼在 ctx 里成 `MELODIC MUTE`，同色同字号，状态读成了名字的一部分。现在是 **M / S 单字母药丸**，与 hero 列同一字形同一颜色（`HERO_MS = RGB565(224,160,64)`，两边共用一个宏）。药丸 = 一个字形 + 左右各 4px = 14px 宽，x194..208，页面点从 211 起。字母**画两遍**：`cv_text` 是覆盖率混合，5px 宽的 M 在亮橙上只叠到 60%，笔画变橄榄色糊在底里（vision 读成 "H"）；画两遍覆盖率平方，笔画核心落到 TB_BG、边缘留一个软像素
- **行数 5 → 4**（SWING 移走）：channel map 上移到 128/142..162，共享行 170
- **行字号 12px → 16px**（`TS_Y0 30` / `TS_PITCH 22`，标签 FONT_S、值 FONT_B），五行 y30..140，channel map 下移到 144/158..178，底行 186。量得每行墨迹 11px 高，值最右 x=216，不出边
## GLOBAL 页（2026-10-30，两页）

入口 **GLO+HOME**（按下沿），出口裸 HOME（松手沿），**两页之间也是 GLO+HOME**（同一个和弦，按一次翻一页，死循环两页）。第 6 个页面点，`h.page = 4`。**header 不再有药丸**：GLOBAL 不是某条轨的页面，没有编号可挂；页号写进 header 文字本身（`GLOBAL 1/2`、`GLOBAL 2/2`），用 `/` 而不是 `-` 是因为 2/2 读起来是"两页里的第二页"，不是"2 减 2"。John 2026-10-30 定：「header 前面的药丸 1 就不需要了」。

### 第 1 页 —— 全机三个数

| 行 | 引擎字段 | 步进 | 备注 |
|---|---|---|---|
| TEMPO | `eng.bpm` 20..300 | 1 BPM，GLO 时 10 | `ds_set_bpm` 之前**全仓零调用**，tempo 在此之前不可改 |
| SWING | `eng.global_swing` | 1，GLO 时 10 | 与 TRACK SET 第 5 行是**两个数**：`global × trk × 0.5` |
| GM SYNTH | `set.gm_on` → `glo_gm_on` | 开关 | 见下 |

- **K1 走行**（死停 0..2）、**K2 改值**、K3/K4 空
- 3 行 `GL_Y0 30 / GL_PITCH 34` → y30 / 64 / 98；标签与值都是 `FONT_M`（24px 字号，John: "字号还有空间就加大"）；band `cv_rect(4, y-6, 232, 25, RGB565(26,26,34))` + 左侧 2px 琥珀竖线 `cv_rect(4, y-6, 2, 25, C_AMB)`（与 TRACK SET 同一套，John 2026-10-30 "GLOBAL页面也要保持同样的风格"）
- 行下三条**只读**读数（y146 / 163 / 180，`FONT_T` 灰标签 + `FONT_S` 值）：
  - `CPU` —— `plat_cpu_pct()`，进 5% 才刷新（这个数是平台每 500 ms 量的，不是每帧）
  - `XRUNS` —— `plat_xruns()`，非零时值转琥珀色（`C_AMB`），这是本页唯一会变色的读数
  - `PATTERN` —— 实际字节数 + 折算扇区，如 `146/3840 1S`。**不是**"剩余 pattern 空间"：flash 里目前只存 settings（`OBJ_SETTINGS`），pattern bank 还没设计（`chain[64]` 全仓零读写），所以能报的只有"这个 pattern 要是存下来要占多少"。报一个并不存在的银行余量会是在说谎。测量走 `ds_pack_track()` **逐轨累加**（pattern 就是 16 条 track 记录的拼接），一秒一次；用整对象缓冲会是 3,840 B 的池，实际只要 1,040 B
- 最后一行注释 `TAP: RIGHTMOST KEY`（y200）

**为什么读数用"进 % 才刷新"而不是每帧算**：`ds_pack_pattern()` 要序列化整条 pattern，每帧调等于每帧做一次存档。

### 第 2 页 —— 蓝牙：开关 + 设备列表

John 2026-10-30：**"BLE的开关可以移到第二页，这样第一页空间大一点，第二页也不至于这么空旷"**。移过来之后，这一页和第一页是**同一套语法**：K1 走行，K2 改当前行的值，SAVE 提交。

| 行 | 内容 | K2 |
|---|---|---|
| 0 | `BLUETOOTH  ON/OFF` | 开关（`plat_ble_enable` + `set.ble_off`） |
| 1.. | 扫描到的设备名 + 右侧信号条 | 无（连接是 SAVE，不是一个数） |
| 1（占位） | 链接行：`btdev_name()` + `LINKED`/`LINKING` | 无 |

- 行几何：y34 + 18*i，band `cv_rect(4, y-5, 232, 17, …)` + 左侧 2px 琥珀竖线 `cv_rect(4, y-5, 2, 17, C_AMB)`
- **底行一个 `FONT_T` 行两件事**：左边是 radio 状态（`SCANNING` / `CONNECTING name` / `FINDING MIDI` / `CONNECTED name` / `RADIO OFF`），右边是 `SAVE: CONNECT` / `SAVE: LET GO`。原来状态行在列表**上方**，读起来像一行设备（"SCANNING" 像设备名），所以挪到下面
- 空列表时写 `NO DEVICES`（radio 开）/ `LOOKING...`（正在扫）
- 页脚：K1 = `DEVICE`，K2 = `ON/OFF` **仅当光标在开关行**；设备行 K2 留空
- 进页面自动扫描，离开即停（`ui.looking`）

**为什么开关必须是行，不能是页面顶部一条**：页面只有一套旋钮语法，K1 的主语在整页必须是同一个东西（这里是"列表"）。开关若独占一行但不在列表里，K1 就得在两种语义间切换，玩家要先想"现在 K1 走的是哪种"。

**链接行为什么要单独一行**：连上之后 btdev 停止扫描（`btdev_page` 只在 `BLE_DEV_OFF` 时维持 scan），`btdev_count()` 立刻归零。没有这行，光标会被挤回开关行，`SAVE` 再也按不到设备——**连得上，断不了**。测试把这个抓出来了（`SAVE` 连上后 `ble_dev==4` 但 `gl2_n==0`）。

### PRESETS 旋钮 = 当前轨的音色（John 2026-10-30）

**"现在的PRESETS旋钮还没有用上，这个旋钮应该是立刻选择当前track的音色的，旋转的时候屏幕要有提示，可以采取类似popup的样式，字体大一点，停止旋转后等待几秒popup关闭。可以用GLO+PRESETS加速"**

`EN_PRESET` 是面板第七个编码器（在 ALGO 和 K1 之间），此前 `ui_encoder` 对 `EN_ALGO || EN_PRESET` 直接 return。现在：

- **1 program / detent，GLO 按住 8 / detent**，0..127 死停。
- **即刻生效**：每个 detent 走 `ds_send_prog()`，所以停着也能听见（这是 TRACK SET 那条 bug 的同一机制）。
- **popup 占满屏**（`ui_preset.c`）：上行 `TRACK n` + 通道，中间三行——上下邻居灰、当前 `FONT_M` 白居中，底部在库两端时说 `START/END OF BANK`。
- **150 帧（≈2.5 s）无输入自动关**，每个 detent 重新计时。
- **鼓轨说 KIT 不说 voice**：PC 落在通道上，ch10 是鼓，`002 KIT` 而不是 `002 BRIGHT PIANO`。

**两处"重复信息"被砍掉了**（按"同一信息只出现一处"）：初版有独立的大号 PROGRAM 数字块，但名字里已经带 `040`；初版上行写 `4 VOICE`，会被读成"四复音"。现在是 `TRACK 4` + `CH 1`。

**尺寸实测**：128 个名字里最宽的是 `024 TANGO ACCORDION` = **218 px**（FONT_M，240 px 屏，左右各余 11）。`pz_wmax` 断言 <224 把这条锁住。

**GM_PROG 表搬到 `ui_tables.c`**：PRESET 和 TRACK SET 的 PROGRAM 行读同一张表，两份会漂。

### 页脚不能"画两遍"（John 2026-10-30）

**"GLOBAL page 2 scanning, 最底部有些闪烁，似乎footer和scanning重叠了"** —— 不是重叠，是**同一带每帧传了两次**。`draw_global2()` 先调 `ui_draw_footer()`（画共享的 `ROW EDIT` 并 `cv_blit(0,216)`），然后自己重画成 `DEVICE`、再 `cv_blit(0,216)`。host 只保留最后一帧所以看不出来，真机 LCD 就会先闪一下 `ROW EDIT`。

改法不是"少画一次"，而是**让共享 painter 知道自己在画哪一页**：`gl_page`/`gl_row` 移到 `ui_glue.c`（unity 里排最前，`ui_screens.c` 在 644、`ui_global.c` 在 648），`ui_draw_footer()` 直接按 `gl_page` 画 `DEVICE`/`ON/OFF`，第 2 页不再覆盖。

**实测**：同样 1408 ms，第 2 页 blit 数 **216 → 176**（差 40 = 2 次 × 20 帧的重复传输）。

### 试听要发 PC（John 2026-10-30）

**"track set的program change的时候，是不是没有发送PC，那么弹奏试听就无用了"** —— 对，PC 只在 `ds_tick` 的播放分支里发，而 `ds_tick` 在 `!playing` 时 `out_len=0; return;`。于是 TRACK SET 改 PROGRAM/CHANNEL 只改了引擎字段，合成器从没听见，试听弹的是**旧音色**。

新增 `ds_send_prog(s, trk)`：从编辑本身发，不等 tick（一个 detent 必须立刻听见），同时更新 `sent_prog[ch]` 让播放路径不重发。**CHANNEL 也发**——程序属于它移到的那个通道。

**实测**：改前该脚本**零 MIDI 输出**，改后 `C9 01 00`（ch10, prog 1）。测试锁死这条：`tests/trkset.txt` 88 断言，改前第 87 条就 "not sent"。

### 两态行 = 滑块，不是开关（John 2026-10-30）

**"第一页的GM，第二页的BLE on/off选择都没有做到盲拧效果"** —— 原来的代码是 `if (det) 翻转`，**方向被完全忽略**：左拧和右拧都翻转，手不用看屏幕就不知道往哪拧。这不是两个孤立的 bug，是同一个错误的交互模型。

面板本来就有一条既定规则：K3 的 MUTE/SOLO/ON 是一个**滑块位置**（`ui.c k3_state_slider`），两头死停，拧左就往左走。两态行应该服从同一条规则：

- 右拧（`det > 0`）= 要 ON
- 左拧（`det < 0`）= 要 OFF
- 已经在该档，拧那个方向 = **什么都不做**（不是"翻回去"）

统一成一个函数 `gl_want(on, det)`。两个后果值得记：

1. **GM 关着的时候，左拧不再重发 CC123**。旧代码每次拧都会 panic 一次——一个静音的合成器被反复 all-notes-off。
2. **BLE 那一处旧代码还写反了**：`gl_bt_set(set_ble_off() ? 0 : 1)` 在 radio 已经 ON 时再要 ON，开关永远关不掉（见上一节）。

全仓 `if (det)` / `&& det` 形态只有这两处，已经没有了。

### 一个 UI 陷阱：同帧释放两个键 = 误判离开

`ui_input` 按 `EN_K1..EN_K4、GLO、HOME` 的顺序**逐个**释放。GLO+HOME 这种和弦，如果同一帧里两个键都放开，先处理 GLO 会把 `glo_held` 清零，紧接着 HOME 的释放就被读成"裸 HOME 离开页面"——**翻页变成了关页**。

host 测试脚本把 HOME 的释放写在 GLO 之前，正好把这个 bug 藏住了；wasm 里按帧推进，暴露出来。

修法：`glo_held` 之外再加一个 `gl_chord` 锁存，在**按下**时置位（"这次 HOME 是被和弦按下的"），在 HOME 的**释放**消费它一次。

### 另外两个踩到的坑

- **`gl_bt_set(set_ble_off() ? 0 : 1)` 参数反了**：`set_ble_off()` 为 1 表示已存 OFF，问 `? 0 : 1` 等于"已经 ON 的时候再要 ON"——开关永远关不掉。正确是 `gl_bt_set(set_ble_off())`。
- **`const char *label = name;` 悬垂指针**：`name` 声明在 else 块内、`cv_text` 在块外读，第一次画第 2 页就 segfault。缓冲区提到循环层。

### PRESETS 旋钮 = 当前轨的音色（John 2026-10-30）

**"现在的PRESETS旋钮还没有用上，这个旋钮应该是立刻选择当前track的音色的，旋转的时候屏幕要有提示，可以采取类似popup的样式，字体大一点，停止旋转后等待几秒popup关闭。可以用GLO+PRESETS加速"**

`EN_PRESET` 是面板第七个编码器（在 ALGO 和 K1 之间），此前 `ui_encoder` 对 `EN_ALGO || EN_PRESET` 直接 return。现在：

- **1 program / detent，GLO 按住 8 / detent**，0..127 死停。
- **即刻生效**：每个 detent 走 `ds_send_prog()`，所以停着也能听见（这是 TRACK SET 那条 bug 的同一机制）。
- **popup 占满屏**（`ui_preset.c`）：上行 `TRACK n` + 通道，中间三行——上下邻居灰、当前 `FONT_M` 白居中，底部在库两端时说 `START/END OF BANK`。
- **150 帧（≈2.5 s）无输入自动关**，每个 detent 重新计时。
- **鼓轨说 KIT 不说 voice**：PC 落在通道上，ch10 是鼓，`002 KIT` 而不是 `002 BRIGHT PIANO`。

**两处"重复信息"被砍掉了**（按"同一信息只出现一处"）：初版有独立的大号 PROGRAM 数字块，但名字里已经带 `040`；初版上行写 `4 VOICE`，会被读成"四复音"。现在是 `TRACK 4` + `CH 1`。

**尺寸实测**：128 个名字里最宽的是 `024 TANGO ACCORDION` = **218 px**（FONT_M，240 px 屏，左右各余 11）。`pz_wmax` 断言 <224 把这条锁住。

**GM_PROG 表搬到 `ui_tables.c`**：PRESET 和 TRACK SET 的 PROGRAM 行读同一张表，两份会漂。

### 页脚不能"画两遍"（John 2026-10-30）

**"GLOBAL page 2 scanning, 最底部有些闪烁，似乎footer和scanning重叠了"** —— 不是重叠，是**同一带每帧传了两次**。`draw_global2()` 先调 `ui_draw_footer()`（画共享的 `ROW EDIT` 并 `cv_blit(0,216)`），然后自己重画成 `DEVICE`、再 `cv_blit(0,216)`。host 只保留最后一帧所以看不出来，真机 LCD 就会先闪一下 `ROW EDIT`。

改法不是"少画一次"，而是**让共享 painter 知道自己在画哪一页**：`gl_page`/`gl_row` 移到 `ui_glue.c`（unity 里排最前，`ui_screens.c` 在 644、`ui_global.c` 在 648），`ui_draw_footer()` 直接按 `gl_page` 画 `DEVICE`/`ON/OFF`，第 2 页不再覆盖。

**实测**：同样 1408 ms，第 2 页 blit 数 **216 → 176**（差 40 = 2 次 × 20 帧的重复传输）。

### 试听要发 PC（John 2026-10-30）

**"track set的program change的时候，是不是没有发送PC，那么弹奏试听就无用了"** —— 对，PC 只在 `ds_tick` 的播放分支里发，而 `ds_tick` 在 `!playing` 时 `out_len=0; return;`。于是 TRACK SET 改 PROGRAM/CHANNEL 只改了引擎字段，合成器从没听见，试听弹的是**旧音色**。

新增 `ds_send_prog(s, trk)`：从编辑本身发，不等 tick（一个 detent 必须立刻听见），同时更新 `sent_prog[ch]` 让播放路径不重发。**CHANNEL 也发**——程序属于它移到的那个通道。

**实测**：改前该脚本**零 MIDI 输出**，改后 `C9 01 00`（ch10, prog 1）。测试锁死这条：`tests/trkset.txt` 88 断言，改前第 87 条就 "not sent"。

### 两态行 = 滑块，不是开关（John 2026-10-30）

**"第一页的GM，第二页的BLE on/off选择都没有做到盲拧效果"** —— 原来的代码是 `if (det) 翻转`，**方向被完全忽略**：左拧和右拧都翻转，手不用看屏幕就不知道往哪拧。这不是两个孤立的 bug，是同一个错误的交互模型。

面板本来就有一条既定规则：K3 的 MUTE/SOLO/ON 是一个**滑块位置**（`ui.c k3_state_slider`），两头死停，拧左就往左走。两态行应该服从同一条规则：

- 右拧（`det > 0`）= 要 ON
- 左拧（`det < 0`）= 要 OFF
- 已经在该档，拧那个方向 = **什么都不做**（不是"翻回去"）

统一成一个函数 `gl_want(on, det)`。两个后果值得记：

1. **GM 关着的时候，左拧不再重发 CC123**。旧代码每次拧都会 panic 一次——一个静音的合成器被反复 all-notes-off。
2. **BLE 那一处旧代码还写反了**：`gl_bt_set(set_ble_off() ? 0 : 1)` 在 radio 已经 ON 时再要 ON，开关永远关不掉（见上一节）。

全仓 `if (det)` / `&& det` 形态只有这两处，已经没有了。

### 一个 UI 陷阱：同帧释放两个键 = 误判离开

`ui_input` 按 `EN_K1..EN_K4、GLO、HOME` 的顺序**逐个**释放。GLO+HOME 这种和弦，如果同一帧里两个键都放开，先处理 GLO 会把 `glo_held` 清零，紧接着 HOME 的释放就被读成"裸 HOME 离开页面"——**翻页变成了关页**。

host 测试脚本把 HOME 的释放写在 GLO 之前，正好把这个 bug 藏住了；wasm 里按帧推进，暴露出来。

修法：`glo_held` 之外再加一个 `gl_chord` 锁存，在**按下**时置位（"这次 HOME 是被和弦按下的"），在 HOME 的**释放**消费它一次。这是 `gl_opening` 的推广——原来那个锁存只保护"开页"这一个方向。

### 第 1 页的空间分配（John 2026-10-30 问"剩下的空间放什么"）

第 1 页原来 4 行到 y108 就没了，y120 到页脚（y216）之间有近 100px 空白。John 提的三个候选（tap tempo 说明、CPU 负荷、剩余 pattern 空间）里：

- **tap tempo 说明**：收成一行 `TAP: RIGHTMOST KEY`（y194）。不能再长——这一页其余部分是数值表，说明句一多就把表挤成注释。
- **CPU 负荷**：`plat_cpu_pct()` 确实有（`audio_cpu_pct` ← `fm1b_dbg.cpu_q12`，音频 block 里量的实际负载，约 16 block 平滑）。显示成 `CPU 47%`。
- **剩余 pattern 空间**：**没有这个数**。全仓唯一的存储对象是 `OBJ_SETTINGS`（`plat_store_save(OBJ_SETTINGS,…)`），pattern bank 根本没实现（`chain[64]` 零读写、`flash[16]` 只有清空循环）。能报的只有"当前 pattern 打包后占多少"，所以标成 `PATTERN 146/3840 1S`（字节 / 一个存储对象 / 需要几个 4 KiB 扇区）。**报一个并不存在的银行余量会是在说谎**。
- **XRUNS**：顺手加的一条。`plat_xruns()` 之前全仓零调用，音频欠载一次就 ++，这是"为什么刚才咔了一下"的唯一线索，比任何静态信息都值得占一行。非零转琥珀色。

**FW 版本号没放**：现在没有可显示的版本串。`APP_VERSION` 默认 `"DEV"`（`device/unity.c`），只有 `--release X.Y.Z` 才变成真版本，而 identity（`FM-1_9200`）是打包器写进 fwsc 头部的，app 运行时读不到。要放得先有一条"版本从哪来"的路（编译期 `-D` 注入日期/哈希，或读 flash 里的 identity），那是另一件事，不在这一页里顺手加。

**为什么读数在行下面而不是第五行**：它们不是设置，K1/K2 碰不到。用 `FONT_T` 灰标签 + `FONT_S` 值、行距 16px，视觉上和上面四条 band 行分开——band 是"能改的"，无 band 是"只读的"。

### 探针
`tools/global_probe.mjs` **52 项**：第 1 页三条行带（34px pitch）+ 药丸已消失 + 三条读数 + `TAP` 行 + 页脚无溢出；第 2 页开关行在 row 0、设备行 y52、信号条、底行状态 + SAVE 提示翻转、连接后链接行仍在、页脚 `ON/OFF` 只出现在开关行；和弦翻页、裸 HOME 离开；**以及 TRACK SET 的轨号药丸仍在**（`hdr_t = {0}` 的回归保护）。

探针坑：band 判定要"连续 10 行"，单行判定会把 header 底边当成 band（报过 "band at 20"）；band 高 25px、pitch 34px，相邻 band 重叠 9px，实测步进是 **32** 不是 34。

### 顶栏 6 点的坑
`dx = 211 + i*6` 在 i=5 时是 241，出 240 屏；改 5px 间距（211..236）。

### GM 开关 = 监听开关，不是静音
`app.c` 的 drain 里四个 `gm_*` 被 `if (glo_gm_on)` 包住：关掉只是**不再喂** EAS。但只停喂会挂音——关闭瞬间正在响的音，它的 note-off 排在 `eng.out` 后面，等送到时接收端已经不听了，EAS 没有看门狗，声部会一直渲染。所以关闭时**先发 CC123 再关闸**（`gm_panic()`，原本就叫 `gm_stop`，改名是因为 panic 才是它的语义）。恢复时不重发 PC：`sent_prog` 不清，因为 CC123 不是 reset，接收端的音色状态没变。实测：关掉后整轮播放对 EAS 零 note-on，重新打开后计数恢复。

### 设定持久化（此前完全缺失）
`plat_store_save()` 在 driftseq 里**零调用**——`set_dirty` 置了位没人用，所有设定断电即丢。现在 `set_commit()` 在**退出 GLOBAL 页**时写（`OBJ_SETTINGS`），不在每个 detent 写：4 KiB 扇区擦写，设定页的正常形状就是"改一堆然后走开"。`settings_t` 加 `gm_on`，开机在 `app_init` 里恢复。**蓝牙不在这里恢复**：平台按 `APP_BLE_AT_BOOT` 自己起，重复告知只会和启动竞争。

### 第 1 页的空间分配（John 2026-10-30 问"剩下的空间放什么"）

第 1 页原来 4 行到 y108 就没了，y120 到页脚（y216）之间有近 100px 空白。John 提的三个候选（tap tempo 说明、CPU 负荷、剩余 pattern 空间）里：

- **tap tempo 说明**：收成一行 `TAP: RIGHTMOST KEY`（y194）。不能再长——这一页其余部分是数值表，说明句一多就把表挤成注释。
- **CPU 负荷**：`plat_cpu_pct()` 确实有（`audio_cpu_pct` ← `fm1b_dbg.cpu_q12`，音频 block 里量的实际负载，约 16 block 平滑）。显示成 `CPU 47%`。
- **剩余 pattern 空间**：**没有这个数**。全仓唯一的存储对象是 `OBJ_SETTINGS`（`plat_store_save(OBJ_SETTINGS,…)`），pattern bank 根本没实现（`chain[64]` 零读写、`flash[16]` 只有清空循环）。能报的只有"当前 pattern 打包后占多少"，所以标成 `PATTERN 146/3840 1S`（字节 / 一个存储对象 / 需要几个 4 KiB 扇区）。**报一个并不存在的银行余量会是在说谎**。
- **XRUNS**：顺手加的一条。`plat_xruns()` 之前全仓零调用，音频欠载一次就 ++，这是"为什么刚才咔了一下"的唯一线索，比任何静态信息都值得占一行。非零转琥珀色。

**FW 版本号没放**：现在没有可显示的版本串。`APP_VERSION` 默认 `"DEV"`（`device/unity.c`），只有 `--release X.Y.Z` 才变成真版本，而 identity（`FM-1_9200`）是打包器写进 fwsc 头部的，app 运行时读不到。要放得先有一条"版本从哪来"的路（编译期 `-D` 注入日期/哈希，或读 flash 里的 identity），那是另一件事，不在这一页里顺手加。

**为什么读数在行下面而不是第五行**：它们不是设置，K1/K2 碰不到。用 `FONT_T` 灰标签 + `FONT_S` 值、行距 16px，视觉上和上面四条 band 行分开——band 是"能改的"，无 band 是"只读的"。

### 探针
`tools/global_probe.mjs` **52 项**：第 1 页三条行带（34px pitch）+ 药丸已消失 + 三条读数 + `TAP` 行 + 页脚无溢出；第 2 页开关行在 row 0、设备行 y52、信号条、底行状态 + SAVE 提示翻转、连接后链接行仍在、页脚 `ON/OFF` 只出现在开关行；和弦翻页、裸 HOME 离开；**以及 TRACK SET 的轨号药丸仍在**（`hdr_t = {0}` 的回归保护）。

探针坑：band 判定要"连续 10 行"，单行判定会把 header 底边当成 band（报过 "band at 20"）；band 高 25px、pitch 34px，相邻 band 重叠 9px，实测步进是 **32** 不是 34。

### 顶栏 6 点的坑
`dx = 211 + i*6` 在 i=5 时是 241，出 240 屏；改 5px 间距（211..236）。**TRACK SET 的药丸右界 208** 仍安全。

- **本页弹琴不写 jam**（`ui.c` 里 `uint8_t jam = (ui_mode != M_TRKSET)`）：键照样 `ds_live_on/off`（试听 PROGRAM、试听 ROUTE），只是不进 jam ring；GRID 上同一次按键仍然入 jam，所以这是页面属性不是键盘坏了

### 引擎侧同时落地（`2c21d00`、`d3092d9`）
三个字段此前引擎从未读过（只有 pack/clear 用），上线必须同时给语义：
- `route` → `ds_route_sync()` 维护 `usb_mask`/`ble_mask`（按 channel 位），`app.c` drain 按两个 mask 组装 `to` 后一次 `plat_midi_send`；**GM 不再受 ROUTE 影响**（2026-10-30 改，原 `wire_mask`/`gm_mask` 名为 GM 目的地已废）。每 block 重算（16 次迭代），页面改完立即生效
- `voice_mode` → MONO 每步只发**最高音**（和弦按进单音轨时语义正确），`mono_note[t]` 记住本轨挂着的那一个，下一步先掐它
- **channel 改动的挂音 bug**：`ds_gate_ev` / live pool 原本在 off 时回读 `track.channel`，页面改通道会让 off 发到错误通道 → 改为一律**捕获 note-on 时的通道**
- 探针：`ts_row` `ts_ch` `ts_prog` `ts_route` `ts_voice` `ts_swing` `ts_gswing` `ts_owner` `ts_shared` `ts_prog_hash`(djb2)

### 测试
- `tests/global.txt`：**91 断言**全通过（第 1 页四行两端死停、tap tempo 120/160 两档、GM 双向 + panic、退出写盘、第 2 页扫描/选中/连接/断开、反向契约"K2 在第 2 页动不了任何东西"）
- `tests/trkset.txt`：**85 断言**全通过（门、五个字段两端死停、1-based 显示、逐轨独立、PROGRAM 行 djb2、HOME/SEL 出口）
- `tests/trkset_route.txt`：**ROUTE 到线缆边界** —— 16 轨全 GM → `midi_out == 0`；全 USB → `midi_out == 32`（45 是旧值：多出的 13 条是每条共享轨各发一次 PC）
- `tests/trkset_owner.txt`：**21 断言** —— 鼓通道 owner=0(4 条里最低)、bass owner=3（11 条空轨不能抢）、lead owner=4、改空轨 PROG 不动 owner、改 owner PROG 才动
- `tests_engine/test_trkset.c`：ROUTE 掩码 / MONO 掐音 / SWING / 通道改动后 off 走原通道
- golden 哈希 `420C4824C85B79CB`→`52CF520E290E322D`（note-ons 242 不变；只有 PC 字节变了，这正是修复的证据）
- ASAN 全跑 trkset+scale 无报错

## Program Change 归属（`e207583`，2026-10-29）

**PC 是通道状态，不是轨状态。** 接收端每条通道只存一个 program，所以"每轨一个音色"只在一条轨独占通道时成立。

规则（John 2026-10-29 拍板方案 1）：
- **owner = 该通道上编号最低、有音符、未被 mute/solo 掩掉的轨**（`build_owners()`，每 tick 从 pattern 重算）
- 只有 owner 发 PC，且只在 `sent_prog[ch] != t->prog` 时发
- "有音符"只看**自己窗口内**（`loop_len` 之内）的 `nvoices`：窗口外的音符永远不响，和空轨等价
- 空轨不占通道：11 条默认轨和 bass 同占 ch0，但都没音符，所以 owner 是 trk3

**为什么不用 per-run 标志**：旧的 `pc_sent[16]` 回答的是"这次 run 发过没有"，而接收端记住的是"这条通道是什么音色"，两者跨 pattern 切换就脱节——切到 B，标志还是 1，B 的新 program 永远发不出去。改成 `sent_prog[16]`（每通道记住上次真正送出的值，初值 0xFF=未知）后，判据变成"接收端的副本是否已一致"。

**为什么 owner 每 tick 重算而不是切换时算一次**：owner 是 pattern 的属性。pattern 换了、CH 改了、某轨取消 mute 了，下一个 tick 自然收敛，不需要任何显式的失效通知——这也是 chain 将来能直接复用的原因。

**chain 待办**（今天**完全没有实现**，不能当已存在）：`chain[64]` / `chain_len` 声明了但全仓零读写，`flash[16]` 只有 `ds_init` 的清空循环，`chain_len` 没进 `pack.c`，UI 零命中，`DS_C_LAST4` 是 `return 1 /* TODO */`。真做 chain 时 PC 侧只需保证边界顺序 **off → PC → on**（`ds_stop` 的 CC123 路径不能直接用于 pattern 切换，切换不是 stop）。

### 两个已修的 bug（都是真 bug，不是测试错）
1. **`txt_pool` 越界写坏 `jam_menu`**：`SHARED WITH T1 T2 …` 最多 58 字符，塞进 36 字节槽 → 溢出到 `jam_menu`(=50) → 悄悄打开 SAVE 菜单 → 菜单吞掉 K1/K2，"K2 没反应"其实是缓冲区溢出。改用局部 `char[48]` + 列表上限 3 名 + `+N`
2. **`tests/scale.txt` 4 处注释行**（`#` 前有空格）让解析器以 `unknown command` 停机，28 条断言实际只跑了 25 条 —— 此前"0 fails"是没跑完。已并入前一行注释，现 28 条全跑全过

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

## SELECT 切轨要报音色（2026-10-30）

**问题**（John）：「track set画面，chang prg后的确起作用了，但是此时SELECT切换track，音色又不对了」

**根因**：接收端每个通道只有一个 program 副本，而 PC 只在 `ds_tick()` 的播放分支里按 owner 规则发。停着的时候在 TRACK SET 上改 PROG 已经能发（`9c46305` 加的 `ds_send_prog()`），但 **SELECT 换轨本身不发 PC** —— 于是选到另一通道的轨，合成器还停在上一个音色：TRACK SET 行里写着 `041 VIOLIN`，琴键弹出来的还是 bass。

**修法**：`sel_track` 的三处写入（TRACK SET 的 SELECT、裸页 SELECT、jam 菜单 NEW TRK 分到新轨）统一走 `ui_select_track()`，它只做两件事：赋值 + `ds_send_prog(&eng, t)`。放在 `ui/ui_preset.c` 里（PRESET 与 SELECT 是同一族：都在描述"当前 track 是什么声音"）。开机 `sel_track = 0` 仍直接赋值 —— 那时还没有传输可报。

**`ds_send_prog()` 补上去重**：原来无条件发，现在和 tick 路径同一条规则 —— 接收端副本相同就不发。走同一个通道的几条轨之间 SELECT 只发一次，来回改同一个 program 也不会在音符中间反复重报（有些模块会听得出来）。`sent_prog` 初值 `0xFF`，所以第一次一定发。

**实测**（host，John 的原始序列）：track 3（ch0，prog 38）改 PRG 到 42 → SELECT 到 track 4（ch1，prog 81）→ SELECT 回 track 3 → PLAY：
```
C0 26 00   SELECT 到 ch0 就报 38
C0 27..2A  改 PRG 的四个 detent，每格一条
C1 51 00   SELECT 到 track 4 报 ch1 的 81
C0 2A 00   SELECT 回 track 3 报 42（去重后仍发，因为接收端是 81）
```
琴键确认用的是 42，不是 81 也不是 38。

**测试**：`tests/preset.txt` 19 → **45**（SELECT 走一遍 ch10/ch0/ch1 的报音色、同通道不重发、PRESET 每格发 PC、popup 自动关、六个页面 PRESET 都生效）；`tests/trkset_route.txt` 36 → **39**（SELECT 走轨会带出 PC，改用 delta 断言：全 BLE 下 PLAY 前后 `midi_out` 都是 3）。

**PRESETS 全局化**（同轮）：`ui_edit.c` 里把 `EN_PRESET` 拦在页面专用 encoder 分派**之前**，与 SELECT 同级 —— TRACK SET / GLOBAL / SCALE 都把编码器整体拿走，原来 PRESET 在那三页是死的。四个页面实测每格都发 PC（`c9 01`→`c9 02`→…）且 popup 出现。

**探针教训**：`tools/global_probe.mjs` 报 `footer K1 says a word (DEVICE)` 只有 18px。根因不是页脚画错，而是**旋钮在手的读数覆盖层**：`kn_ttl` 给 40 帧（约 640ms），K1 那一栏画的是数字 `1` 而不是 `DEVICE`。取帧必须等读数过期（`step(900)`）。这与页脚闪烁那次同源 —— host 能看见最后一遍，真机看见每一遍。

**尺寸**：image 524,544 / 581,564（余 57,020）；RAM 68,692；fwsc `fca76e5bb44df4ca5c72b9ed715335d5`，611,547 B。

**探针**：`tools/preset_probe.mjs`（38 项）在**实际下发的 wasm** 上验证 HOME/ARP/SCALE/TRACK SET/GLOBAL 五页：每转一格都发 PC、popup 出现、2.6 s 后自动收回、收回后回到该页自身基线。两个坑已在文件头记死：① `build/emu/app.wasm` 可能旧于源码（第一次"PRESETS 没反应"就是这个），探针比 mtime；② **按钮 bit 来自 `plat.h` 的顺序** `B_FX 0, B_SEL 1, B_ENV 2, B_LFO 3, B_EDIT 4, B_GLO 5, B_HOME 6, B_ARP 8` —— 按 3 进的是 LFO 不是 TRACK SET，第一版探针因此"验证"了三个它从没进去的页面。页面身份改用各页自身 body ink：HOME 2144 / ARP 1213 / SCALE 9470 / TRACK SET ~3200-3700 / GLOBAL 7125。

**刷机陷阱（2026-10-30 实测）**：`build.py --web` **不会**把新的 `build/driftseq.fwsc` 拷到 `build/emu/`，而 8933/8930 服务的是 `build/emu/`。只跑设备构建再跑 web 构建，网页上下载到的仍是**上一次的 fwsc**（那天 09:34 旧件 `7d539e8a` vs 09:47 新件 `fca76e5b`，两者都是 611,547 B，看大小分辨不出来）。下发前必须 `cp build/driftseq.fwsc build/emu/`，并 `curl | md5sum` 核对。
**BLE 双向回声：定案不做（2026-10-30，John 拍板 C）**

现象：MPC 建一个 MIDI track，in=all port、out=MPC（即 BT01），MPC pad → FM-1 正常；FM-1 按键 → MPC 后会从 MPC 再回来，形成回灌。电脑走同一台 BT01 也一样，故与 FM-1 无关，是 MPC 那个 track 把自己收到的又送出去。

**但 FM-1 自身也在 echo，实测（host，非推断）**：BLE-DEV 连上后注入 `midi BLE-DEV 90 3C 64` → `out 99 3C 64 -> USB BLE-DEV`；`80 3C 00` → `89 3C 00 -> USB BLE-DEV`。路径 `ui_midi_dispatch` → `ui_midi_note` → `ds_live_on` 写入 `eng.out`，`engine_drain` 按 ROUTE 选 `to`，默认 BOTH 的 `TO_BLE` 同时含 HOST 与 DEV，不分方向。`app.c` 头注释声称遵守 plat.h 的“不回发到来源链路”，但 `plat_midi_in_from()` 在本工程**只出现在那句注释里**，`midi_q` 也只存包不存来源 —— 该约定从未实现。

**否决方案与理由**：
- **A 抑制来源链路**（midi_q 带来源 + drain 时从 `to` 剔除）：符合平台约定，但会引入“同一音符因到达路径不同而走向不同”的分支，且当前痛点在 MPC 配置而非 FM-1。
- **B live 音符只喂本地 GM、不出线**：与“外部音源为主”的定位冲突，外部键盘接 FM-1 时演奏传不到下游。
- **C 维持现状（选定）**：作为 central，FM-1 的动作与电脑完全一致——电脑收到什么就发回什么。行为对齐是本次的目的，回声由 MPC 端关掉回发解决，不在固件里特判。


**SCALE 页键盘灯：床是键位区，不是 pattern（2026-10-30，John 真机报 + 拍板）**

John 报「SCALE页面的键盘灯有问题，随着弹奏逐渐越亮越多，大概率是算错了该亮哪个键」，并给出定案：「在scale状态下不需要0,4,8,12的键盘标尺（标尺只对drum有用）反而是左边级数7个键，右边和弦8个键需要dim，然后和弦按下有效的需要亮起」。

**根因**：`ui_leds`（ui.c）把**每个页面**的「有音符的步」画成白键灯，而 SCALE 页每次按 root 都 `sc_grid_write()` —— 整格替换 + `cursor++`（2026-10-28 就医时定的和弦输入）。于是每弹一个和弦就多写满一格，键床上永久多亮一个白键：亮的不是「该亮的键」，而是「已经写过的步」。同一循环还画 16 步页内四分标尺（id 0/7/14/21）。两个信息在这页都是冒名：OFC 打开时键床已被图例标成 DEG / ROOT / CHORD 三区，它的语义是**键位**，不是 step。

**修**：
- `ui/ui.c` `ui_leds()`：step 灯与四分标尺整段只在 `ui_mode != M_SCALE` 时画；`drum_sel` 的鼓黑键也在 SCALE 页不画（那页没有「当前鼓」这回事）。
- `ui/ui_scale.c` `scale_leds()` 重写，SCALE 页独占键床：OFC 关 → 0..18 全 dim（每个键都是可弹的 snap 键盘，19..26 不存在）；OFC 开 → 级数键 0..6 dim（CHR 无级数，不 dim）+ 成分键 19..26 dim，选中且**有效**者亮，root 选中亮。`有效` = `oc_resolve(sc_mask).n != 0`：两个三音（MIN+DIM、MIN+SUS…）或两个七音（7+M7）让整组熄灭 —— 和不发一声、和屏幕上的和弦名为空是同一个判断，不是另算一套。

**测试**：`tests/scale.txt` 28 → **43**（新增自 `reboot` 起的干净段：OFC 关 0..18 dim、OFC 开 0x7f8007f、连弹四个 root 只亮最后一个且 zone 不长大、MAJ 有效亮、两个三音整组熄、退出页面标尺回归）。新增 host `expect lit_keys / dim_keys`（app.c `app_expect`）—— 灯掩码之前只能靠 `leds` 那行散文肉眼读，测不了。host 合计 **327** 断言全过；引擎 5 套 + golden `52CF520E290E322D` / 242 note-ons 不变。

**探针**：新增 `tools/scale_led_probe.mjs`（**11 项**），在**实际下发的 wasm** 里断言同一组行为（含「OFC 开时 keys 7/14 保持黑 = 无标尺」——OFC 关时整床都 dim，标尺藏不住，只能在这个态测）。三个既有探针 52 / 23 / 38 全绿。

**尺寸**：image 524,656 / 581,564；RAM 68,692；fwsc `5bff5e15a2c3ab1383f2ff89eb097a6f`，611,547 B，已 `cp` 进 `build/emu/`（两端口 md5 一致）；wasm `4bb05ecb25adcfcb080b94cb73c3d990`，412,897 B。

**实测中三个「假 bug」教训**：① 我一开始用 `turn K4` 开 OFC，K4 早已不是 OFC（注释写明 "K4 is freed"），OFC 在 **K3**；② 在 track 0 上测——demo 的 0/1/2 轨都是鼓（ch9），SCALE 的和弦键盘对鼓轨不存在，`is_drum` 直接跳过整段；③ 探针常数写错三处（keys 7..18 是 `0x7ff80` 不是 `0x7f80`；"标尺不存在"不能在整床 dim 的态下判断）。三次都是断言错，不是代码错 —— 先按 `leds` 实际输出反推，再改断言。


**鼓组（drum kit）：标准有几个、我们做到哪（2026-10-30，John 拍板暂不做）**

John 问「PRESETS 切换 drum set 的时候一共有多少个 drumset？GM 标准呢？」，追问时定调「不要考虑 EAS 支持多少个，按 GM 标准来」，收到答复后定案：「gs 和 xg 的音源我都有，不过完整的支持工作量太大，暂时不考虑了」。

**标准答案**：
- **GM Level 1：1 套**。GM1 只规定 Standard Kit，ch10 音符映射固定，规范里没有「换鼓组」这回事。
- **GM Level 2：9 套**，program（1 起算）= 1 Standard / 9 Room / 17 Power / 25 Electronic / 26 TR-808 / 33 Jazz / 41 Brush / 49 Orchestra / 57 Sound FX。
- **GS 另有 128 CM-64/CM-32L**（MT-32 兼容鼓组），GM2 规范本身不含；XG 的 kit 表同样是这 9 套加扩展位。

`ui/ui_trkset.c` 的 `GM_KIT[]` 9 条正好就是 GM2 那 9 个，只是用**线缆上的 0 起算号**（0/8/16/24/25/32/40/48/56）。PRESETS 在鼓轨上开放 **0..127**（128 个位置），只有这 9 个有名字，其余 119 个显示纯数字 —— 标准没给它们命名。

**内置 EAS 的实际行为（实测，不是推断）**：鼓通道上**根本不区分 kit**。`eas/wt_200k_G.c` 文件自带统计注释 `Number of banks: 1 / Number of programs: 1`，`eas_banks[]` 只有一个 locale `0x7900`（旋律 bank MSB），鼓 bank `0x7800` 不存在 → `VMFindProgram()`（eas_voicemgt.c:2736）在 `pBanks` 里找不到就回落到 `pPrograms`，而那只有一条 `{ 7864320, 0 }`。实测：鼓轨上 prog 1/8/16/25/56/127 打 kick+snare+tom+hat 五击，WAV 哈希**逐个相同**；prog 0（不发 PC）另有一组哈希。对照实验证明方法有效：旋律轨换 0/24/40/56 音色明显不同（56 与 73 相同，是同一组采样，合理）。

**定案**：GM2 那 9 套鼓要听，得接外部音源（John 有 GS/XG 硬件）。固件侧**不做** kit 支持——要做得把 GM2 的 bank/kit 数据打进固件并让 EAS 查 bank，工作量与收益不成比例。现状保留：鼓轨 PRESETS 照常发 `C9 xx`（外部音源会响应），popup 对 9 个已知号显示名字、其余显示数字。这条不是 bug，是「内置 GM 仅试听」定位下的已知边界。

**鼓轨的 PRESET：九套 kit + bank select（2026-10-30，John 两条指令）**

John：「我建议drum的preset就用有值的那9个」，随后指出更根本的问题：「否则发那么多pc没意义和gm音源对不上、实际上和gs/xg也对不上，它们需要发bank select才行」。

**第二条是本轮真正的修复**。鼓通道的 program 选的是 **kit**，而 GM Level 1 只有一套（Standard），所以裸 PC 在 ch10 上对任何按规范实现的音源都等于没换。GM2 把鼓组放在 **bank 120**，GS 的鼓组同样走 bank select，XG 的 kit 也在自己的 bank MSB 下——三者共同点是**必须先发 Bank Select**。此前 `emit_pc()` 只发 `C9 xx`，这正是「换 kit 不生效」的原因：不是名字表错，是消息缺一半。

**修**：
- `engine/driftseq.c` `emit_pc()`：鼓通道（ch9）改为三消息 `CC0 120 → CC32 0 → PC`，共 8 字节；旋律通道仍是单个 2 字节 PC。CC32 显式归零，否则音源若停在 GS 的 variation bank 上会忽略 CC0。
- `ui/ui_preset.c`：鼓轨上 PRESET 只在 `GM_KIT` 那 9 个 program 之间走，不经过 119 个无名槽；两端死停；若轨道停在无名槽（外部编辑器可造成），按旋转方向吸附回集合内。popup 的上下邻居行也走同一个 `kit_land()`，否则会显示 "023 KIT" 这种既不是 kit 也不是来路的数字。
- `GM_KIT[]` 从 `ui_trkset.c` 移到 `ui/ui_tables.c`（与 `GM_PROG` 同处）：TRACK SET 的 PROGRAM 行和 PRESET 都要用，不再依赖 unity build 的包含顺序。
- TRACK SET 的 PROGRAM 行**仍保留 0..127**：它是诚实编辑器，外部模块可能把 kit 放在无名槽；PRESETS 是表演控件，只停在有东西的地方。

**标准答案**（John 要求按 GM 而非 EAS）：GM1 = **1 套**；GM2 = **9 套**（1 Standard / 9 Room / 17 Power / 25 Electronic / 26 TR-808 / 33 Jazz / 41 Brush / 49 Orchestra / 57 Sound FX，1 起算；线上是 0/8/16/24/25/32/40/48/56）；GS 另有 128 CM-64/CM-32L，GM2 规范本身不含。

**golden 随之改变，并做了双向断言**：`52CF520E290E322D` → **`1DFFB74BC111252F`**（242 note-ons 不变）。`test_golden.c` 现在同时校验「剔除鼓通道的 bank select CC 后必须精确回到 `52CF520E290E322D`」——实测剔除 2 条 bank 消息后哈希逐字节回到旧值，证明**唯一**的变化就是 bank select，没有别的字节被顺手改动。这条断言取代了「改常数了事」，将来任何隐藏回归都会让它失败。

**测试**：host **347** 断言全过（preset 45→63，其余不变）；引擎 7 套全过；探针 preset **38→43**（新增 KIT 段：八次 detent 依次 8/16/24/25/32/40/48/56、每次前置 CC0=120、CC32=0、不停在无名槽），scale 11 / global 52 / trkset 23 全绿。

**踩到的坑（都是探针错，不是产品错）**：① 探针原来在 track 0 上测「每页 PRESET 都生效」，而 0..2 都是鼓轨，改成先 SELECT 到 track 3（旋律）再测页面；② `pz_prog` 只在转旋钮后更新，读「当前轨 program」要用 `ts_prog`；③ **PC 是两字节消息，值在 d1**，探针读 d2 导致八套 kit 全报 0——这条曾让我误判成 drain 分帧 bug，读原始包 `c9 8 0` 才定位。

**尺寸**：image **524,976**（+320 B）/ 581,564；RAM 68,692；fwsc `c8302226ddf74e2a65460ea10cf1a575`，611,547 B，已拷进 `build/emu/`；wasm `424b161f9c485cca0f4ec5dc9ca39a5a`，413,261 B；8933/8930 双端口同件。真机仍是最早的 `8412daad`，本版未刷。

## 和弦层在每一页都发声；写 step 只属于两个 grid 页 —— 2026-10-31 实测

**John 报告**："scale页面，opc on的弹奏内容没有进jam，回到hero后自动退出了ofc，应该保持的"

**两个症状，一个错。** 上一步修 step record（`5f5e317`）时，我把页面门加在了**按键**上：

```c
if (ui_mode != M_GRID && ui_mode != M_SCALE) return 0;   // scale_key_hook() 开头
```

于是 hero、TRACK SET、GLOBAL 上的每一个按键都被交回普通试听路径：**单音、不 snap、不成和弦**。设置一直是 ON，发声权被拿走了 —— 从演奏者角度看，"这一页替我关了 OFC"和"这一页把 OFC 变成哑的"是同一件事，所以它们被一起报上来。

**门属于动作，不属于输入。** 两个命题不同，不能共用一个门：

- **和弦层 = 这件乐器怎么发声** —— 每一页，无例外。
- **写 step = 两个 grid 页的职责** —— 只有 `M_GRID` / `M_SCALE`。

新增 `sc_write_ok()` 问第二个问题，由两个写函数（`sc_off_write()` / `sc_grid_write()`）调用，按键不问它。`focus_layer`（EDIT 淡入层）仍留在按键上，因为它管的是"谁收到这次按键"，不是"记不记录"。

**实测证据（host）**：`jam_used` 只数**格子**，三音和弦和单音都读 1，所以为它加了 `jam_notes`（数 voice）。OFC on + MAJ + 选过三度：SCALE 页弹根音 → `jam_notes` **4**（根/三/五/七），hero 上同样 **4**，TRACK SET 上 **4**，格子都不写。反向：把宽门放回去，hero 读到 **1**（单音），测试失败 4 条。

**另一件实测澄清**：不选任何 component 时，`sc_revoice()` 里 `!sc_mask` 直接 `sc_tones[0] = root` 就 return —— **OFC on 本来就不是和弦**。要测和弦必须先按一个 component 键（key 24+）。这点先前被我当成 bug 查了一轮，是设计。

**测试**：`tests/chordsound.txt` **28** 条；`tools/chordsound_probe.mjs` **7** 项。两者都反向验证过（旧代码分别失败 **4** 和 **3**）。host 全量 **684** 通过（`basic.txt` 35 条模板遗留一直在）。

**探针 lesson**：第一版探针断言"hero 会画出和弦"，实测 **0 px 变化** —— hero 画的是 **pattern 的图**，不是键盘，本来就不显示实时音。改成读 MIDI 出口：hero 发出的正是 `[67,71,74,81]`，与 SCALE 页逐音一致。**屏幕不是唯一的证人，声音才是。**

**另一件探针 lesson**：整帧比对不可用 —— 空跑一次 hero 往返、什么都没弹，底部 y212..214 也差 **611 px**（页面往返自己改那三行）。格子区是 y0..211，只比这一段。

**尺寸**：image **527,888** / RAM **68,744** / fwsc **`3d0e5399a6cc79b60a0233ef78cd964e`**（611,547 B）；wasm **`cde14b1808d4d9f31eaba3f33ccdcabe`**（421,316 B）；8933 / 8930 双端口实测同件、`no-store`。

**既有欠账（在 master `549ccec` 上同样失败，与本轮无关）**：`jam_sound_probe.mjs` 2/3、`midi_probe.mjs` 3/14。用 worktree 对照过 `549ccec`，数字一模一样。

## 为什么测试没发现 —— 可数的答案，以及补上的那张表 —— 2026-10-31

John 问："为什么测试没有发现这些错误？"

**先数，再说。** 补测试之前，全量套件里一共 **108 次按键**：

- SCALE 页 **86** 次、GRID 11 次、hero **4** 次、ARP 4 次、TRACK SET 3 次
- 这些断言**全部**是屏幕状态或格子内容——**没有一条断言"按键发出声音"**

两个 bug 都住在那一个格里：**(不是 SCALE/GRID 的页面) × (它响不响?)**。而这一格整个套件只碰过一次（`trkset.txt` 里 hero 上的 `sel_track`），且不含声音。所以一个"hero 画面完全不变、但变哑"的改动，能通过全部测试。

**更深的错在断言的写法**：我只测了刚刚改动的机制（写），把"发声"当成不会坏的不变量——因为和弦层是老代码、一直在工作。但两个 bug 都是**连带伤害**，不在我改的东西里：第一次修改变了键被消费的位置，第二次修把门加在了键上而不是写上。**改一条共享路径，坏的是这条路径的其它消费者**，而那恰恰是自己的测试不会去看的地方。

`tests/keymatrix.txt`（**56** 条）就是那张缺失的表：5 个页面 × CHORD KEYS 两态，每格问两个**分开的**问题——**发声**（`gm_noteons`）、**写格**（`step0`/`cursor`）。反向验证：还原宽门失败 **12** 条，还原"OFC off 只试听"失败 **4** 条。

规则：**一个页面可以只发声不写；永远不许只写不发声。只测其中一半，就是这两个 bug 藏身的方式。**

## jam 存不下来：take 的归属由演奏决定，不由时钟决定 —— 2026-10-31 实测

**John 报告**："hero页面jam还是没能保存。jam之后选save，replace，然后seq看到所有step都是空的"

**主因：`pass` 是从挂钟推出来的。** `ds_jam_save_*` 里 `pass = jam_abs >> 6`，即"按 SAVE 那一刻时钟走到第几个 pass"。环是 64 步 1/16，120 BPM 下一步 125 ms，**一个 pass 正好 8 秒**。弹完乐句、想一下、伸手按 SAVE —— 每个音都被盖上"上一轮的戳"，save 循环把它们当上一轮残留跳过，轨道全空。

没有任何东西过期，也没有计时器：**take 的身份来自一直在跑的钟，而不是来自演奏**。修法：`state.jam_pass`，由 `ds_jam_capture` 每次落音时盖章，save 读它。环里因此永远是**最后弹的那一句**——这才是 John 10-28 那条决定（"C E G，一轮之后 A B A B，必须只存 A B A B"）真正要的东西；先前的读法"take 就是此刻所在的 pass"顺带把玩家想留的那句也扔了。

**同一报告里另两个真 bug：**

- **弹窗打开时按 SAVE 完全无效。** 规则 3（"SAVE 不是确认键"）被实现成了"SAVE 在弹窗期间是死的"，第二次按直接消失。现在它用自己的弹窗替换掉现在的，开在 SAFE：一条规则，没有例外——SAVE 打开 SAVE 弹窗，从任何地方，包括在另一个弹窗上面。
- **弹窗期间按琴键仍会写进环。** 弹窗是对环内容的决定，玩家停手之后手蹭到键盘不该落进正被保存的 take。**但按键仍要发声**。这道门第一次写进了 `scale_key_hook`，而那只覆盖和弦层——`panel_key_handler` 有自己的试听路径，会自己 capture，一半的键照样漏过去。现在只有一个入口 `sc_jam()`，由每个写入者调用，做法与 `sc_write_ok()` 完全一致。

**实测**：`tests/jamsave.txt` **25** 条（等 **9 秒**后 take 仍在、等待不会复活旧 take、SAVE 替换弹窗且开在 SAFE、弹窗期间发声但不录）。三条反向验证全部成立：墙钟 pass → 失败 **3**；SAVE 无效 → 失败 **2**；弹窗期间录 → 失败 **7**。host 全量 **765**（`basic.txt` 35 条模板遗留不变）；引擎 5 套（jam / jam_rehit / tie / pack / pack_engine）+ golden `1DFFB74BC111252F` / 242 note-ons + euclid + engine 全过。

**两个测试自身的教训**（都是我写错，不是产品错）：

- 第一版用的是 **track 4**（本来就有 4 个音），"ts_notes 没变"就当成通过 —— 在有音的轨道上这证明不了任何事。改用真正空的 **track 5**。
- 测"SAVE 替换弹窗"时断言只写了 `jam_sel 0`：弹窗本来就停在 SAFE，所以**坏代码也能通过**。必须先把高亮移到第 2 行（`turn K2 2`）才能区分"没反应"与"重开在 SAFE"。

**探针**：`jam_sound_probe.mjs` 的 2/3 失败**不是产品 bug，是探针过时**——它用第二次 SAVE 确认（规则 3 之后 SAVE 不再确认），并用 `web_key(k, vel, 0)` 按键（第三个参数是 stress 0..1000，传 0 等于没按）。已改成 OCT+ 确认 + `web_keys` 掩码。另外确认：**master `549ccec` 上 `web_keys` 按键同样不出 MIDI**，所以那不是回归，是探针/emu 输入路径的既有状况，尚未定位（host 上正常）。
