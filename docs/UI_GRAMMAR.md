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

## 4. Page keys: a button is a door, and a second press is a page turn

John 2026-10-31: "页面打开逻辑也不一致：现在home按钮是在hero和grid间切换，arp和seq是进入页面，
再次按无效，env和scale都是在相应页面和hero间切换，比较混乱。统一改为按钮就是进入相应页面，
重复按进入同一功能的不同页面（现在每个功能只有一个页面，所以就是停留在原页面），
这样是不是更清晰？和glo+home的逻辑也一致"

He is right that GLO+HOME was the one coherent one, so it becomes the rule for
everything:

| press | result |
|---|---|
| the button of a page you are NOT on | go there, on that feature's remembered page |
| the button of the page you ARE on | turn to that feature's next page; with one page, stay put |
| HOME, from any other page | go to HOME (it is the way back, see below) |
| HOME, already on HOME | stay put |

**Every feature has a page count**, even when the count is 1. `ui_page[mode]`
holds the page last shown, so a feature with two pages comes back where it was
left rather than at page 1 -- the same thing GLOBAL already did with `gl_page`.

| key | page | pages today |
|---|---|---|
| HOME | M_HOME | 1 |
| SEQ | M_GRID | 1 |
| ARP | M_ARP | 1 |
| ENV | M_TRKSET | 2 |
| SEL | M_SCALE | 1 |
| GLO+HOME | M_GLOBAL | 2 |

TRACK SET became the second two-page feature on 2026-10-31 (its page 2 is
DIVISION and CLEAR TRACK - see below), so the table above is no longer "one page
each plus GLOBAL".

GLOBAL keeps working exactly as it did; it simply stops being the only page with
a page number. Its header (`GLOBAL 1/2`, `GLOBAL 2/2`) is the model for the rest:
when a feature grows a second page, its header says `SCALE 1/2` the same way.

### HOME is the way back, and that is not in conflict

HOME arrives from a page button and leaves from any other page. The asymmetry is
deliberate and John picked it: on the panel HOME is the button the hand reaches
for to get out, and a page key that also refused to leave would strand the player.
So HOME is the one key whose meaning depends on where you are -- but the meaning
is the same in both directions: *HOME goes to HOME*. From GRID it is a trip to
the hero; from the hero it is already there.

### EDIT is not a page key

EDIT toggles the GRID's focus layer (PIT/GAT/VEL/PROB/SEL) and has no page of its
own. It stays a layer switch, and only on GRID -- which is where the layer exists.

### Where the code puts it

`ui_page[]` and `ui_pages[]` live in `ui.c` next to `ui_mode`, and one function
does all of it:

```c
static uint8_t ui_page_key(uint8_t mode, uint8_t cur)
{
    uint8_t n = ui_pages[mode];
    if (cur != mode) { ui_mode = mode; return 1; }        /* the door */
    ui_page[mode] = (uint8_t)((ui_page[mode] + 1u) % n);  /* the turn */
    return 1;
}
```

Every page key routes through it: SEQ and ARP from `ui_edit.c`, SEL from
`ui_scale.c`, ENV from `ui_trkset.c`, GLO+HOME from `ui_global.c`. `gl_page` is
now `ui_page[M_GLOBAL]` spelled differently, so GLOBAL's own code and its tests
did not move -- the point was to stop it being a special case, not to rewrite it.

Two consequences worth stating because they were behaviour before:

- **GLOBAL arrives on the page it was left on.** Opening used to force page 1,
  on the theory that "opening never lands on page 2". But page 2 is the scan
  list and it starts itself when looked at, so a player working there who
  stepped out came back to the settings and had to turn again. "You arrive
  where you left" is one rule for every feature.
- **HOME still closes GLOBAL** even though HOME is also a page key. The page
  owns HOME while it is up (`global_button` returns 1 on a bare HOME release),
  and a chord on page 1 turns to page 2. Both paths exist and `tests/pages.txt`
  pins them.

---

## Test consequences

