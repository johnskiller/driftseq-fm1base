# OFC（One-Finger Chords）完整规格 v2 — 按评审重构

来源：`ui/ui_scale.c` + 本次评审（2026-10-29，John）。实现与文档同步改齐（commit `749bf71`，wasm `054799eb`，fwsc `d0ce22d0`）。

设计定位：OFC 是**轻量和弦输入与演奏界面**，不是编曲工作站。转位与音符级修改交给 Step Edit。真实场景 = MIDI 驱动外部音源；内置 GM 只为试听。

---

## 0. 评审意见 → 处理对照

| 评审点 | 处理 |
|---|---|
| 三组按键分工保留（级数/绝对根音/和弦类型） | ✓ 保留不变 |
| Root/Chord 采用**锁存选择**语义 | ✓ 已实现（§2） |
| 区分 Root Selection / Chord Selection / Active Chord | ✓ 按此模型重构（§1） |
| 发音分 Gate / Latch 两种模式 | ✓ 已实现：**K3 三档**（OFF / GATE / LATCH，死停两端；K4 因此腾空，John 2026-10-29） |
| 松开事件不得隐式撤销选择 | ✓ 已实现（旧的"抬键清位"全部移除） |
| 按住 Root 时切另一个 Root 需明确处理 | ✓ 新 root 接管：旧和弦先 Note Off，新和弦打击（§3.2） |
| GRID 写入行为需明确 | ✓ root 按下也写 step；游标规则见 §5 |
| MIDI 生命周期 / hanging notes | ✓ K3 OFF 挂音 bug 修掉；事件配对规则见 §7 |
| C6 五音问题 | ✓ 修为 0,4,7,9（原实现漏五音，属 bug 非取舍） |
| DIM+7 命名 | ✓ 实现是 0,3,6,10 = **m7♭5（半减七）**，文档已按此命名；**减七（0,3,6,9）= DIM+6**（本次一并实现，原来 6 被静默忽略） |
| 最大音符数与截断优先级 | ✓ 4 音，规则见 §4.4 |
| Chromatic 下级数键行为 | ⚠ 行为已核实但**音乐性存疑**（§6 开放问题 1） |
| 编辑层（EDIT）语义 | ✓ 修复：OFC 不再吞 EDIT 层按键（bug） |

## 1. 状态模型（三个概念，两个变量 + 一个门控）

| 概念 | 载体 | 语义 |
|---|---|---|
| Root Selection | `sc_root_key` | 当前选中的根音。**松开 root 键不清除** |
| Chord Selection | `sc_mask` | 当前选中的成分集合。**松开成分键不清除** |
| Active Chord | `sc_tones[]` | 当前实际发声/写入的和弦 = Selection 经 `oc_resolve` 的结果 |
| （门控） | `sc_gate` | 当前**物理按住**的 root 键（0xFF = 无）。Gate 发音的开关 |

## 2. 锁存语义（核心交互）

以 C → MIN → G 为例（评审 §三 的例子）：

1. 按 C → Root Selection = C，Active = C（单音，snap）
2. 松开 C → 静音，Selection 不变
3. 按 MIN → Chord Selection = MIN（无 root 按住 = 只登记不出声）
4. 松开 MIN → Selection 不变
5. 按 G → Active = **Gm**（新 Root + 保留的 MIN）——发声

- **成分键 = 触发器（toggle）**：按亮着的键 = 取消该成分（回到基础和弦的路径）。未亮的键按下 = 加入。
- 清除全部成分：逐个 toggle 掉，或按 MAJ（等价于"隐含大三度"，见 §4.1），或 K3 OFF。
- K3 OFF：**全部复位**（silence + root/mask/deg 清空）——可预测的"总清"。

## 3. 发音（两种模式，K4 切换）

### 3.1 规则

- **root 按下**：按当前 Selection 打击 Active Chord（无成分 = 单个 snap 音，吸附当前 scale）
- **root 按住期间**：
  - 改成分 → 立即重击（re-strike：旧音符全 Note Off → 新和弦全 Note On），不需要重新按 root
  - 走级数键 → 换配级数和弦，同样重击
