# Iron Fist 3D — project notes

## ⚠️ ASCII-ONLY IN `DrawText` STRINGS (READ FIRST)

raylib's default built-in font only ships ASCII glyphs. Any non-ASCII
character inside a `DrawText` argument — em-dashes (`—`), en-dashes (`–`),
curly quotes (`'`, `"`, `'`, `"`), ellipses (`…`), bullets (`•`), arrows
(`→`), accented letters, etc. — renders on screen as a literal `?`.

**Rules**:

- In any string passed to `DrawText`, `MeasureText`, `Msg()`, `g_msg`,
  `g_hypeMsg`, or other on-screen text, use ONLY ASCII (`-`, `'`, `"`, `...`).
- This applies to picker blurbs, hype banners, kill announcements, debug
  overlays, menu hints — every user-visible string.
- Comments in code, header comments, commit messages, log file output to
  `/tmp/...` and CLAUDE.md itself can use any UTF-8.
- When you find yourself reaching for an em-dash mid-message, type ` - `
  (space-hyphen-space) instead.

This bug has bitten the project repeatedly. Before submitting any change
that touches user-facing text, mentally re-scan every literal string for
non-ASCII characters.



FPS built in C with raylib from a single source file (`src/game.c`), targeting
three platforms from the same codebase:

- **macOS** — primary dev target; ships as an `.app` bundle.
- **Windows** — mingw-w64 cross-compile; single self-contained `.exe` with
  assets bundled as an RCDATA resource.
- **Web** — Emscripten / WebAssembly build, runs in any WebGL-2 browser.

Three platform dispatches live in `src/game.c`:

- Asset path: `#if defined(__APPLE__)` picks `../Resources/` (app bundle),
  Windows extracts to `%TEMP%/IronFist3D/`, else flat `sprites/`/`sounds/`.
- Shaders: `#if defined(PLATFORM_WEB)` emits GLSL ES 300 (WebGL 2) instead
  of GLSL 330 core. Everything else in the shader source is identical.
- Main loop: `#ifdef __EMSCRIPTEN__` calls `emscripten_set_main_loop(StepFrame,
  0, 1)` instead of the native `while (!WindowShouldClose())`. The loop body
  is factored into `StepFrame()` so both paths share it exactly.

## Build & run

```
./run.sh           # kills any running instance, `make -B`, `open IronFist3D.app`
make               # just build (macOS)
make run           # build + open (macOS)
make windows       # cross-compile dist-win/IronFist3D.exe (mingw-w64)
make web-raylib    # clone + build raylib source for PLATFORM_WEB (first time only)
make web           # emcc → dist-web/index.html (+ .js/.wasm/.data)
make web-serve     # build + `python3 -m http.server` on localhost:8000
make clean         # wipe .app / dist-win / dist-web
```

`run.sh` ALWAYS does a full recompile (`make -B`). Don't change that — the
user wants to know they're testing the latest code every time.

Native (macOS) requires `brew install raylib`. Windows target requires
`brew install mingw-w64` + prebuilt raylib in `vendor/{include,lib}/`.
Web target requires emsdk activated (`source vendor/emsdk/emsdk_env.sh`)
and raylib source built via `make web-raylib`.

## Web build specifics

- **No persistence.** `SaveMusicVol` / `LoadMusicVol` short-circuit under
  `__EMSCRIPTEN__` — MEMFS doesn't survive page reloads. Upgrade path is
  localStorage via `EM_JS`; deliberately unshipped for v1.
- **No `--debug` flag.** The debug log is opt-in via argv which doesn't apply
  on web. `g_dbgLog` stays NULL, `DebugLogTick` no-ops.
- **Click-to-start gate** in `web/shell.html` satisfies the Web Audio
  autoplay + Pointer Lock user-gesture requirement before the first frame
  runs.
- **`g_cam` is file-scope** so it outlives `main()` — on web,
  `emscripten_set_main_loop` continues calling `StepFrame()` after main
  returns via the JS runtime, and a main-local camera would be dangling.
  The native loop also uses `g_cam` for consistency.
- **ASYNCIFY is on** so raylib's internal blocking waits (music-stream
  init, etc.) work in the browser. Trade-off: ~30% slower + bigger .wasm;
  if profiling says it hurts, audit what's actually blocking and pull
  those calls out of the hot path.

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

