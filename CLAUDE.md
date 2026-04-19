# Iron Fist 3D — project notes

Native macOS FPS built in C with raylib. Single-file source (`game.c`) +
procedurally-generated resources + WolfenDoom / classic-games sprites & sounds.
Runs as an `.app` bundle with a custom icon.

## Build & run

```
./run.sh           # kills any running instance, `make -B`, `open IronFist3D.app`
make               # just build
make run           # build + open
make clean         # wipe the .app
```

`run.sh` ALWAYS does a full recompile (`make -B`). Don't change that — the
user wants to know they're testing the latest code every time.

Requires `brew install raylib` on the host.

## Debug log (pathing / AI bug reports)

The game accepts a `--debug` command-line flag. When set, it opens
`/tmp/ironfist-debug.log` for writing and dumps a 5 Hz snapshot of the
player + every active enemy on each tick. **The log is NOT written by
default — debug mode is off unless `--debug` is passed.**

- `run.sh` passes `--args --debug` so `./run.sh` launches with logging on.
- `./debug.sh` runs `tail -F` on the log — open it in a second Terminal
  tab while playing.

Each tick writes one block like:

```
[t=  23.40s] wave=2 bossMode=0 bossFight=0  player: pos=(55.12, 1.35, 40.00) yaw=1.47rad (84deg) hp=85/100 weapon=1
  #00 CHEF  alive pos=( 50.12,  0.00,  38.20) to-player=(+0.98, +0.21) dist= 5.10 state=CHASE  hp=  45/  65 pd=(+0.71, +0.71) flashT=0.00 bleedT=0.18
  #01 BOSS  alive pos=(114.00,  0.00,   6.00) to-player=(-0.88, +0.48) dist=66.50 state=PATROL hp= 800/ 800 pd=(+0.20, +0.98) flashT=0.00 bleedT=0.00
  (alive=2)
```

**Field meanings** (important — user will paste chunks of this log when
describing bugs, and you should read them as authoritative game state):

- `t=Xs` — seconds since window init (raylib `GetTime()`)
- `wave=N` — current wave number
- `bossMode=0|1` — true when the player started in boss-test-mode (press
  **B** on the menu). In this mode there are no chef waves, just a lone
  boss that respawns on death.
- `bossFight=0|1` — true only during the between-waves boss interlude of a
  normal run. **Always 0 in boss-test-mode** even though a boss is live —
  check `bossMode` for that case. At least one of the two is true whenever
  a boss is alive.
- `player pos=(x, y, z)` — world coords. y=0 is the floor, y=1.35 means
  standing on a platform, etc.
- `yaw` — in radians; `(Ndeg)` is the degree form. 0 = +z direction.
- `weapon` — 0=shotgun, 1=MG, 2=launcher
- Per-enemy row:
  - `#NN` — slot index in `g_e[]`
  - `CHEF / HEAVY / FAST / BOSS` — `e->type` 0/1/2/3
  - `alive / DYING` — `dying` flag (corpse vs live AI)
  - `pos=(x, y, z)` — enemy world coords; `y` being >0 means they're on a
    platform / step
  - `to-player=(dx, dz)` — unit vector from enemy toward player, `dist` in
    metres. Useful for spotting direction vs actual movement bugs.
  - `state=PATROL/CHASE/ATTACK` — AI state
  - `hp=A/B` — current / max HP
  - `pd=(x, z)` — patrol direction (used in PATROL state). In CHASE state
    `pd` is stale; rely on `to-player` instead.
  - `flashT` — seconds remaining on pain-frame flash (set to 0.12 on every
    damage tick)
  - `bleedT` — countdown to next blood drip (only counts down while
    `hp < maxHp`)

When the user reports an AI bug and pastes a log excerpt, use the coords
and state to localise the bug precisely — e.g. a chef stuck at the same
pos across several ticks while `state=CHASE` and `to-player` points into
a wall tells you exactly where pathing is failing.

## Asset layout — IMPORTANT

All assets live under typed subdirectories at the project root. The Makefile
copies them into `IronFist3D.app/Contents/Resources/` at build time. The game
loads them from that bundle path via `GetApplicationDirectory()` + a relative
path.

