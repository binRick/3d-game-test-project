# ⚔️ IRON FIST 3D

### ▶️ [Play in your browser — ironfist.ximg.app/play.html](https://ironfist.ximg.app/play.html)

No install, no download. WebGL 2 required.

![gameplay](docs/screenshot-1.png)

![](docs/screenshot-2.png)

**A Duke-Nukem-style FPS built from scratch in C with raylib — native on macOS and Windows, WebAssembly in the browser.**

A `src/game.c` core (plus 3 helper modules and an Objective-C bridge for the
macOS leaderboard POST). No engine. No scripting layer. Real OpenGL, real
audio, real 3D collision. You fight an escalating horde of cleaver-swinging
**chefs** plus 9 other enemy types — Doom-style soldier, cacodemon and cyber
demon, Beautiful-Doom revenant / lost soul / pain elemental, Wolfenstein SS
guard, ranged mutant, heavy mech, and a cyber-demon **wave 2 boss** — through
an industrial arena with textured walls, Q3-style stairs, a tesla cannon
that chains lightning, friendly-fire splash, and an honest-to-god C&C Red
Alert soundtrack.

```
    ╔════════════════════════════════════════════════════╗
    ║  1 - SHOTGUN  2 - MG  3 - LAUNCHER  4 - TESLA      ║
    ║  WASD move  ·  MOUSE aim  ·  LMB fire              ║
    ║  SPACE jump · SHIFT sprint · P pause · -/+ music   ║
    ║  ESC: in-game -> menu   /   menu -> quit           ║
    ║  A on menu -> ARENA picker (test any enemy solo)   ║
    ╚════════════════════════════════════════════════════╝
```