- **Single file** (`src/game.c`) — keep it that way unless the user explicitly
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

### Billboard draw order (IMPORTANT — see-through bug)

Enemy chef billboards are drawn with **depth test on but depth-mask OFF** so
the transparent edges of one chef don't clip the chef behind. Side effect:
chefs do **not** write to the depth buffer, so anything drawn *after* them
has no enemy z-value to occlude against, and shows *through* the enemy.

**Rule for any new world-space billboard (pickups, barrels, props, decor,
etc.): draw it BEFORE `DrawEnemies`** in the per-frame 3D pass. The world
sprite then writes depth normally, and the enemy's depth-test (still on)
correctly hides the part of the sprite the enemy is in front of.

If you ship a new sprite type and a chef walking between camera and the
sprite *doesn't* occlude it — this is the bug. Move the new draw call ahead
of `DrawEnemies(g_cam)`.

Pickups also need a platform-top lookup in `UpdPicks` so they sit on stairs
instead of clipping into the steps — same pattern if you add a new
world-space sprite that lives at floor height.

## Weapons (current)

| Key | Name          | Fire rate | Damage  | Ammo        | Notes                                   |
|-----|---------------|-----------|---------|-------------|-----------------------------------------|
| 1   | Shotgun       | 0.59s     | 15×8    | shells(32)  | Browning sprite, 8-pellet spread        |
| 2   | Machine gun   | 0.09s     | 18      | mgAmmo(120) | MP40 sprite with RIFGA flash overlay    |
| 3   | Launcher      | 0.96s     | 200     | rockets(8)  | Panzerschreck sprite (static), rocket splash 200 over 5m |
| 4   | Tesla cannon  | 0.45s     | 140 +chain | cells(30) | Wide-cone auto-aim, 6m range, 5-hop chain (85/70/58/48/40% falloff). Distance to target measured to enemy SURFACE not centre — `bodyR` per-type subtracted before comparing to range, so wide bosses (1.5m radius) can still be tagged. Pickup spawns near player on first run. |

The four weapon slots use a shared `WepSprite g_wep[4]` array; adding a new
weapon is one row in `packs[]` inside `main()`.

## Enemies (13 types, all share state machine PATROL/CHASE/ATTACK/DYING)

Stat tables are arrays indexed by `e->type` — `ET_HP[]`, `ET_SPD[]`,
`ET_DMG[]`, `ET_RATE[]`, `ET_AR[]` (alert radius), `ET_ATK[]` (attack
radius), `ET_SC[]` (score), `ET_COL[]` (minimap colour). All arrays MUST
have 13 entries; bounds checks on `e->type < ARRAY_COUNT` guard the few
sites that access them via name lookup tables.

| #  | Type           | Sprite source            | AI / attack                 | Notes |
|----|----------------|--------------------------|-----------------------------|-------|
| 0  | Chef           | sprites/monsters/AFAB*   | Melee cleaver               | Wave 1 staple |
| 1  | Heavy chef     | sprites/monsters/TORM*   | Melee cleaver, slow         | |
| 2  | Fast chef      | sprites/monsters/SCH2*   | Melee cleaver, fast         | |
| 3  | Boss chef      | sprites/monsters/BTCN*   | Melee, big hitbox           | Between-wave interlude (except wave 2) |
| 4  | SS guard       | sprites/monsters/PARA*   | Hitscan tracer + sparks     | 8-rotational, mirror pairs |
| 5  | Mutant         | sprites/monsters/MTNT*   | Energy ball (SpawnEShot)    | 8-rotational |
| 6  | Mech           | sprites/monsters/MAVY*   | Heavy rocket (SpawnEShotRocket) | 8-rotational, splash dmg, no death sprite (explodes + removed) |
| 7  | Soldier        | sprites/monsters/preview/soldier/  | Hitscan tracer (cultist path) | DOOM-style-Game source |
| 8  | Cacodemon      | sprites/monsters/preview/caco/     | Fireball, **flying y=1.5** | DOOM-style-Game · joins wave 2+ |
| 9  | Cyber demon    | sprites/monsters/preview/cyber/    | Heavy rocket               | DOOM-style-Game · **wave 2 boss** (replaces type 3) |
| 10 | Revenant       | sprites/monsters/preview/revenant/ | Melee placeholder          | Beautiful-Doom · arena-only preview, walks via `RSKEa1..h1` time-cycle |
| 11 | Lost soul      | sprites/monsters/preview/lostsoul/ | Melee placeholder, **flying y=2.2** | Beautiful-Doom · arena-only preview |
| 12 | Pain elemental | sprites/monsters/preview/painelem/ | Melee placeholder, **floats y=2.0** | Beautiful-Doom · arena-only preview |

