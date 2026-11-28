# Sterrenvaarder

An open-source, clean-room engine for playing **Freelancer (2003)** on modern systems,
built from scratch and fed by the data files of your own legally owned copy.

Goals: any resolution and aspect ratio (16:9, 21:9, 32:9) with a centred HUD, a modern
renderer with upscaling, a layered mod system, and portability (Windows, Linux, macOS,
and later consoles).

> Sterrenvaarder contains **no** game code or assets. It reads them from your own install.
> Freelancer is a trademark of Microsoft; this project is not affiliated with Microsoft.

## Status: phase 4, bases and economy (in progress)

Data layer (phase 1), renderer (phase 2) and flight (phase 3) are done. Data layer results:

| Reader | Result on the retail v1.0 data |
| --- | --- |
| BINI / text INI | 1,251 / 1,251 files |
| UTF containers | 3,072 / 3,072 files |
| Models (.cmp/.3db) with LODs and joints | 1,852 models, 7,167 parts |
| Textures (TGA, DDS DXT1/3/5) | 6,849 decoded |

See [docs/formats.md](docs/formats.md) for format notes.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sterrenvaarder_tests                       # unit tests, no game data needed
./build/sterrenvaarder_corpus /path/to/Freelancer/DATA
```

## Viewer (phase 2)

Fly through any star system with the new renderer:

```sh
./build/sterrenvaarder --data /path/to/DATA --system Li01            # window, any size
./build/sterrenvaarder --data /path/to/DATA --system Li01 \
    --width 5120 --height 1440 --scale 0.6 --screenshot ny.png       # offscreen 32:9, upscaled
```

Right mouse = look, WASD/QE = move, Shift = boost, wheel = speed, F = fullscreen.
Options: `--scale` (render resolution, upscaled with Lanczos + adaptive sharpening),
`--msaa 1|2|4|8`, `--exposure`, `--normal-strength`, `--lod-scale` (1 = original draw
distances), `--mod <dir>` (extra data layers, e.g. a mod's DATA folder).

Rendering: SDL3 GPU (Vulkan today; Direct3D 12 and Metal follow via SDL_shadercross),
HDR forward shading with the system's own lights and ambient, MSAA, bloom, ACES
tonemapping, derived normal maps, anisotropic filtering. The field of view is Hor+, so
21:9 and 32:9 screens see more of the scene instead of a stretched image.

Shaders live in `shaders/`; after editing one, run `python3 scripts/compile_shaders.py`
(needs `glslangValidator`) to refresh the embedded SPIR-V.

## Flying (phase 3)

Without `--free` the viewer puts you in your own ship (default `li_elite`, or `--ship`),
using the ship's mass, drag, torque and inertia from `shiparch.ini` and the engine from
its loadout. A light fighter tops out at ~80 m/s; cruise runs at 300 m/s (or the data's
`CRUISING_SPEED`).

Field of view: the chase camera's `fovx` from `cameras.ini` (70°, defined for 4:3) by
default. The vertical angle is kept and wider screens see more to the sides (Hor+), so
4:3 looks exactly like the original. `--fov <degrees at 4:3>` changes it.

Left mouse = fire (guns from the ship's loadout, aimed through the cursor), mouse = steer (Space toggles mouse flight), W/S = throttle, Shift+W = cruise, A/D =
strafe, Q/E = roll, Z = stop engines, T = next target, F11 = fullscreen.

By default the HUD is the original one: its interface models are loaded from your own
game data (`DATA/INTERFACE/HUD`) and placed by the engine; no game files are part of this
repository. A second style, `modern`, is our own vector design that needs no game files.
Both are drawn at output resolution (crisp at any `--scale`). HUD elements sit in a canvas of at most 16:9 (`--hud-aspect`), centred on wider screens, so
on 21:9 and 32:9 the gauges stay in front of you. On 4:3, 5:4 and 16:10 the canvas is
the whole screen and the HUD scales down to fit. HUD text uses the font the game's own
`fonts/fonts.ini` names (Agency FB) when it is installed on your system, like the
original. Otherwise it uses our bundled Saira Condensed (SIL Open Font License, see
`fonts/saira-condensed/OFL.txt`). `--hud-font <file or family>` picks another font and
`--hud-font stroke` the built-in stroke font.

The layout is adjustable: canvas width (16:9, 16:10 or full width), overall size, and
anchor, offset, size and visibility per panel. See
[docs/hud.ini.example](docs/hud.ini.example). The file is read from
`sterrenvaarder/hud.ini` in the data layers (so a mod can ship one), then from
`--hud-config <file>`.

An optional post-processing look (in the spirit of ReShade) sits on top: extra bloom,
sharpening, saturation, vibrance, contrast, brightness, colour temperature, vignette, film
grain and chromatic aberration, with built-in presets (`vivid`, `cinematic`, `crisp`) and
your own `[Preset]` sections. It is off by default, so the game looks like the original
unless you turn it on. See [docs/postfx.ini.example](docs/postfx.ini.example); it is read
from `sterrenvaarder/postfx.ini` in the data layers, then `--postfx <file>`, and
`--postfx-preset <name>` picks a preset directly. The HUD is never affected.

```sh
./build/sterrenvaarder --data /path/to/DATA --width 3440 --height 1440 \
    --screenshot fly.png --sim 4 --cursor 0.25,-0.05   # scripted flight, then a screenshot