- `trkset2.txt` (new, 111): TRACK SET's second page - the page turn, the row
  dead stops, DIVISION's five values and their stops, and the popup in all four
  outcomes (OK on NO, OCT- cancel, HOME cancel, OK on CLEAR). It also sets
  PROGRAM/VOICE/ROUTE to non-default values, clears, and reads them back: the
  identity assertions on track 0 alone cannot catch a wipe that resets the
  program, because track 0's program IS the default.
- `tools/trkset2_probe.mjs` (new, 24 px checks): page 2 is a real page drawn at
  page 1's scale, the header says n/2, the footer names a different K2 on the
  CLEAR row, and Cancel restores the page byte for byte above y216.
- `pages.txt` (new, 48): each page key reaches its page, a second press stays
  put (one page each), HOME arrives from every page and leaves every page, and
  GLOBAL's page number is remembered across a trip to GLOBAL and back.
- `global.txt`: the BLE connect / let-go assertions move from SAVE to K2 detents;
  its page-number blocks now say which page they start on instead of trusting
  the chord to reset it.
- Probes that left a page with `press(SEL)` / `press(ENV)` now leave with HOME.
- `jam` assertions: SAVE still opens the menu; commit is OCT+, not SAVE.
- A popup must be **closable without committing**: assert that OCT- leaves the
  ring exactly as it was, not merely that the menu closed.
- Popups no longer expire: the idle-timeout assertions (if any) are removed.

---

## 5. A destructive action is a popup whose default is NO

CLEAR TRACK (TRACK SET page 2, 2026-10-31) is the first destructive action in the
UI, so it fixes the shape:

