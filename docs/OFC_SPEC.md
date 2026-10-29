# OFC（One-Finger Chords）完整规格 — 供审查

来源：`ui/ui_scale.c`（逐段核对源码，2026-10-29，wasm `e1f7b547`）。
审查提示：所有行为都标注了源码依据（函数名/分支），发现与实际手感不符处直接指出来。

---

## 1. 总开关与作用范围

| 项 | 值 | 依据 |
|---|---|---|
| OFC 开关 | SCALE 页 K3（左扭=OFF，右扭=ON，死停不循环） | `scale_encoder` EN_K3 |
| 默认值 | OFF（`static uint8_t sc_ofc;` = 0） | 源码 init |
| 关闭 OFC 时清空 | `sc_mask = 0`（已按住的成分位全部作废） | `scale_encoder` |
| 生效范围 | **所有页面**（hook 在 `panel_key_handler` 最顶部，无 ui_mode 判断）；**鼓轨除外**（ch9 全 raw）；**EDIT 层除外**（编辑已写入 voice 时 hook 不接管） | `ui.c:183`、`scale_key_hook` |
| OFC OFF 时按键 | 单音，过 `ui_snap` 吸附到当前 scale | `scale_key_hook` snap 分支 |
| LED | OFC 开时 8 个成分键（19..26）常亮 + 按住的 root 键亮 | `scale_leds` |

## 2. 键位映射（27 键，OFC ON 时三分区）

| 键 | 区 | 功能 |
|---|---|---|
| 0..6 | 左手 7 键 | 级数 I..VII（scale 相对） |
| 7..18 | 中 12 键 | root，C4..B4 半音序（`sc_root_note`: 60 + key − 7 + key_shift） |
| 19..26 | 右 8 键 | 成分（chord flavors），见 §4 |

- OCT± 按钮 = `key_shift`（±24 上限），影响 root 音高与 OFC OFF 时的音符。
- key 0..6 在 OFC OFF 时是普通琴键（F3 起半音序，`53 + key + key_shift`），OFC ON 时变级数键——**不再发声单独的音**。

## 3. 左手：级数走位（key 0..6）

- **触发条件：必须已有 root 按住**（`sc_root_key != 0xFF`）。没有 root 时按级数键 = 无声、无状态（返回 1 吞掉）。
- 按下 → `sc_deg_key = key`，实时换配和弦：`sc_degree_chord` = 当前 scale 的调内音堆叠三度到七音（4 音：1、3、5、7 级），degree root 取**离按住 root 最近八度**（±6 半音内找最短路径）。
- 成分键的修饰照常叠加（`sc_flavor`）。
- **抬键 → 静音**（2026-10-29 John 规则：松手不发声）。不回落到 root 和弦。
- 同按多个级数键：直接切换到新按的级数（`sc_deg_key != key` 允许覆盖）；同键重复按忽略。
- 级数性质由 scale 决定（如 MAJ scale：I maj7、ii m7、iii m7、IV maj7、V7、vi m7、vii m7♭5——全部堆叠到七音）。

## 4. 右手：root + 成分（key 7..26）

### 4.1 root（key 7..18）

- 按下：若当前无 root → 接管为 root，发声。**已有 root 时按第二个 root = 忽略**（一只左手）。
- 抬键：清 root、清级数、**静音**。成分位保留（还物理按着的话），下一个 root 落下时立即重新配音。
- **root 单按（无成分）= 一个吸附音**（过 `ui_snap`，跟随当前 scale），不是三和弦。
- 换 root（先抬后按）= 新和弦完整重新打击。

### 4.2 成分（key 19..26，按 v3.7 人体工学序）

| key | 键名 | 成分 | 黑/白 | 单按语义 |
|---|---|---|---|---|
| 19 | C5 | **MAJ** | 白 | 大三和弦（其实 MAJ 是"无位"——implied，见 §5） |
| 20 | C#5 | **7** | 黑 | 属七（0,4,7,10） |
| 21 | D5 | **M7** | 白 | 大七（0,4,7,11） |
| 22 | D#5 | **MIN** | 黑 | 小三（0,3,7） |
| 23 | E5 | **DIM** | 白 | 减三（0,3,6） |
| 24 | F5 | **ADD9** | 白 | 加九（0,4,7,14） |
| 25 | F#5 | **SUS4** | 黑 | 挂四（0,5,7） |
| 26 | G5 | **6** | 白 | 六和弦（0,4,9） |

