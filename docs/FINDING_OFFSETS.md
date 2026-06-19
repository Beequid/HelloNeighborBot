# Finding Offsets — filling in `config/config.json`

This is the doc that matters most for getting HelloNeighorBot working on **your**
copy of *Hello Neighbor* (Old Patch 1.1.6, `HelloNeighbor-Win64-Shipping.exe`).

Memory offsets are build-specific and are **not** shipped. The bot is entirely
config-driven: you find `GWorld` and the pointer chain to the player's location
`FVector`, then write those into `config/config.json`. Start by copying the
template:

```
config/config.example.json  ->  config/config.json
```

The DLL searches for `config.json` next to the DLL, then in the game directory,
then at the path in the `HNBOT_CONFIG` environment variable.

> **Every value in `config.example.json` is a PLACEHOLDER.** The `rva`,
> `pattern`, and the `location_chain` numbers there are made up. They will not
> work on your build. You must replace them.

> **Wrong offsets do not crash the game.** Every memory access goes through
> SEH-guarded reads/writes (`mem::SafeRead`/`SafeWrite`). If `GWorld` resolves to
> garbage or a chain is wrong, `SdkAccess::GetLocation` simply returns `false`
> and the menu shows "SDK not ready" / no live coords. You can iterate on the
> numbers safely — at worst nothing moves.

---

## What you are looking for

Two independent things go into the config:

1. **Where `GWorld` lives** — a single static pointer inside the game module.
   Configured under `resolve.gworld`.
2. **How to get from `GWorld` to the player's location `FVector`** — a Cheat
   Engine-style pointer chain. Configured as `player.location_chain`.

`velocity_chain` and `rotation_chain` are **optional**. Leave them as empty
arrays (`[]`) and the corresponding getters/setters just return `false`; the bot
still walks/teleports using `location_chain` alone.

---

## Pointer-chain semantics (read this before you do anything)

The chain is applied from the **GWorld variable address**, which the bot computes
as:

```
gworldVarAddr = module_base + resolved_gworld
```

`gworldVarAddr` is the address of the *static slot that holds the `UWorld*`* — it
is **not** the `UWorld*` itself yet.

`mem::ReadChain(base, offsets)` then walks the chain like Cheat Engine does:

```
cur = base                         // = gworldVarAddr
for each offset i:
    cur = cur + offsets[i]
    if i is NOT the last offset:
        cur = *(uintptr_t*)cur     // dereference (SEH-guarded; 0 on fault)
// return cur as an ADDRESS — the last offset is added but NOT dereferenced
```

Consequences that drive how you write `location_chain`:

- **The first offset is almost always `0x0`.** Adding `0x0` to `gworldVarAddr`
  and then dereferencing yields the live `UWorld*`. That is the standard
  "deref GWorld" step.
- **The last offset is the field offset of the `FVector` itself.** It is added
  to the final object pointer but **not** dereferenced — `ReadChain` returns that
  address, and `GetLocation` reads 12 bytes (`FVector` = 3 floats) there;
  `SetLocation` writes 12 bytes there.
- Every middle offset is "add, then follow the pointer".

So a chain like:

```json
"location_chain": [ "0x0", "0x180", "0x38", "0x250", "0x130" ]
```

means: deref GWorld -> +0x180 deref -> +0x38 deref -> +0x250 deref -> +0x130
(that last one lands on the X float of the location FVector).

Offsets may be written as hex strings (`"0x180"`) or as plain numbers; both
parse. `_comment` keys anywhere in the JSON are ignored.

---

## Approach A — Cheat Engine pointer scan (recommended, no SDK needed)

This is the easiest path and needs no UE4 knowledge. The idea: find the live
address of the player's coordinates, then have Cheat Engine find a stable pointer
path that ends at a static in the game module — which we then express relative to
`GWorld`.

### A1. Find the player's location FVector

1. Launch the game, load a level, open Cheat Engine and attach to
   `HelloNeighbor-Win64-Shipping.exe`.