- **root 松开**：对该次发音有效的全部音符 Note Off。Selection 不变
- **按住 A 再按 B（root）**：B 接管 selection + gate；旧和弦全部 Note Off → 新和弦 On。之后 A 松开 = 无事发生（gate 已在 B）
- **无 root 按住时**：按成分/级数 = 只改 Selection，不出声

### 3.2 MIDI 事件原则（评审 §六）

- 事件配对：每次状态变更 = 一组完整的 off+on；同一音高连续出现 = off→on（重触发），不悬挂
- 共同音不追求平滑衔接（legato 归音源）；实现 = 全 off 再全 on，最简且无悬挂
- 停止清理：root 松开 / K3 OFF / 无效组合 = 全部 Note Off
- 已修 bug：K3 OFF 原来只清 mask 不发声 off → **挂音**；现在全清（探针 M1 验证）

### 3.3 Latch 模式（K3 右到底 = LATCH，左到底 = OFF，中间 = GATE）

- root 按下 = 打击；**root 松开不打断**，和弦继续响（gate 保持锁住）
- 按另一个 root = 切换（旧 off → 新 on，继续锁住）
- 成分松开 = 无操作；成分再按 = toggle（回基础和弦，立即重击）
- 级数键同理锁存：级数松开不打断
- **LATCH → GATE 边界 = 静音**（模式切换当作清界，防挂音）
- 停止手势：K3 左到底（OFF 全清）或 退回 GATE；无新增按键
- GRID 写入不受模式影响（按下瞬间写，写入内容 = 当前 Active Chord）

## 3.4 按键规则的统一表述（John 2026-10-29 提炼）

| 动作 | 和弦选择 | 发音 |
|---|---|---|
| 激活成分键（按下未选中的） | 加入该成分 | **重击整个当前和弦** |
| 取消成分键（按下已选中的） | 移除该成分 | **只停该成分音**，不重击 |
| 按下根音键（不论是否同一个根音） | 根音 = 该键 | **触发当前选择的和弦** |
| 释放任意键 | 不改变选择 | 由 Gate/Latch 决定何时停 |

推论（都已实测）：

- 取消成分键不回写 GRID（选择是编辑，不是演奏）
- 激活成分键会回写 GRID（和按下根音一样，是一次演奏动作）
- 同一个根音连按两次 = 两次打击（re-hit），不是切换
- 级数键在 GATE 下自己就是门（1b 之后它的音高挂在 tonic 上，不需要先按 root）

## 4. 和弦定义（已按乐理核实/修正）

### 4.1 成分键（key 19..26，v3.7 人体工学序）

| key | 键 | 成分 | 单按（root=C） |
|---|---|---|---|
| 19 | C5 | MAJ | 0,4,7 —— MAJ 位是"隐含大三度"，与无成分**同音**（灯亮音不变，有意） |
| 20 | C#5 | 7 | 0,4,7,10 属七 |
| 21 | D5 | M7 | 0,4,7,11 大七 |
| 22 | D#5 | MIN | 0,3,7 |
| 23 | E5 | DIM | 0,3,6 |
| 24 | F5 | ADD9 | 0,4,7,14 |
| 25 | F#5 | SUS4 | 0,5,7 |
| 26 | G5 | 6 | **0,4,7,9**（本次修正：原 0,4,9 漏五音） |

### 4.2 组合表（root=C）

