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