### Flying enemy rules (types 8, 11, 12)

- Spawn at fixed `e->pos.y` (1.5 / 2.2 / 2.0) — set in all 4 spawn sites
  (arena initial, arena respawn, wave 1 init, per-wave loop)
- Skip platform-y snap in PATROL and CHASE branches of `UpdEnemies`
- Bypass the ATTACK-y-gate (`fabsf(g_p.pos.y - e->pos.y) <= STEP_H`) so they
  can engage the player on the floor from above
- On death (`e->dying`), accelerating gravity decreases `e->pos.y` toward 0
  in the dying-corpse block of `UpdEnemies`
- Once landed (`e->pos.y <= 0.05`), the death-frame `spriteH` is multiplied
  by 0.6 in `DrawEnemies` so the top-down gore sprite collapses to a flat
  pile instead of standing up vertically with blood floating mid-air

### Hit-volume sites (3 of them — keep in sync)

1. `Shoot()` — per-pellet hitscan (shotgun, MG): per-type `headY/headR/
   bodyY/bodyR` offsets-from-feet
2. `UpdBullets()` — enemy-projectile-vs-enemy AND player-rocket-vs-enemy:
   per-type `_bodyY/_bodyR/_bodyH` offsets-from-feet (single cylinder)
3. `FireTeslaShot()` — cone target + chain hops: per-type `coneChestY` for
   the targeting reference, per-type `bodyR` for **surface-distance** test
   (subtracted from centre distance before comparing to range)

When you add a new enemy type, add entries in ALL THREE sites or rockets/
hitscan/tesla will pass through.

### Walk-frame rendering for preview enemies (types 7-12)

- `walk_N.png` files in the preview folders are EITHER 8-direction rotation
  views (single-frame idle/standing) OR animation frames (multiple poses,
  same view angle) depending on the source.
- Soldier / caco / cyber: 8 rotation views (DOOM-style-Game).
  `DrawEnemies` picks the slot from enemy-facing-vs-player angle (Doom
  octant indexing) — NOT a time cycle, otherwise the enemy appears to spin.
- Revenant (10): time-cycles 8 RSKE animation frames at front-rotation
  only. Special-cased in the render branch — always faces camera but looks
  alive walking.
- Beautiful-Doom decorate scripts (`third_party/Beautiful-Doom/Z_BDoom/
  m_*.zc`) document which prefix is which animation: revenant Spawn=REVI,
  See=RSKE, Pain=REVP, Melee=SSKE, Death1=REVN, Death=REVM, XDeath=REVX,
  missile tracer=SKEB, tracer death=FBXP. Use the in-game sprite browser
  (S on main menu) to verify visually.

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

## High-score submission (added v1.3.14)

On death, before the restart prompt, the death screen shows a 3-character
initials selector. Confirming with ENTER POSTs a JSON payload to
`https://ironfist.ximg.app/api/scores` with shape:

```json
{"initials":"AAA","score":N,"kills":N,"wave":N,"weapon":"shotgun|machinegun|launcher|tesla","time":SECONDS,"pickups":N,"shots":N,"damage":N}
```

The stat globals (`g_statShots`, `g_statDamage`, `g_statPickups`,
`g_statTime`) are file-scope in game.c, reset in `InitGame`, and
incremented at:
- `Shoot()` — one shot per fire (pellet count NOT broken down)
- `DmgEnemy()` — damage clamped to remaining HP (overkill not counted)
- `UpdPicks()` — every successful pickup grab
- `StepFrame()` GS_PLAY branch — accumulates `dt` while not paused

Platform dispatch in `SubmitScore()`:
- Web (`__EMSCRIPTEN__`): `EM_JS` block calls `fetch()` directly.
- macOS native (`__APPLE__`): `extern void IronFistPostScoreMacOS(const char*)`
  is implemented in `src/score_post.m` using `NSURLSession` (fire-and-forget
  detached task). Linked via `-framework Foundation` in the Makefile.
