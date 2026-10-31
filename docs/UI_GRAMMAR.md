# UI interaction grammar (2026-10-31)

John: "UI/UX的统一：设定页面k1 row，k2 value，所以ble连接不应该用save按钮，而应该用k2来连接/断开。
多个选项统一用类似preset的POPUP，用OCT+确认、oct-取消，也就是OK/cancel，所以jam的save在按下后
弹出四个选项，k1选择，oct+确认并关闭，oct-1取消并关闭。确认是否理解，其他操作是否还有需要统一的"

Three rules, and they are one rule seen three ways: **a row is a thing, a knob is
a number, and a decision is a popup.**

---

## 1. Setting pages: K1 picks the row, K2 edits its value

Every page that is a list of settings obeys this, and the split is fixed:

| knob | job |
|---|---|
| K1 | the row. Dead stop at both ends, never wraps. |
| K2 | the value in that row. Dead stop at both ends unless the value is a list that means to cycle. |
| K3 | the page's own odd control (MUTE/SOLO on TRACK SET, GATE/LATCH on SCALE). Not a row. |
| K4 | free, except SCALE where it is the chord *kind*'s partner... (see the page's spec) |
| SELECT | leaves the page's list alone: it walks tracks (TRACK SET) or open/closes the page (SCALE). |

A row whose value is a **verb** rather than a number (connect, disconnect, scan)
puts that verb on **K2** like any other value: K2 right asks for the verb, K2 left
asks for its opposite. It does not get its own button.

**Consequence (the change this spec makes):** GLOBAL page 2's BLE link row no
longer uses SAVE. SAVE is not a confirm key anywhere (rule 3), and connecting is
not an exception: K2 right = connect the selected device, K2 left = let it go.
Row 0 (the radio switch) was already K2's and stays.

*Why the row exists at all when K2 could just connect the highlighted device:*
the row says what the link is doing ("CONNECTED / PAIRING / LET GO"). Selecting a
device with K1 is navigation, not a commitment -- that is the same distinction
K1/K2 already draws everywhere else.

---

## 2. Multi-choice decisions are popups, and only popups

A popup is a decision: it has options, a highlight, and two ways out.

| control | meaning |
|---|---|
| K1 | move the highlight (dead stop; K2..K4 do the same, so the hand can grab any knob) |
| **OCT+** | **OK**: do the highlighted thing, close |
| **OCT-** | **Cancel**: do nothing, close |
| HOME | also cancels (the panel's universal "back") |
| SAVE | **nothing.** It is not a confirm key, not a shortcut, not an alternative. |

The preset popup is the visual reference (240 px wide, the three-line
neighbourhood, own footer). A decision popup may be narrower; it must not look
like a different apparatus.

Popups take the four knobs while they are up, and take the two OCT buttons. They
do **not** take the keybed: playing while a popup is up still sounds, because the
popup is a decision about a value, not a modal dialog.

### OCT+ / OCT- is contextual, and only inside a popup

| no popup | popup up |
|---|---|
| OCT+ / OCT- shift the keybed by an octave (GLO+OCT+ cycles rest/tie on the cursor step) | OCT+ = OK, OCT- = Cancel |

Nothing else changes: with a popup down, the octave keys are the octave keys.

### The first popup: JAM save

SAVE opens it (SAVE's remaining job is "open a menu", which is not confirming
anything). Four options in the existing order:

1. CLEAR      `ds_jam_reset`
2. MERGE      `ds_jam_save_merge`
3. REPLACE    `ds_jam_save_replace`
4. NEW TRK    `ds_jam_save_new`

K1 highlights, OCT+ commits and closes, OCT- closes and commits nothing. The
toast that follows a commit (`JAM MERGED`, `JAM TO 3`, `RING EMPTY`) is unchanged:
it reports what happened, it does not ask anything.

**This replaces:** SAVE-as-confirm inside the menu, and the 3-second idle
timeout that silently closed it. A decision with options that can silently
expire is the thing this spec is trying to stop. HOME still cancels.

---

## 3. SAVE

SAVE never confirms. Its one job left is opening the JAM menu, which is an *entry*
to a decision, not the decision. GLOBAL page 2 stops using it entirely.

---

## What else was inconsistent (audited 2026-10-31)

| place | was | now | why |
|---|---|---|---|
| GLOBAL 2, BLE device row | SAVE connects / disconnects | K2 right = connect, left = let go | rule 1: a verb is a value |
| GLOBAL 2, link row | SAVE released the link | K2 left | same |
| JAM menu | SAVE = confirm, HOME = cancel, 3 s idle closed it | OCT+ = OK, OCT- = Cancel, HOME = Cancel, no idle close | rule 2 |
| JAM menu highlight | K1..K4 all moved it | unchanged (all four still do) | a popup is grabbed by whichever knob is under the hand |
| GLOBAL 1 | K1 row / K2 value | unchanged | already the rule |
| TRACK SET | K1 row / K2 value | unchanged | already the rule |
| SCALE | K1 kind / K2 tonic / K3 OFC | unchanged | already the rule (the page's list is three values, no row index) |
| PRESET popup | K1? no: it is a *readout*, not a decision | unchanged | it has no options and no cancel: it shows a value you already changed. It is not a popup in this spec's sense |
| platform RESTORE FROM USB? (`SEL yes / HOME no`) | SEL confirms | **out of scope here** -- platform code, and it runs at boot with the app's own screen not yet up. Recorded as an open item, see below |

### Open items this spec does NOT change

- **`RESTORE FROM USB? SEL yes / HOME no`** is in `platform/device/unity.c`
  (`rs_ask`) and `backup.c`. It is the platform's, it runs before the app owns the
  screen, and making it obey OCT+/OCT- means changing the platform's backup
  protocol. Flagged, not touched.
- **The PRESET popup** is a readout that follows a knob you have already turned;
  it has nothing to confirm or cancel. Left as is. If it should *block* the value
  until confirmed, that is a different feature (a picker, not a readout), and it
  would need its own decision about what "cancel" restores.
- **GLO+HOME, GLO+OCT+, GLO+TEMPO** are chords, not menus. They stay.

---

## Test consequences

- `global.txt`: the BLE connect / let-go assertions move from SAVE to K2 detents.
- `jam` assertions: SAVE still opens the menu; commit is OCT+, not SAVE.
- A popup must be **closable without committing**: assert that OCT- leaves the
  ring exactly as it was, not merely that the menu closed.
- Popups no longer expire: the idle-timeout assertions (if any) are removed.