```
sprites/
  browning/       — shotgun viewmodel frames (BA5*.png)
  luger/          — luger pistol frames (unused since pistol was removed, kept archived)
  mp40/           — MP40/rifle gun body + muzzle overlay (RIF*.png)
  panzerschreck/  — rocket launcher pose + explosion frames (PAN*/EXP*)
  monsters/       — chef billboards: AFAB* (walk/pain/death), AODE* (2nd type, unused)
  pickups/        — health (CHIKA, EASTA), ammo (SBOXA shells, MCLPA bullets, MCLPB MG rounds, MNRBB rockets)
  crosshairs/     — per-weapon crosshair overrides (SHOT.png for shotgun)
sounds/
  *.mp3           — all music + SFX (hell-march background, headshot/fatality/
                    holy-shit/first-blood announcers, shotgun, chef-die, rocket-hit)
```

### When the user adds new assets (IMPORTANT)

If the user drops a loose file at the project root (e.g. a `*.mp3` or `*.png`),
**move it into the correct asset subdirectory** — don't leave it loose. Use the
existing subfolders; create a new one only if no existing folder fits the
asset type. After moving, update any Makefile rule if a new top-level asset
folder is introduced (see `$(SPRITES)`, `$(SOUNDS)` rules — they `cp -r` the
whole folder into Resources).

The `WolfenDoom-master/` and `game-sounds-main/` archives at the project root
are source dumps — gitignored. Pull assets from them into `sprites/` or
`sounds/` when the user references a path like `./game-sounds-main/...` or
`WolfenDoom-master/sprites/...`.

## Code conventions

- **Single file** (`game.c`) — keep it that way unless the user explicitly
  asks to split it. The file is large but navigable via section banner
  comments like `// ── PLATFORMS ───────`.
- **raylib 5.x** API. Use `rlgl` directly when you need to flush batches,
  toggle depth writes, or disable backface culling. Remember that state
  changes applied between `DrawBillboard`/`DrawModel` calls do **not** take
  effect until the batch is flushed with `rlDrawRenderBatchActive()` — several
  bugs originated from this.
- **Sprite weapons** live in a unified `WepSprite g_wep[3]` array (pistol was
  removed; 0=shotgun, 1=MG, 2=rocket). Adding a new sprite weapon is a one-line
  entry in the `packs[]` table inside `main()`. Per-weapon `xShift`/`yShift`
  (fractions of screen size) tune viewmodel placement.
- **Real audio overrides** procedural audio by loading after the generator and
  `UnloadSound(old); g_s... = real;`. Same pattern for shotgun, rocket-hit,
  chef-die. If the MP3 fails to load, the procedural version stays.
- **Ignore the IDE `raylib.h not found` diagnostics** — they're false positives
  from the language server not knowing the include path; the actual compiler
  has it.

## Weapons (current)

| Key | Name          | Fire rate | Damage | Ammo       | Notes                                   |
|-----|---------------|-----------|--------|------------|-----------------------------------------|
| 1   | Shotgun       | 0.59s     | 15×8   | shells(32) | Browning sprite, 8-pellet spread        |
| 2   | Machine gun   | 0.09s     | 18     | mgAmmo(120)| MP40 sprite with RIFGA flash overlay    |
| 3   | Launcher      | 0.96s     | 200    | rockets(8) | Panzerschreck sprite (static), rocket splash 200 over 5m |

## Enemies

Chef (AFAB* billboards). 4-frame walk cycle driven by `legT`, pain frame on
flash, G→H→I→J death freeze. Corpses stay in scene, drawn with depth mask off
so they don't clip live enemies. `KillEnemy` sets `dying=true`; `Alive()`/AI/
bullets/minimap all skip dying enemies.

3 enemy type values (GRUNT/HEAVY/SPECTER) drive stats (HP, speed, size) but
visually all share the chef sprite set for now.

## Level

2D grid `MAP[ROWS][COLS]` of walls, plus a `Platform[]` list for Q3-style
stacked-AABB geometry (stairs + raised decks + jump platforms). `PlatBlocks`
and `PlatGroundAt` handle collision + step-up (STEP_H = 0.55m). Ceiling height
is `WALL_H = 5.1f`.

## HUD

- Rotating radar minimap (forward=screen-up), scaled to fit the full map when
  player is near centre.
- Kill counter + wave + score + ammo + hp + weapon name.
- Announcer SFX: UT first-blood / headshot / multi-kill (holy shit), MK
  fatality, played via `PlaySound` with `SetSoundVolume(..., 1.5f)` for
  loudness boost.

## Persistent state

Music volume is saved to `~/.ironfist3d.cfg` on every change (and reloaded at
launch). The game is otherwise stateless between runs.

## UI text caveat

raylib's default built-in font only has basic ASCII glyphs. **Don't use
em-dashes (`—`), curly quotes, or other non-ASCII characters** in `DrawText`
strings — they'll render as `?`. Use `-` and `'` instead. (Comments in code
can use whatever you want.)