| 按键 | 和弦 | 音 |
|---|---|---|
| MIN+7 | Cm7 | 0,3,7,10 |
| MIN+M7 | CmMaj7 | 0,3,7,11 |
| MIN+DIM | Cm7♭5 | 0,3,6,10 |
| MIN+6 | Cm6 | 0,3,7,9 |
| MIN+ADD9 | Cmadd9 | 0,3,7,14 |
| MIN+ADD9+7 | Cm9 | 0,3,10,14（五音让位，见截断规则） |
| DIM+7 | Cm7♭5 | 0,3,6,10（= 半减七；**不是**减七） |
| DIM+M7 | Cdim(maj7) | 0,3,6,11 |
| **DIM+6** | **Cdim7 减七** | 0,3,6,9（本次新增：原来 6 静默忽略） |
| **DIM+MAJ** | **Caug** | 0,4,8 |
| SUS4+7 | C7sus4 | 0,5,7,10 |
| SUS4+M7 | Cmaj7sus4 | 0,5,7,11 |
| 6+7 | **C13(no5)** | 0,4,9,10（n=5 砍五音，标准爵士 voicing） |
| 6+M7 | **Cmaj13(no5)** | 0,4,9,11（同上） |
| ADD9+7 | C9 | 0,4,10,14 |
| ADD9+M7 | Cmaj9 | 0,4,11,14 |
| MIN+SUS4 | 静音 | 三度槽互斥 |

冲突规则：三度槽（MIN/DIM/SUS4）互斥（MAJ+DIM=Aug 例外）；七度槽（7/M7）互斥；6 与七度**不再互斥**（John 2026-10-29：支持，凑成 5 音时现有截断规则砍五音，得 13 和弦 voicing）。冲突 = **整体静音**（无错误 MIDI 事件，Selection 保留，可继续 toggle 修正）。

### 4.3 级数和弦（key 0..6）

- 前提：**gate root 按住**（无 root 时级数键无声无状态）
- 和弦 = 当前 Scale（SCALE 页 K1/K2）的调内音**堆叠三度**：默认 **3 音三和弦**（1-3-5），按 7/M7 才加七音（4 音）。C 大调：i=Cmaj、ii=Dmin、iii=Emin、iv=Fmaj、v=Gmaj、vi=Amin、vii=Bdim（John 2026-10-29：这才是"最自然的级数"）
- degree root 取离 gate root 最近的八度（±6 半音最短路径）
- 成分键修饰（`sc_flavor`）：MIN 强制小三、MAJ 强制大三（**只动三度**，不加七音）、SUS4 换四度、6 换六度、M7 强制大七、7 保留音阶自身的七音、ADD9 加九音
- 级数键抬 = 静音（不回落）
- **Scale 复用**：不引入第二套调式配置，直接读 SCALE 页的 `sc_kind`/`sc_tonic`
- **Chromatic 模式**：**级数键无效**（2026-10-29 John 拍板方案 c：半音阶没有"级"这个概念，对象不存在）。CHR 下和弦输入 = 中间 12 个 root 键（全部调外，走绝对根音路径，语义自洽）

### 4.4 音符数上限与截断

- 上限 **4 音**（`sc_tones[4]`）
- `oc_resolve` 组合达 5 音（如 ADD9+7）时：**先砍五音**（t[2]），保 root/三度/七度/九音 → C9 = 0,4,10,14
- 级数路径：三和弦 + ADD9 = 4 音，直接成立；加七音 + ADD9 = 5 音时 `sc_revoice` 的 4 音上限保护生效（保 1/3/5/7）

## 5. Grid Step Record（OFC 在 GRID 页）

- **写入时机**：Selection 变化导致 Active Chord 变化的**按下瞬间**——root 按下写一步，成分按下（root 已在 gate）再写一步
- **写入内容**：Active Chord 的全部 MIDI 音符（无成分时 = 单个 snap 音——root 单按也写 step，新增能力）
- **Step 处理**：替换 cursor 所在 step 的全部 voice，`on = 1`，cursor+1（63 封底）
- **松开不影响**：物理释放只管 Gate 发音，已写入 step 不动（Gate/选择分离的自然结果）
- Home Jam 的 Gate/Latch 设置不影响 GRID 行为（§3.3 的模式只影响实时发音，GRID 写入在按下瞬间完成）

## 5.1 SCALE 页显示（John 2026-10-29）

