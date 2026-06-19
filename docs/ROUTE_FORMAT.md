# Route File Format

HelloNeighorBot drives the player along a saved **route**: an ordered list of
waypoints that the `RouteEngine` walks or teleports between. Routes are plain
JSON files, loaded from the path given by `route_path` in `config.json` and
written back by **Save Route** in the in-game menu (`SaveRoute` emits this exact
shape, pretty-printed).

The reference example is
[`config/routes/act1.example.json`](../config/routes/act1.example.json).

## Top-level schema

```json
{
  "name": "Hello Neighbor 1.1.6 - Act 1 (example)",
  "loop": false,
  "waypoints": [ /* ... */ ]
}
```

| Key         | Type    | Meaning                                                                 |
|-------------|---------|------------------------------------------------------------------------|
| `name`      | string  | Human-readable route name, shown in the menu.                          |
| `loop`      | bool    | When the last waypoint is reached: `true` resets to index 0 and keeps running; `false` finishes the run. |
| `waypoints` | array   | Ordered list of waypoint objects (see below). The bot runs them in order. |

Any `_comment` key is ignored, so you can leave notes in the file.

## Waypoint schema

```json
{ "name": "window-ledge", "x": 1800.0, "y": 900.0, "z": 450.0,
  "mode": "teleport", "tolerance": 150.0, "wait_ms": 300, "action": "split" }
```

| Key         | Type                       | Meaning                                                                 |
|-------------|----------------------------|------------------------------------------------------------------------|
| `name`      | string                     | Label for the waypoint (menu/editor only).                             |
| `x`,`y`,`z` | number                     | Target position as a UE4 world-space `FVector`, in centimeters.        |
| `mode`      | `"teleport"` \| `"walk"`   | How the bot reaches this point (see below).                            |
| `tolerance` | number                     | Arrival radius in cm — the point counts as reached when the player is within this distance. |
| `wait_ms`   | integer                    | Milliseconds to pause at the point after arriving, before advancing.   |
| `action`    | string (optional)          | Currently `"split"` marks a timer split at this waypoint. Omit for none. |

## `teleport` vs `walk`

The two modes differ only in how the engine moves the player toward `x,y,z`:

- **`teleport`** — snaps the player straight to the target in a single tick
  (`SetLocation(wp.pos)` once), then waits `wait_ms` and advances. Instant; no
  collision or path following.
- **`walk`** — glides toward the target a little each tick. Per tick the step is
  `move_speed * dt` (seconds), **capped** by `teleport_step` so a single tick can
  never jump more than that distance. (`move_speed` and `teleport_step` come from
  `settings` in `config.json`.) The waypoint is considered reached once the
  player is within `tolerance` of the target, after which it waits `wait_ms` and
  advances. Use `walk` where a smooth approach matters; `teleport` for raw speed.

`tolerance` and `wait_ms` apply to both modes (teleport effectively arrives
immediately, so its `tolerance` rarely matters in practice, but `wait_ms` still
pauses there).

## Coordinates

`x`, `y`, `z` are **UE4 world units (centimeters)** in the same space the SDK
reads/writes via the player location pointer chain. They are awkward to author
by hand. The intended workflow is **in-game capture**:

1. Start the game with the DLL injected and load the relevant area.
2. Walk to a point on your route and press the capture hotkey (**F8** by
   default). This appends a waypoint at your current location using the default
   mode/tolerance.
3. Repeat for each point, tweak modes/tolerances/waits/splits in the menu's
   waypoint editor, then click **Save Route** to write the JSON.

This guarantees the coordinates match the running build's world space.

## Annotated example

```jsonc
{
  "name": "Hello Neighbor 1.1.6 - Act 1 (example)",
  "loop": false,                 // run once, then Finish
  "waypoints": [
    // Smooth glide out of spawn; large tolerance, no pause.
    { "name": "spawn",        "x": 0.0,    "y": 0.0,    "z": 200.0,
      "mode": "walk",     "tolerance": 150.0, "wait_ms": 0 },

    { "name": "front-yard",   "x": 1200.0, "y": 300.0,  "z": 200.0,
      "mode": "walk",     "tolerance": 150.0, "wait_ms": 0 },

    // Snap up onto a ledge (walking can't climb), then pause 300ms.
    { "name": "window-ledge", "x": 1800.0, "y": 900.0,  "z": 450.0,
      "mode": "teleport", "tolerance": 150.0, "wait_ms": 300 },

    // Teleport into the basement; pause to grab the key; mark a split.
    { "name": "basement-key", "x": 2400.0, "y": 1500.0, "z": -100.0,
      "mode": "teleport", "tolerance": 150.0, "wait_ms": 500, "action": "split" },

    // Walk to the door and split again.
    { "name": "basement-door","x": 2600.0, "y": 1700.0, "z": -100.0,
      "mode": "walk",     "tolerance": 150.0, "wait_ms": 0,   "action": "split" }
  ]
}
```