Web build also posts your run to a public leaderboard at
[ironfist.ximg.app/scores.html](https://ironfist.ximg.app/scores.html) when
you die — type 3-character initials, ENTER to submit.

---

## 🎮 Play

**macOS** — builds an `.app` bundle:

```bash
./run.sh               # auto-installs raylib via brew, rebuilds, opens the app
```

Or grab the prebuilt [release](https://github.com/binRick/Iron-Fist/releases).

**Windows** — a single self-contained `IronFist3D.exe`. All sprites and
sounds are baked into the binary as a Win32 RCDATA resource and extracted to
`%TEMP%/IronFist3D/` on first launch, so you only ship one file. Cross-build
from macOS:

```bash
brew install mingw-w64
make windows           # produces dist-win/IronFist3D.exe
```

**Browser** — Emscripten / WebAssembly build. Hosted at
[ironfist.ximg.app/play.html](https://ironfist.ximg.app/play.html), or build
locally: the same `src/game.c` compiles to `dist-web/index.html` (+ `.js` /
`.wasm` / `.data`). WebGL 2 required.

```bash
# one-time: install emsdk, activate it, and source the env
git clone https://github.com/emscripten-core/emsdk vendor/emsdk
./vendor/emsdk/emsdk install latest
./vendor/emsdk/emsdk activate latest
source ./vendor/emsdk/emsdk_env.sh

make web-raylib        # clone + build raylib source for PLATFORM_WEB (first time)
make web-serve         # build + http://localhost:8000/
```

Differences from the native builds:

- Mouse look + audio require a user gesture, handled by a click-to-start gate.
- Music volume does not persist across page reloads (no filesystem).
- No `--debug` flag / debug log.

---

## 🏛️ Architecture

```mermaid
flowchart TB
    subgraph Input["🖱 Input"]
        MOUSE["Mouse delta → yaw/pitch"]
        KB["WASD · SHIFT · SPACE · 1/2/3 · -/+"]
    end

    subgraph State["🧠 Game State"]
        PLAYER["Player: pos, hp, ammo<br/>weapon, kills, score"]
        ENEMIES["Enemy[96]: pos, hp, state<br/>flashT, deathT, bleedT"]
        BULLETS["Bullet[256]: pos, vel<br/>life, damage, rocket?"]
        PARTS["Part[768]: vel, life<br/>stuck (decals)"]
        PLATS["Platform[64]: AABB + top"]
    end

    subgraph Sim["⚙️ Tick"]
        UP["UpdPlayer"]
        UE["UpdEnemies<br/>(AI + separation)"]
        UB["UpdBullets"]
        UPa["UpdParts<br/>(stick on land)"]
        UPk["UpdPickups"]
    end

    subgraph Render["🎨 Render (BeginMode3D)"]
        WORLD["Walls · Floor · Ceiling<br/>(lit shader, 6 point lights + fog)"]
        PLATDRAW["Platforms"]
        ENSPR["Chef billboards<br/>(back-to-front, depth mask off)"]
        BULS["Bullets · Particles · Pickups"]
        LIGHTS["God-ray cones (additive, last)"]
        WEP["Weapon viewmodel"]
    end

    subgraph HUD["📊 HUD (2D)"]
        CROSS["Crosshair (per-weapon tex)"]
        BARS["HP bar · Ammo · Kills · Wave"]
        MINIMAP["Rotating radar"]
        MSGS["Kill / Wave / Volume messages"]
    end

    subgraph Audio["🔊 Audio"]
        MUSIC["Streamed Hell March<br/>(~/.ironfist3d.cfg volume)"]
        SFX["PlaySound pool:<br/>weapon · announcer · chef vocals"]
    end

    Input --> Sim
    Sim --> State
    State --> Render
    State --> HUD
    Sim --> SFX
    Render --> Screen[["🖥 Screen"]]
    HUD --> Screen
    MUSIC --> Speakers[["🔊 Output"]]
    SFX --> Speakers
```

---

## 🔫 Weapons

| Key | Weapon                    | Fire rate | Damage          | Ammo       | Notes |
|-----|---------------------------|-----------|-----------------|------------|---|
| **1** | **Shotgun** (Browning)  | 0.59s     | 15 × 8 pellets  | 32 shells  | Distance-scaled: **2.5× at 0m**, 1.0× at 6m, 0.25× floor at 15m+ |
| **2** | **Machine Gun** (MP40)  | 0.09s     | 18              | 120 rounds | Full-auto · RIFGA muzzle flash overlay |
| **3** | **Launcher** (Panzerschreck) | 0.96s | 200 direct + 200 splash | 8 rockets | Splash 5m radius · you take 30 self-damage if close |
| **4** | **Tesla Cannon**        | 0.45s     | 140 + chain     | 30 cells   | Wide auto-aim cone, 6m range, chains across up to 5 nearby enemies with 85/70/58/48/40% falloff. Distance to target measured to enemy *surface* so wide bosses (1.5m+) can be tagged from a sensible distance. Picked up via the TESLA crate near spawn. |

### Shotgun damage falloff

```mermaid
xychart-beta
    title "Shotgun multiplier vs range"
    x-axis "Range (m)" [0, 3, 6, 9, 12, 15, 20]
    y-axis "Multiplier" 0 --> 2.5
    line [2.5, 1.75, 1.0, 0.75, 0.5, 0.25, 0.25]
```

### Per-weapon customisation (`g_wep[]`)

Every weapon is a row in a table — frame list, scale, and per-weapon
horizontal/vertical offset (fraction of screen). Adding a new sprite weapon
is one line.

| Field     | Example                                             |
|-----------|-----------------------------------------------------|
| `folder`  | `browning`                                          |
| `frames`  | `BA5GA0, BA5GE0, BA5GB0, BA5GC0, BA5GD0`            |
| `scale`   | `4.4` (screen-pixel multiplier)                     |
| `xShift`  | `-0.01` (1% left)                                   |
| `yShift`  | `-0.085` (8.5% up)                                  |
| `flash`   | Optional full-canvas overlay during fire window      |

---

## 🧟 Enemy roster (13 types)

Every enemy uses the same shared AI state machine (`PATROL → CHASE → ATTACK
→ DYING`) but stats, attack style and sprite set differ. Wave scaling:
+12% HP, +0.18 speed per wave (per-enemy values are wave-1 baselines).

```mermaid
stateDiagram-v2
    [*] --> PATROL
    PATROL --> CHASE: player in alertR
    CHASE --> ATTACK: dist < atkR (and same y-level for melee)
    ATTACK --> CHASE: dist > atkR*1.5  OR  LOS blocked  (ranged)
    CHASE --> DYING: hp <= 0
    ATTACK --> DYING: hp <= 0
    DYING --> [*]: corpse persists (depth mask off; flying enemies fall)
```

| # | Enemy               | HP   | Speed | Dmg | Rate | Attack             | Notes |
|---|---------------------|------|-------|-----|------|--------------------|---|
| 0 | **Chef**            | 65   | 6.0   | 10  | 1.5s | Melee cleaver      | AFAB sprites · wave 1 staple |
| 1 | **Heavy chef**      | 145  | 3.6   | 24  | 2.1s | Melee cleaver      | TORM sprites · tankier, slower |
| 2 | **Fast chef**       | 42   | 8.8   | 8   | 1.0s | Melee cleaver      | SCH2 sprites · glass cannon |
| 3 | **Boss chef**       | 800  | 7.5   | 40  | 1.6s | Melee, big hitbox  | BTCN sprites · between-wave interlude |
| 4 | **SS guard**        | 80   | 5.0   | 14  | 1.8s | Hitscan tracer     | PARA sprites · 8-rotational |
| 5 | **Mutant**          | 90   | 4.2   | 14  | 1.4s | Purple energy ball | MTNT sprites · ranged projectile |
| 6 | **Mech**            | 260  | 2.6   | 28  | 2.6s | Heavy rocket       | MAVY sprites · slow, splash dmg |
| 7 | **Soldier**         | 120  | 5.0   | 14  | 1.4s | Hitscan tracer     | DOOM-style-Game shotgunner |
| 8 | **Cacodemon**       | 220  | 3.5   | 18  | 2.0s | Fireball, *flying* | DOOM-style-Game · joins wave 2+ |
| 9 | **Cyber demon**     | 1500 | 3.8   | 34  | 2.6s | Twin rockets       | DOOM-style-Game · **wave 2 boss** (replaces chef boss) |
| 10 | **Revenant**       | 250  | 5.5   | 16  | 1.6s | Melee placeholder  | Beautiful-Doom · arena-only preview |
| 11 | **Lost soul**      | 60   | 9.0   | 8   | 0.9s | Charge-melee, *flying* | Beautiful-Doom · arena-only preview |
| 12 | **Pain elemental** | 420  | 3.0   | 18  | 2.0s | Melee placeholder, *floats* | Beautiful-Doom · arena-only preview |

Types 10/11/12 are **preview-only** — sprites loaded, arena spawn works,
but they currently use chef-style melee placeholder AI; full Doom-style
behaviours (homing missiles, kamikaze charge, lost-soul spawning) are
deferred until they're greenlit for waves.

### Flying enemy handling

Cacodemon (8), lost soul (11) and pain elemental (12) spawn at a constant
`e->pos.y` (caco 1.5m, lost soul 2.2m, pain elem 2.0m), bypass the
platform-y snap, and on death **fall** to the floor with accelerating
gravity. Once landed, the corpse sprite collapses to 60% height so the
top-down gore texture reads as a flat pile rather than a vertical
billboard with blood floating mid-air.

### Hit volumes

Every enemy has per-type head + body sphere sizes for hitscan (shotgun /
MG) and a per-type cylinder for projectiles (rockets, mutant ball, cyber
rocket, caco fireball). Rockets-pass-straight-through-cyber bugs are
caused by mismatched values here — see `UpdBullets` and `Shoot()` in
`src/game.c`.

### Separation behaviour

```mermaid
flowchart LR
    E1((Chef 1)) -->|seek + separation| P{{Player}}
    E2((Chef 2)) -->|seek + separation| P
    E3((Chef 3)) -->|seek + separation| P

    E1 <-.push-away.-> E2
    E1 <-.push-away.-> E3
    E2 <-.push-away.-> E3
```

Within a 1.5m neighbour radius, each chasing chef sums a push-away vector
from every other live chef, blends it 1.4× against the seek vector, and
moves along the normalised result — fanning out instead of conga-lining.

### Navigation around terrain

Chasing chefs probe 1.5m ahead along their seek direction each tick:

- If the probe hits a **walkable step** (height ≤ `STEP_H`), nothing special —
  they just walk onto it. `PlatGroundAtR` lifts their y as soon as their
  body overlaps the step, so they don't clip into the mesh at y=0.
- If the probe hits a **too-tall platform**, they **skirt** it tangentially
  — perpendicular to the enemy→platform-centre vector, blended 85/15 with the
  pull toward the player. Each chef picks a skirt side from its hash so a
  group fans around both sides instead of piling against one face.
- The probe is staircase-aware: if there's a walkable step between the chef
  and the unreachable platform top, it's *not* flagged as a blocker — the
  chef will take the stairs up instead of detouring.
- Wall collision is **circle-vs-grid**, so chefs don't snag on outside
  corners from a diagonal graze (the old 3-point shoulder test false-blocked
  on corner grazes and forced chefs to slide several metres before turning).
- Dropping off a platform edge no longer strands a chef in the collision
  safety margin around the base — `PlatPenetration` lets them walk away
  from the edge but still stops them from pushing back into the margin.

---

## 🧱 Level

- **2D grid walls** (`MAP[20][30]`) rendered as a single lit mesh
- **Q3-style platforms** (`Platform[]`): AABB boxes with a `top` height,
  stacked on the flat floor
- **Step-up collision**: any platform within `STEP_H = 0.55m` auto-climbs,
  taller ones block as walls
- **Ceiling lights**: 6 colored point lights + visible fixtures + additive
  god-ray cones drawn last
- **Atmospheric fog** handled in the fragment shader

```
z=0 ──┐                                                        ┌── z=80
      │  ┌──┐                                                  │
      │  │  │                        ┌───────── platform ──┐   │
      │  └──┘     stairs             │                     │   │
      │         0.45→0.90→1.35       │   deck at y=1.35m   │   │
      │  ┌─────────────────────────► │                     │◄──┤
      │  │                           └─────────────────────┘   │
x=0 ──┴─────────────────────────────────────────────────┴── x=120
```

---

## 🎨 Lighting & rendering

- Custom GLSL shader (embedded as a C string) does per-pixel lighting for
  up to 8 coloured point lights + exponential fog
- Walls, floor, ceiling, and platforms all run through the same shader and
  share the procedural brick texture
- **Enemy billboards** sorted **back-to-front** each frame and drawn with
  `rlDisableDepthMask()` so transparent sprite pixels don't occlude each
  other (major fix — live chefs behind corpses are now visible)
- Blood particles are tiny `DrawCubeV` specks with slight alpha and
  randomised spawn jitter; once they land slowly they become **flat floor
  decals** that fade over ~12 seconds
- Wounded chefs **leak blood** as they walk — faster drip rate the more hurt
  they are

---

## 🔊 Sound logic

```mermaid
flowchart TD
    subgraph Music
        A[Hell March loop] -->|UpdateMusicStream| B[Mixer]
        V["- / + keys<br/>→ ~/.ironfist3d.cfg"] -.-> A
    end

    subgraph Firing["Firing stingers"]
        F1[Shotgun trigger] --> shotgun-kill.mp3
        F2[MG trigger] --> mg-sound.mp3
        F3[Launcher trigger] --> launcher-shot.mp3
    end

    subgraph Impacts
        I1[Rocket detonation] --> rocket-hit.mp3
        I1 -. volume ∝ 1/distance .-> rocket-hit.mp3
    end

    subgraph Enemy
        E1[Chef damaged] --> PainFrame[AFABF sprite]
        E2[Chef killed] --> DieRand{Random pick}
        DieRand --> chef-die.mp3
        DieRand --> chef-die-1.mp3
        DieRand --> chef-die-2.mp3
        DieRand --> chef-die-3.mp3
    end

    subgraph Announcer["UT / MK announcer stingers"]
        K1[kills == 1] --> first-blood.mp3
        K2[headshot] --> headshot.mp3
        K2 --> SKIP[skip fatality]
        K3[regular kill] --> fatality.mp3
        K4[≥2 kills in one shot] --> holy-shit.mp3
        K5[enemy enters FOV after none] --> distant-enemy.mp3
        K5 -.IsSoundPlaying guard.-> distant-enemy.mp3
        K6[wave clear] --> next-wave.mp3
    end

    F1 --> B
    F2 --> B
    F3 --> B
    I1 --> B
    PainFrame -.-> B
    DieRand --> B
    K1 --> B
    K2 --> B
    K3 --> B
    K4 --> B
    K5 --> B
    K6 --> B
    B --> Out[🔊 Speakers]
```

**Key mechanics**
- **Distance-scaled explosions**: rocket-hit volume 2.5× point-blank, 0.25×
  floor past 32m (`vol = 2.5 − d·0.07`)
- **Headshot suppresses fatality**: `g_lastHitHead` flag set around
  `DmgEnemy`, read in `KillEnemy` so you don't double up announcers
- **Multi-kill detection**: `g_killsThisShot` reset per-trigger; after the
  pellet loop / splash loop, if ≥2, the Unreal Tournament multi-kill plays
  at 8× volume
- **Anti-spam**: `distant-enemy.mp3` checks `IsSoundPlaying` before
  retriggering so stacked enemy alerts never overlap
- **Persistent volume**: `LoadMusicVol()` reads `~/.ironfist3d.cfg` at
  startup; every `-` / `+` press writes it back

---

## 🎯 Mechanics reference

| Mechanic               | Detail                                                                     |
|------------------------|----------------------------------------------------------------------------|
| Headshot               | Ray-tests the head sphere separately, 2.5× damage, plays `headshot.mp3`     |
| Multi-kill             | ≥2 kills from one trigger pull (or explosion) → `holy-shit.mp3` at 8×      |
| First blood            | Triggers on `kills == 1` per run                                           |
| Wave advance           | Fires when `Alive() == 0` → wave++, next-wave stinger, spawn next batch     |
| Shotgun damage falloff | 2.5× at 0m → 1.0× at 6m → 0.25× floor at 15m+                               |
| Chef separation        | Boids-style, 1.5m neighbour radius, weight 1.4 vs seek                     |
| Step-up collision      | Platforms ≤ 0.55m auto-climb, taller blocks like walls                     |
| Enemy step-up          | Body-radius snap: chefs climb stairs as soon as their footprint overlaps a step |
| Enemy path probe       | 1.5m look-ahead; skirt too-tall plats, walk through walkable stairs         |
| Enemy wall collision   | Circle-vs-grid, so chefs don't snag on outside corners                     |
| Enemy depenetration    | Player pushed out of overlapping live chefs each frame (radius 0.45m)      |
| Floor decals           | Slow-landing blood particles stick at y=0.005 for 10–16s then fade          |
| Bleeding trail         | Any chef with `hp < maxHp` drips blood every 0.08–0.33s (scales with HP)   |
| Fog                    | Exponential in fragment shader, fades towards dark purple                   |
| Cursor                 | `HideCursor + DisableCursor + SetMousePosition` every HUD frame            |

---

## 🎮 Other things you'll find

### Arena picker (A on main menu)

13 enemy slots. Arrow keys cycle through cycling sprite + attack-frame
previews; ENTER spawns 8 of that type (or 1 for boss/cyber demon) into the
arena map. Handy for testing AI / hit volumes / animations. Press ESC to
return to the menu.

### Pause (P during gameplay)

Stops every Upd*, mutes music + master volume so in-flight one-shots
silence too, releases pointer lock so the cursor's free, and on resume
re-engages pointer lock + drains accumulated mouse delta (otherwise the
camera spins on unpause). Two-line overlay separates the keys: green
"PRESS P - RESUME GAME" / red "PRESS ESC - EXIT TO MAIN MENU".

### High-score leaderboard (web build)

On death you get a 3-character initials selector. Keys: ←/→ pick slot,
↑/↓ cycle A-Z 0-9, type directly to set + auto-advance, BACKSPACE step
back, ENTER submits, ESC skips. The score POSTs to
`https://ironfist.ximg.app/api/scores` along with kills, wave, weapon
last equipped, time played, pickups, shots, and damage dealt — viewable
at [/scores.html](https://ironfist.ximg.app/scores.html). Web uses
`EM_JS` fetch, native macOS uses `NSURLSession` via `src/score_post.m`,
Windows currently no-ops. Server-side validation rejects implausible
runs.

### Sprite browser (S on main menu, macOS dev only)

Press **S** on the main menu (or F8 anywhere) to flip through every PNG
in 23 curated source folders (Beautiful-Doom MONSTERS + DOOM-style-Game
npc). Shows filename + sprite + folder counters. Used to figure out which
4-letter prefix in a Beautiful-Doom enemy folder maps to which animation
role — REVI is "pain", REVP is "run", RSKE is "walk", REVN is "death", etc.
Keys: ←/→ file, [ ] folder, - / + zoom, ESC exit. macOS-only because it
reads absolute paths to `third_party/`; gated behind `!PLATFORM_WEB &&
!_WIN32`.

### ESC navigation

Consistent across every screen:
- **Main menu** ESC → quit
- **Death screen** ESC → quit (post-initials)
- **In-game / paused / picker / sprite browser** ESC → main menu

Implemented by disabling raylib's default exit-on-ESC (`SetExitKey(KEY_NULL)`)
and routing every ESC keypress through per-state handlers in `StepFrame`.

---

## 📁 Repo layout

```
src/
  game.c                  ← main code (~5000 lines)
  hud.c / .h              ← Doom mugshot HUD + status bar
  effects.c / .h          ← particle pool: blood, sparks, decals
  level.c / .h            ← MAP grid + Platform[] + collision predicates
  common.h                ← shared types (Player / Enemy / Pickup / Part / Platform)
  score_post.m            ← macOS NSURLSession bridge for high-score POST
Makefile                  ← macOS .app + Windows single-file .exe + emcc web targets
run.sh                    ← brew-installs deps → make -B → exec the binary in foreground
gen_icon.py               ← procedural iron-fist .icns
pack_bundle.py            ← flattens sprites/ + sounds/ into dist-win/bundle.dat
                            (embedded into the Windows exe via windres RCDATA)
web/shell.html            ← emscripten click-to-start gate for autoplay + pointer lock
CLAUDE.md                 ← conventions / asset rules / debug-log format for future dev

sprites/
  browning/ mp40/ panzerschreck/  weapon viewmodels (luger archived)
  monsters/                       AFAB/TORM/SCH2 chefs, BTCN boss, MTNT mutant,
                                  MAVY mech, PARA SS guard
  monsters/preview/               soldier/caco/cyber + revenant/lostsoul/painelem
                                  (walk_N / atk_N / pain_N / death_N each)
  textures/                       sky.png + wall1..wall5.png (connected-component
                                  flood-fill picks one texture per wall block)
  blood/                          YBL7A..S animated splat decal
  pickups/                        health, ammo, power-ups, tesla pickup
  crosshairs/                     per-weapon overrides

sounds/
  hell-march.mp3                  looping BG music
  shotgun-kill / mg-sound / launcher-shot / rocket-hit / chef-die*  weapon + chef vocals
  first-blood / headshot / fatality / holy-shit / next-wave / distant-enemy  announcers
  tesla-* / mech-* / mut-* / chef-hit                              new-weapon + new-enemy

third_party/                      git submodules (gitignored except the two below)
  Beautiful-Doom/                 GZDoom mod — MONSTERS sprite source for revenant/
                                  lostsoul/painelem, plus the m_*.zc actor scripts
                                  that decode which prefix is which animation
  DOOM-style-Game/                Doom-style-Game project — MONSTERS sprite source
                                  for soldier/caco/cyber

docs/screenshot-1.png             gameplay screenshots
docs/screenshot-2.png
```

---

## 🛠️ Build gotchas

- raylib 5.x required. `./run.sh` auto-installs `raylib` + `pkg-config` via
  Homebrew if they're missing
- The IDE will yell about missing `raylib.h` includes and `Vector3` types —
  those are false positives from the language server. The compiler has the
  include path. Ignore them
- Default raylib font is ASCII-only; don't use em-dashes (—) in `DrawText` —
  they'll render as `?`
- macOS icon cache can be stubborn. If the Dock shows the old icon, run
  `killall Dock` after a build
- Windows: the game includes `<windows.h>` would clobber raylib's `DrawText`,
  `CloseWindow`, `Rectangle` macros — `src/game.c` forward-declares only the
  handful of Win32 APIs needed for the RCDATA resource extraction instead

---

Built one turn at a time. Every feature request, every bug fix, every
"wait make the blood smaller" request, delivered.

🔥 **Cook some chefs.**