- 排序原则（v3.7 John 拍板）：常用组合相邻——MAJ+7（属七）、MAJ+M7（大七）、MIN+7（相邻两黑键一指滚）、MIN+DIM（m7♭5）、MIN+ADD9（m9）。
- **无 root 按成分 = 只登记（LED/图例亮），不出声**；root 落下瞬间成型。
- **按下（有 root）= 全和弦重新打击**（`sc_restrike`：先全 off 再全 on，不是只补 delta 音）。
- **抬键 = 静音**（不回落到 root 三和弦）。
- 已按住再按同键 = 忽略（`!(sc_mask & bit)` 守卫）。

### 4.3 组合规则（`oc_resolve`）

三类槽位冲突即**整个静音**（`c.n == 0` → 无声，等价于松手）：

- 三度槽：MIN / DIM / SUS4 互斥（MAJ 例外，见 Aug）
- 七度槽：7 / M7 互斥
- 6 与任何七度互斥

有效组合表（root=C 为例）：

| 按键 | 和弦 | 音 |
|---|---|---|
| MAJ（或无成分） | — | MAJ 位是"隐含大三度"，单按 = 大三和弦 0,4,7 |
| 7 | C7 | 0,4,7,10 |
| M7 | Cmaj7 | 0,4,7,11 |
| MIN | Cm | 0,3,7 |
| MIN+7 | Cm7 | 0,3,7,10 |
| MIN+M7 | CmMaj7 | 0,3,7,11 |
| MIN+DIM | Cm7♭5 | 0,3,6,10 |
| MIN+ADD9 | Cmadd9 | 0,3,7,14 |
| DIM | Cdim | 0,3,6 |
| DIM+7 | Cm7♭5 | 0,3,6,10 |
| DIM+M7 | Cdim maj7 | 0,3,6,11 |
| **DIM+MAJ** | **Caug** | 0,4,8（2026-10-29 新增：三度回大、五度 #5） |
| SUS4 | Csus4 | 0,5,7 |
| SUS4+7 | C7sus4 | 0,5,7,10 |
| SUS4+M7 | Cmaj7sus4 | 0,5,7,11 |
| 6 | C6 | 0,4,9 |
| ADD9 | Cadd9 | 0,4,7,14 |
| ADD9+7 | C9 | 0,4,10,14（省五音） |
| ADD9+M7 | Cmaj9 | 0,4,11,14（省五音） |
| ADD9+MIN | Cmadd9 | 0,3,7,14 |
| ADD9+MIN+7 | Cm9 | 0,3,10,14（省五音） |
| MIN+SUS4 / 7+M7 / 6+7 / 6+M7 | 静音 | 槽位冲突 |
| 5 音封顶 | n==5 砍五音 | `oc_resolve` n==5 分支 |

## 5. MAJ 位的特殊性（审查重点）

MAJ 是 8 个成分之一（bit 0），但 `oc_resolve` 的三度槽**不含 MAJ**——它只在两处起作用：

1. **DIM+MAJ = Aug**（§4.3）
2. **级数走位的 flavor**（`sc_flavor`）：MAJ 位强制三度回大（scale 堆叠的 iii/vi 级本是小三，按 MAJ 纠正成大三）

所以"单按 MAJ"和"什么都不按"在 root 和弦上是**同一个音**（0,4,7）——MAJ 灯亮但音不变。这是有意设计（MAJ 作为"默认三度"的显式表达），审查时请确认你接受这个语义。

## 6. GRID 写入（录音行为）

- **仅成分按下且 root 已按住且当前在 GRID 页**时触发：把当前和弦写到 cursor 所在 step（替换该 step 全部 voice），`st->on = 1`，cursor+1（63 封底）。
- root 按下、成分抬起、级数按下/抬起**都不写**。
- 写入的和弦由 seq 回放——在 demo 播放中测试时，听到的"再次发声"可能是回放而非 live（之前误判过一次）。
- 成分按下时若解析结果无效（静音）：**不写 step**（`sc_tones_n == 0` 提前返回）。