| | |
|---|---|
| the row | names the action, and K2 **right** opens the popup; K2 left does nothing |
| the popup | two rows, **NO first**, opened on NO |
| the exits | OCT+ OK / OCT- Cancel / HOME cancels - the same as every popup |
| the selection | dead-stops at both ends (the JAM menu's four cycle; a yes/no must not wrap past NO onto CLEAR) |
| the page | states the RULE (what a clear spares), on page 2 where the doubt arises |
| the popup | states the BILL (how many steps of which track), where the decision is made |

Two rules that came out of it:

- **A wipe is never armed by twisting left.** K2 left on the CLEAR row is a no-op,
  not "close the popup", because there is nothing to close yet and a left-twist
  that opened one would be the worst possible surprise on a knob you were just
  turning to see what it does.
- **The default is the safe answer, and OK on the default commits nothing.** The
  test asserts `ts_notes` is unchanged after OK-on-NO, not merely that the popup
  closed. A popup that closes and wipes anyway would pass a weaker assertion.

### What CLEAR TRACK clears

The notes, and nothing else (John 2026-10-31: "清一个轨不该把轨静默搬到 ch1 /
prog0"). `ds_clear_track()` is NOT the implementation: it memsets the whole
`ds_track`, so channel, program, division, route, voice mode and swing all snap
back to factory values and the track silently moves to ch1 with the piano. The
note fields are cleared by name instead, and the euclid meta (eu_on/k/eu_n/eu_rot)
goes with them because `ds_euclid_fill()` would otherwise repaint the pattern from
the old skeleton on the next call.

Sounding notes are left alone on purpose: each one already has its gate-off
scheduled in `eng.gates[]` keyed by track, so it ends on time whether or not the
step that started it still exists. Panicking would cut a note that was going to
end by itself in 20 ms.

---

## 6. Step record: who owns a key, and when a step is written

John 2026-10-31: "melody grid的step record有问题，只有在ofc on的情况下step record才有效". Report was exact; the blast radius was larger than the report.

**Who owns the key.** `scale_key_hook()` sits at the top of `panel_key_handler()`, above the GRID's `stepEntry`. A hook that consumes a key (`return 1`) owns it completely, so anything it does not do never happens. With CHORD KEYS off it consumed every key and only auditioned — the grid's stepEntry never ran, and nothing was ever written. CHORD KEYS off is the BOOT state, so this was every melody track from power-on.

**Three faults, one branch.** The OFC-off branch was missing both gates the OFC-on branch also lacked:

| gate | what it protects | what went wrong without it |
|---|---|---|
| `focus_layer` | the EDIT layer edits written voices | EDIT was dead whenever CHORD KEYS was off — no voice could be added or removed |
| `ui_mode` | only GRID/SCALE write steps | TRACK SET, which auditions the PROGRAM you are choosing, wrote a step |

Both gates now sit at the top of the hook, above the OFC dispatch, so they cannot diverge again. GRID and SCALE keep writing (the SCALE page has always been a second grid); every other page auditions only.

**When a step is written.** John's decision: one step per **gesture**, written when the **last** key comes up.

- Writing on press cannot express a chord: the second note would already be a step late.
- Writing on the last release makes keys that are down together one step, with no CHORD KEYS engaged.
- The player hears live audition either way, so the timing of the write is invisible in performance — only the grid differs.

The write itself is the GRID's contract, not a second rule: override the step, revive the gate, cursor +1 dead stop. It lives in `sc_off_write()`, next to the state it reads.

**Two sets, two jobs.** `sc_held[]` is what the page NAMES (pitches sounding, for the OFC-off readout). `rel[]`/`cap[]` are what the step will CONTAIN (pitches pressed, in press order). They are not the same set: a key whose snap lands on a pitch already down is the same pitch, not a second press, and must not eat a step.

### Test consequences

- `tests/steprecord.txt` (49) — the write, the chord-as-one-gesture, override-not-append, EDIT add and remove with CHORD KEYS off, TRACK SET not writing, CHORD KEYS on keeping the old press-time path, drums untouched.
- `tools/steprecord_probe.mjs` (7 px) — the same claims on the SHIPPED wasm, by changed cells. Verified to FAIL (2/7) against the pre-fix build, so it is a test of the bug and not of the code.

### The gate is on the write, not on the key — regression, and why

The first cut of the step-record fix (5f5e317) opened `scale_key_hook()` with

```c
if (ui_mode != M_GRID && ui_mode != M_SCALE) return 0;
```

That is the wrong place for the question. It reads as "these pages own keys",
but the question the pages actually differ on is narrower: **which pages write a
step**. Putting it on the key meant the chord layer stopped owning keys anywhere
else, so on the hero, in TRACK SET, and on GLOBAL every press fell through to the
plain audition path: one raw key, no snap, no chord. John reported it as
"回到hero后自动退出了ofc" — the setting was still ON; the sound had been taken
away from it. That the two symptoms arrived as one report is the tell: a page
switching the setting off and a page silencing the setting look identical from
the player's seat.

The two claims are different and must not share a gate:

- **the chord layer is how the instrument sounds** — every page, no exception.
  This is not a grid feature that leaks; it is the keyboard's own behaviour.
- **writing a step is what the grid pages do** — `M_GRID` and `M_SCALE` only.

`sc_write_ok()` asks the second question, and it is asked by the two functions
that write (`sc_off_write()`, `sc_grid_write()`), never by the key. `focus_layer`
(the EDIT fade-in) stays on the key, because it is about who receives the press,
not about what gets recorded.

The lesson generalises to the rest of the UI: when a new rule appears to be about
"which page is this", find the ACTION it constrains first, and gate that. A gate
on the input is almost always wider than the rule it was meant to express.

### Test consequences

- `tests/chordsound.txt` (28) — CHORD KEYS on, MAJ: the chord sounds from the
  SCALE page AND from the hero, note for note (`jam_notes` 4 = root + third +
  fifth + seventh, not the 1 that `jam_used` would report for the same slot);
  the hero and TRACK SET sound but do not write; the GRID still writes; the
  setting survives every page change. Reverse-verified: restoring the wide gate
  fails 4.
- `tools/chordsound_probe.mjs` (7) — the same claims on the shipped wasm, by the
  MIDI that leaves the instrument. The hero draws no live-note readout (a
  picture of the PATTERN, not of the keyboard), so sound is the only honest
  witness: the probe asserts the hero emits `[67,71,74,81]`, the same four notes
  the SCALE page emits. Reverse-verified: restoring the wide gate fails 3.