2. Set the value type to **Float**.
3. The location is an `FVector` = **three consecutive floats, 4 bytes apart**
   (X at `+0x0`, Y at `+0x4`, Z at `+0x8`). The classic way to isolate one of
   them:
   - **First Scan** with scan type *Unknown initial value*.
   - Move your character in-game along one axis, then *Next Scan* →
     *Changed value*. Stand still, *Next Scan* → *Unchanged value*. Repeat,
     moving and standing, until the list is small.
   - Right-click a candidate → *Browse this memory region* and look for **three
     floats in a row** that all look like world coordinates. Moving forward
     changes one or two of them sensibly; jumping changes the vertical one.
4. Confirm: edit a value and watch your character jump in-game. Once you are sure,
   note the address of **X** (the first of the three floats). That is the FVector
   base for the next step.

### A2. Pointer-scan down to a module static

The address from A1 is dynamic (it changes every launch), so we need a stable
path to it.

1. Right-click the X address → **Pointer scan for this address**.
2. In the pointer-scan options:
   - Keep a reasonable max level (start ~5–7) and a sane max offset.
   - **Important:** restrict the scan so it ends inside the game module's static
     region. Tick *"Pointer must end with specific offsets"* if you already know
     the FVector field offset, and prefer results whose final base is inside
     `HelloNeighbor-Win64-Shipping.exe` (a "green"/static address), ideally the
     one corresponding to **GWorld**.
3. Run it, then **restart the game**, attach again, repeat A1 to get the new X
   address, and use *Pointer scan* → *Rescan memory* with the new address to
   throw away unstable paths. Do this 2–3 times until only stable chains remain.
4. Pick a result whose **base address is a static in the module** and, ideally,
   matches the address you find for `GWorld` (see "Confirming GWorld" below).
   Cheat Engine shows it as `module+OFFSET` plus a list of offsets:
   `[[[base + o1] + o2] + o3] + finalFieldOffset`.

### A3. Convert the CE result into config

Say Cheat Engine gives you:

- a static base shown as `"HelloNeighbor-Win64-Shipping.exe"+0x03ABC120`
  (this is the GWorld static), and
- offsets, listed outermost-first as in CE's pointer view, e.g.
  `0x180, 0x38, 0x250, 0x130` where `0x130` is the FVector field.

Then:

**GWorld static → `resolve.gworld` (rva mode):**

```json
"resolve": {
  "gworld": {
    "mode": "rva",
    "rva": "0x3ABC120"
  }
}
```

`rva = foundStaticAddress - moduleBase`. When CE shows `module+OFFSET`, that
`OFFSET` *is* the RVA — use it directly. The bot computes
`gworldVarAddr = module_base + rva`.

**The CE offsets → `location_chain`:**

```json
"player": {
  "location_chain": [ "0x0", "0x180", "0x38", "0x250", "0x130" ],
  "velocity_chain": [],
  "rotation_chain": []
}
```

Notes on the conversion:

- **Prepend `0x0`** as the first offset — that is the "dereference GWorld to get
  the `UWorld*`" step. Cheat Engine's pointer-scan base already *is* the GWorld
  slot, so its first listed offset corresponds to your second chain entry.
- **List the offsets from the GWorld slot outward to the field.** Cheat Engine's
  pointer browser shows them in the same order it dereferences; keep that order.
- **The last entry is the FVector field offset** (the X float). It is added but
  not dereferenced — exactly what `GetLocation`/`SetLocation` need.