## 7. SCALE 页三设置（影响级数与 snap）

| 旋钮 | 设置 | 范围 | 默认 |
|---|---|---|---|
| K1 | scale | MAJOR / MINOR / DORIAN / MIXOLYDIAN / HARM.MINOR / PENT.MAJOR / PENT.MINOR / BLUES / CHROMATIC（死停不循环） | **CHROMATIC** |
| K2 | tonic | C..B（0..11 死停） | C |
| K3 | OFC | OFF/ON 死停 | OFF |
| K4 | 空闲 | — | — |

scale/tonic 影响：① OFC OFF 时的单音吸附 ② root 单按的吸附音 ③ 级数走位的和弦内音。**不影响**成分组合和弦的音程（那些是固定半音结构，"flavor tones: no snap (deliberate)"）。

### SC_SET 掩码（半音位，bit0=C）

| scale | 掩码 | 含音 |
|---|---|---|
| MAJ | 0x0AB5 | 0 2 4 5 7 9 11 |
| MIN | 0x05AD | 0 2 3 5 7 8 10 |
| DOR | 0x06AD | 0 2 3 5 7 9 10 |
| MIX | 0x06B5 | 0 2 4 5 7 9 10 |
| HMI | 0x09AD | 0 2 3 5 7 8 11 |
| PMAJ | 0x0295 | 0 2 4 7 9 |
| PMIN | 0x04A9 | 0 3 5 7 10 |
| BLU | 0x04E9 | 0 3 5 6 7 10 |
| CHR | 0x0FFF | 全部 |

## 8. 状态生命周期速查

| 事件 | sc_root_key | sc_deg_key | sc_mask | 声音 |
|---|---|---|---|---|
| root 按下 | = key | 不变 | 不变 | 单音（snap）或和弦（有成分） |
| root 抬起 | 0xFF | 0xFF | **保留** | 静音 |
| 第二个 root 按下 | 不变 | 不变 | 不变 | 忽略 |
| 级数按下（需 root） | 不变 | = key | 不变 | 级数和弦（flavor 叠加） |
| 级数抬起 | 不变 | 0xFF | 不变 | 静音 |
| 成分按下 | 不变 | 不变 | \| bit | 全和弦重击（或无效=静音） |
| 成分抬起 | 不变 | 不变 | & ~bit | 静音 |
| K3 OFF | 不变 | 不变 | 0 | — |
| 鼓轨 / EDIT 层 | hook 不接管（原逻辑直通） | | | |

## 9. 已知的"奇怪但有意"点（审查清单）

1. MAJ 单按与无成分同音（§5）——灯亮音不变。
2. 成分和弦**不做 scale 吸附**（故意：C#maj 里出 E# 等音是 OK 的，pc 语义）。
3. 级数和弦固定 4 音（堆到七音），没有纯三和弦模式。
4. 级数 root 八度选择 = 离按住 root 最近（可能比 root 高也可能低）。
5. 无效组合的反馈 = 无声 + 图例灯亮（没有额外的屏上提示）。
6. OFC ON 时 key 0..6 从"琴键"变"级数键"——低音区不能弹单音（要弹低音得用 root 区 + OCT-）。
7. GRID 写入只认成分按下瞬间，root 按下不写——单按 root 想写单音 step 得走老路（OFC OFF 或 EDIT 层）。
8. jam capture 只挂在 sc_apply / snap 分支（OFC 和弦走 `sc_apply` → 会进 jam ring）。

## 10. 未验证/待真机确认

- 全部手感结论来自 host 探针 + 源码；真机（fwsc `ff34f3e3` 未刷）行为理论上一致，但 LED/键扫描延迟未实测。
- `tests/scale.txt` 基准：ofc_mask 263 (MAJ+root7) / 828 / 6154；DSL 的 expect 比较坏（FAIL 前缀），以 got 值为准。