- Windows: no-op for now. Could add WinHTTP later.

ESC on the initials screen skips submission. ENTER submits and sets
`g_initialsSubmitted` so the post-submit overlay can show "SCORE SUBMITTED"
vs "SCORE NOT SUBMITTED".

Validation lives on the SERVER (rejects implausible scores, see
`https://ironfist.ximg.app/api/scores` validators). Don't add client-side
"protection" — it's pointless when the WASM can be modified or the API
posted to directly with curl.

## Arena picker (A on main menu)

`g_gs == GS_PICK_ENEMY`. 13 slots cycling sprite + attack-frame previews
with the picker time accumulator `g_pickerT`. Slots 0..6 are full in-game
enemies; 7..9 are full in-game enemies via DOOM-style-Game; 10..12 are
preview-only (not in waves). ENTER spawns 8 of the picked type (or 1 for
boss type 3 / cyber demon type 9). Uses `g_arenaMode` flag in the spawn /
respawn paths to skip the wave system.

## Sprite browser (debug, S on main menu / F8 anywhere)

`g_sbActive` flag swallows the entire frame in StepFrame and renders a
single sprite at variable zoom from a hardcoded list of source folders
(`SB_FOLDERS[]`). 23 folders covering Beautiful-Doom MONSTERS subdirs
plus DOOM-style-Game npc subdirs. Used to identify which 4-letter prefix
in a folder maps to which animation role.

Keys: ←/→ file, [ ] folder, - / + zoom, ESC/F8/S exit.

**macOS-only** — paths are absolute `/Users/.../third_party/...` which
don't resolve in the WASM sandbox or a Windows build. The toggle key, the
menu hint, and the F8 hook are all gated behind `!PLATFORM_WEB && !_WIN32`.

## Pause (P during gameplay)

`g_paused` flag gates Upd*. Music is paused via `PauseMusicStream`,
master volume zeroed via `SetMasterVolume(0.f)` so in-flight Sounds also
silence. Cursor handling is critical on web: `EnableCursor()` on pause to
release pointer lock, `DisableCursor()` + `SetMousePosition(centre)` +
drain `GetMouseDelta()` on unpause. Without this drain the resumed camera
spins wildly because the browser dropped pointer lock during pause and
accumulated a huge delta.

Pause overlay is two coloured lines: green "PRESS P - RESUME GAME" and
red "PRESS ESC - EXIT TO MAIN MENU". Single-line versions read as one
hint and confused users.

## ESC navigation

raylib's default ESC-closes-window is disabled at startup with
`SetExitKey(KEY_NULL)`. Per-state handlers in `StepFrame` own ESC:

- GS_MENU → `g_quit = true` (main loop exits next iteration)
- GS_DEAD post-initials → `g_quit = true` (death is a terminal state)
- GS_DEAD initials phase → skip submission, advance to post-initials
- GS_PLAY (in-game / paused) → `g_gs = GS_MENU`
- GS_PICK_ENEMY (arena picker) → `g_gs = GS_MENU`
- Sprite browser → close + `g_gs = GS_MENU`

Main loop is `while (!WindowShouldClose() && !g_quit) StepFrame();`.

## Third-party submodules (third_party/)

Two are kept (whitelisted in `.gitignore` — everything else under
`third_party/*` is gitignored):

- `third_party/Beautiful-Doom` — GZDoom mod. Sprite source for the preview
  enemies (revenant / lost soul / pain elemental). The `Z_BDoom/m_*.zc`
  ZScript files document which prefix is which animation; read those when
  porting a new enemy in.
- `third_party/DOOM-style-Game` — Doom-style-Game project. Sprite source
  for soldier / cacodemon / cyber demon. Simpler folder layout (`idle/`,
  `walk/`, `attack/`, `pain/`, `death/` with rotation-numbered PNGs).

After cloning the repo, run `git submodule update --init --recursive` to
populate them. The game itself doesn't need them at runtime — sprites are
already copied into `sprites/monsters/preview/` and bundled with the
build. Submodules are needed only for dev workflows (sprite browser, copy
new frames in).

## UI text caveat

raylib's default built-in font only has basic ASCII glyphs. **Don't use
em-dashes (`—`), curly quotes, or other non-ASCII characters** in `DrawText`
strings — they'll render as `?`. Use `-` and `'` instead. (Comments in code
can use whatever you want.)