```

Combat test: `--npcs 3 [--npc-ship pi_fighter]` spawns hostile ships with their own
loadouts. Guns, munitions and shields come from the equipment INIs; shields absorb both
damage types and rebuild after collapsing. Each weapon type hits each shield type with
the multiplier in `equipment/WeaponModDB.ini` (`shield_mod`), and a loadout's `[Armor]`
scales the hull's hit points (`hit_pts_scale`). `--fire` holds the trigger in screenshot runs.

NPC pilots fly by the original pilot data (`missions/pilots_population.ini` and
`pilots_story.ini`, with `inherit`): head-on buzz runs that pass the target and turn
back, trailing from behind at the trail distance, dodging when the target aims at them,
breaking away after heavy damage and fleeing when badly hurt, and firing in bursts with
the gun block's interval, range and accuracy cone. The pilot comes from `npcships.ini`
for the NPC's ship, or `--npc-pilot <nickname>`. The behaviour is our own reading of
what those values describe. Wingmen trail from separate spots and hold fire while an
ally is in the line of fire.

Effects come from the original Alchemy files (`fx/*.ale`, listed in `freelancer.ini`):
engine flames scale with the throttle, guns show their muzzle flash, impacts play the
munition's hit effect, and bolts are drawn from `fx/beam_effects.ini`. Particles follow
the emitter's frequency, speed, spread and lifetime and the appearance's size, colour and
alpha over their life. The beam textures the game draws in code (star, ball, wide, thin)
are our own. Ships die with the effect their `explosion_arch` names in `fx/explosions.ini`,
and the damage fuses from `shiparch.ini` (`fuse = name, delay, hit points`, defined in the
`fx/fuse*.ini` files) start sparks, fire and smoke at the ship's hardpoints as the hull
drops. Animated textures (frame rects in the `.txm`) play over each particle's life.
A destroyed ship breaks into its model parts: they fly apart with the explosion's
`debris_impulse`, tumble, trail the smoke of a `debris_type` picked by weight and end in that
`[Debris]` entry's own explosion after its lifetime. Fields are not in yet. `--npc-die <seconds>` destroys the
test NPCs at that moment in a screenshot run.

## Trading and saves (phase 4)

Names come from the game's own text DLLs in the `EXE` folder next to `DATA` (or `--exe`),
read as data only. Markets, prices and cargo space come from `goods.ini`, `market_*.ini`
and the ship's `hold_size`; a base sells at the same price it buys, as in the original.

```sh
./build/sterrenvaarder --data /path/to/DATA --new-game   # docked on Planet Manhattan, as in a new game
./build/sterrenvaarder --data /path/to/DATA --load save.ini
```

F3 docks at the selected base and launches again. Docked, keys 1-3 switch between the
commodity trader, the equipment dealer and the ship dealer; Up/Down selects, Right buys,
Left sells (Shift: as many commodities as possible). Equipment mounts on the first free
hardpoint of a matching class (the ship's `hp_type` lines); a new ship trades in the old
one with everything on it and comes with its package contents. Market lines above the
player's level stay hidden, and so do lines that need more standing with the base's owner
than the player has.

Factions and how they see each other come from `InitialWorld.ini`; a new player starts
with the `house` standings of `EXE/MPNewCharacter.FL`. Contacts are green when friendly
(standing 0.6 or more) and red when hostile (-0.6 or less), and hostile bases refuse docking. F5 saves and F9 loads the quick save
(in the user's preferences folder, or `--save-file`). Saves are small text INI files.

Docked, the dealer screen sits in front of the base's own room: the base file named in
`universe.ini` lists the rooms, and each room's `.thn` scene scripts (precompiled Lua 3.2,
read as data and never run) give the set model, its lights and the camera. Rooms are lit
as the original lit them: the scene's ambient, each light's attenuation, and the
material's emissive colour adding to the light on the texture, clamped to full brightness.
At the ship dealer the ships for sale stand on the room's own pads (`[ForSaleShipPlacement]`),
the selected one on the pad the room camera looks at. The dealer screen itself is still our own until the original base interface is in.

## Missions (phase 6, in progress)

The story missions run from the player's own scripts in `DATA/MISSIONS/Mxx/mxx.ini`.
`parse_mission` reads the mission header, ships, NPCs, solars, formations, objective
lists, dialogs, objectives and triggers. `game::MissionRunner` runs the triggers. Each
active trigger waits for its condition (timers, comms finishing, ships destroyed by name
or label, distances, health, entering space, a base, a system or a trade lane, watching
another trigger, the player's answer to an offer, hits). Then it runs its actions.
The runner itself handles activating and deactivating triggers and the mission's success
or failure. Every other action goes to the game. The corpus test checks every story
script and lists the conditions the runner doesn't evaluate yet.

`--mission M01a` runs a story mission in flight:
- Mission ships spawn with the ship, loadout and pilot from their `[NPCShipArch]`. Whether they are hostile follows your reputation with their faction.
- Radio calls and dialogs play one after another (the speaker is shown for now; voices come with audio).
- Objectives (`SetNNObj`) show their text from your game's own resources, and the outcome is shown when the mission ends.
- Ships follow their orders (`GiveObjList`, or the list `SpawnShip` names): fly to a point or ship (cruising on long legs), follow a leader in formation, wait, dock and leave. Hostile ships break off to fight when the player comes within 1.5 km.
- `SetVibe` and its label variants make ships friendly or hostile toward the player. `Cloak` hides a ship, `RelocateShip` moves one, and `SpawnSolar` places mission objects such as bases.
- `SpawnFormation` places a formation's ships in the slots from `missions/formations.ini`. The first ship leads and the others follow it.
- Docking, launching, changing rooms and leaving a system are reported to the script (SpaceExit, BaseExit, PlayerLaunch, LaunchComplete, LocEnter/LocExit, SystemExit).
- On a base, tab 4 (key `4`) opens the bar. Entering a room counts for the script (`LocEnter`) for as long as you stay in it.
- A mission offer shows a panel with its title and text (from the offer scene's `mission_text_id`, or `SetTitle`/`SetOffer`) and "ACCEPT MISSION?". `Y` accepts and `N` declines (`MsnResponse`). For screenshot runs, `--answer accept|reject` answers it and `--launch T` launches from the base at T seconds.
- Navigation objectives (`navmarker`, `rep_inst`) and `NNPath` put a waypoint on the HUD: a marker with its name and distance, or an arrow at the edge of the view. `NNIds ..., HISTORY` and `SetNNObj ..., OBJECTIVE_HISTORY` write the mission log.
- `AddRTC`/`RemoveRTC` queue the room cutscenes from `[CharacterEncounter]` files. They count as played when you enter their room; the scenes themselves follow later.
- Saving (F5) during a mission stores its progress: the active triggers, how often each fired, and the objective, waypoint, log, offer and cutscene actions so far. Loading starts the mission again from there. A save made in space goes back to the moment you last launched, at that base, because mission ships and fights are not saved.
- Screenshot runs log how many triggers fired, which are waiting and which actions aren't handled yet. Nothing from the scripts
is stored in this repository: the unit tests use their own small scripts.

## Tools

```sh
# Print a binary INI as text, or a UTF file as a tree
./build/sv-dump --data /path/to/DATA ships/shiparch.ini

# Export a ship to glTF (.glb), with textures, parts and joints
./build/sv-export --data /path/to/DATA --ship li_elite li_elite.glb

# Mods are layers on top of the base data; later --mod wins
./build/sv-export --data /path/to/DATA --mod mymod/DATA --ship li_elite out.glb
```

## Design rules

- Clean-room: written from format observation; no code copied from other projects.
- Original data is never modified; mods are mounted as overlay layers.
- Visuals may be upgraded, but the original view distances, fog and LOD ranges stay the
  default so the sense of scale is kept. View distance is a user setting.

## Licence

[Mozilla Public License 2.0](LICENSE). Changes to existing files must be shared under the
same licence; new files and larger works (mods, tools, platform ports) may use other terms.

## Third-party code and fonts

- `third_party/stb/stb_truetype.h` by Sean Barrett: public domain or MIT, at your choice (licence at the end of the file).
- `fonts/saira-condensed/`: Saira Condensed by Omnibus-Type, SIL Open Font License 1.1 (`OFL.txt`).