- **第三行 CHORD KEYS = K3 的读数**：`OFF` / `ON GATE` / `ON LATCH` 三态，一个旋钮一段行程
- **和弦名行**（y≈186，AUG 提示行下方，琥珀色）：当前 **Active Chord 的实际音名**（如 `C E G`、`C E G B`）——这是真机将要发出的音，用来验证 OFC 理解是否正确；静音时不显示
- **键盘灯**（OFC 打开且非鼓轨时）：
  - 成分区 8 键 = **dim 常亮底**，被选中的成分 **点亮**
  - 级数区 7 键 = **dim 常亮底**（CHR 下不点，因为没有级数），正在走位的级数 **点亮**
  - root 区 = 选中的 root **点亮**
  - 灯语：dim = "这区可用"，亮 = "这个是当前选择"（区分"选中"与"按住"——Latch 下松手仍在响）

## 6. 开放问题（评审 §九.4：列出待定，不自行扩展）

1. **Chromatic 级数键的音乐性**：当前 = 半音堆叠簇（0,2,4,6…）。可选：CHR 时级数键回退到大调堆叠？或 CHR 时级数键禁用（只留 root 路径）？**待 John 拍板**
2. ~~Latch 停止手势~~ **已定（2026-10-29 John）**：K4 切换 Gate/Latch（SCALE 页）；停止 = K3 OFF 或切回 GATE
3. ~~级数 + Latch~~ **已定**：Latch 下级数键同样锁存（松开不打断）
4. ~~6+7 / 6+M7~~ **已定（2026-10-29 John：支持）**：= C13(no5) / Cmaj13(no5)，走现有砍五音截断

## 7. MIDI 事件与 Legato 边界

- CFW 职责：生成音符、正确配对 on/off、变化时清理旧音符、停止时无悬挂——全部已实现并有探针覆盖
- 音源职责：legato/portamento/包络/释放——CFW 不模拟
- 第一版不做共同音追踪；`re-strike`（全 off → 全 on）是明确的最低复杂度方案

## 8. 测试覆盖（tests/scale.txt 已重写，全部通过）

1. ✓ 锁存选择：root 松开 → 保留；成分无 root 登记 → 新 root 落下时成型（G maj 用例）
2. ✓ 成分 toggle-off 回基础和弦（CmajAdd9 → C maj）
3. ✓ 按住 root 切另一个 root（旧 off → 新 on）
4. ✓ C6 = 0,4,7,9；DIM+6 = 0,3,6,9
5. ✓ 无效组合静音（7+M7、三度槽双选）；6+7/6+M7 = 13 voicing ✓；CHR 级数键死 ✓
6. ✓ 级数走位（ii → Dm7；抬键静音）
7. ✓ K3 OFF 全清（无悬挂）
8. ✓ EDIT 层 bypass（raw 音符）
9. ✓ demo 回归 -12.0 dBFS
10. ✓ Latch 模式：root 松开继续响、成分 toggle、K4 边界静音、K3 OFF 全清（tests/scale.txt LATCH 节）

## 9. 与实现对应的代码位置

| 行为 | 位置 |
|---|---|
| 选择/门控状态 | `ui/ui_scale.c` 顶部 `sc_root_key` / `sc_mask` / `sc_gate` / `sc_deg_key` |
| 三分区按键 | `scale_key_hook`（hook 顶部有 `focus_layer` bypass） |
| 组合解析 | `oc_resolve`（冲突→0 音；n==5 砍五音） |
| 级数堆叠 | `sc_degree_chord`（SC_STEP 宏）+ `sc_flavor` |
| 重击 | `sc_restrike`（全 off → 全 on） |
| GRID 写入 | `sc_grid_write`（root-down / component-down / degree-down 共用） |
| K3 复位 | `scale_encoder` EN_K3 分支 |
| K4 模式 | `scale_encoder` EN_K4 分支（sc_latch；SCALE 页专属，K4 原空闲） |
| 发音执行 | `sc_apply`（diff）+ `ds_live_on/off` + `ds_jam_capture` |

## 10. 未验证/待真机

- 全部结论来自 host 探针（L1-L6/M1-M5 + tests/scale.txt）；真机固件 `d0ce22d0` 未刷
- LED 语义：成分灯 = Selection（含未发声的登记），root 灯 = Selection——审查时注意"灯亮"现在表示"选中"而非"按住"