- If the FVector you found is not directly hanging off the chain CE produced
  (e.g. CE's last offset lands one pointer short), add the field offset as an
  extra final entry yourself.

`velocity` and `rotation` are optional. If you want them, repeat A1–A3 against
the velocity vector and the rotation (`FRotator` = Pitch/Yaw/Roll floats), and
fill `velocity_chain` / `rotation_chain` the same way. Otherwise leave `[]`.

---

## Approach B — Dumper-7 / UE4 SDK generator

If you would rather work from real class layouts, dump the SDK and derive offsets.

### B1. Dump

1. Run **Dumper-7** (or another UE4 SDK generator) against the game to dump
   `GWorld`, `GNames`, `GObjects`, and class layouts.
2. From the dump you get:
   - the **RVA / pattern for `GWorld`**, and
   - the **field offsets** along the path to the player's location, e.g.
     `UWorld -> OwningGameInstance -> LocalPlayers[0] -> PlayerController ->
     Pawn -> RootComponent -> RelativeLocation` (the exact path depends on this
     build; use the dumped layouts to read each offset).

### B2. Configure GWorld — RVA mode

If the dumper gives you a fixed RVA for the GWorld static:

```json
"resolve": {
  "gworld": { "mode": "rva", "rva": "0x3ABC120" }
}
```

### B3. Configure GWorld — pattern (AOB) mode

If you prefer a signature that survives small relocations, use `mode: "pattern"`.
The bot AOB-scans the module for the pattern, then optionally resolves a
RIP-relative reference at the match.

For a typical RIP-relative load such as `48 8B 05 ?? ?? ?? ??`
(`mov rax, [rip+disp32]`, the canonical GWorld access):

```json
"resolve": {
  "gworld": {
    "mode": "pattern",
    "pattern": "48 8B 05 ? ? ? ? 48 3B 45 ? 74",
    "rip_relative": true,
    "pattern_offset": 3,
    "instruction_length": 7
  }
}
```

How the bot interprets these (from `mem::ResolveRipRelative`):

- **`pattern`** — IDA-style signature. `?` or `??` is a wildcard byte. The first
  match in the module is used.
- **`pattern_offset`** — byte offset, from the start of the match, to the 4-byte
  signed displacement. For `48 8B 05 ?? ?? ?? ??`, the three opcode/ModRM bytes
  `48 8B 05` come first, so the disp32 starts at offset **3**.
- **`instruction_length`** — total length of the instruction. For
  `48 8B 05 ?? ?? ?? ??` that is **7** bytes.
- **`rip_relative: true`** — the bot computes
  `target = matchAddr + instruction_length + (int32)disp`, i.e. RIP points at the
  *next* instruction. That `target` is the GWorld variable address.

If instead you want the match itself (plus a fixed offset) and **not** a
RIP-relative resolve, set `"rip_relative": false`; then
`gworldVarAddr = matchAddr + pattern_offset`. Use this only when your pattern
lands directly on the static, which is unusual for UE4.

### B4. Location chain from dumped offsets

Build `location_chain` from the dumped field offsets, prepending `0x0` to deref
GWorld and ending on the `RelativeLocation` (or equivalent) FVector field — same
rules as Approach A3. Array-index hops (e.g. `LocalPlayers[0]`) are just another
`add + deref` step; account for element size if the index is non-zero.

---

## Confirming GWorld is right

Whichever approach you used, the GWorld slot should hold a pointer that, when
dereferenced, is the live `UWorld*`. In Cheat Engine: go to your resolved
`module+rva` address; the 8 bytes there should be a heap pointer that changes
between levels but stays valid in-game. If it reads as `0`/garbage while you are
in a level, the RVA or pattern is wrong. (The bot reads GWorld through
`mem::Read<uintptr_t>`, so a bad value never crashes — it just yields no
location.)

---

## Testing your config

1. Save `config/config.json` with your real values.
2. Inject the DLL (run `injector.exe`, or your usual injector).
3. If `enable_console` is `true`, watch the console: `SdkAccess::Init` logs the
   resolved GWorld variable address, or an error if the module / pattern was not
   found.
4. Press **INSERT** (default `toggle_menu`) to open the ImGui menu. It shows
   **SDK ready** state and **live location**.
5. **Walk around in-game and confirm the X/Y/Z in the menu change as you move.**
   That is the definitive check that `location_chain` is correct. If the coords
   stay blank or never change, the chain or GWorld resolution is wrong — fix the
   numbers and press **F10** (`reload_config`) to reload without re-injecting.

Once live coords track your movement, capture waypoints (**F8**) and the bot can
teleport/walk the route.
