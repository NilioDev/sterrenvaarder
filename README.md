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

F3 docks at the selected base and launches again. Landing puts you in the base's start
room (its cityscape or deck); keys 1-3 switch between the
commodity trader, the equipment dealer and the ship dealer, 4 goes to the bar and 0 back
to the start room; Up/Down selects, Right buys,
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
- Without `--system`, the mission begins where its script does: the system its starting triggers are bound to, docked at the base (and in the room) they wait for. That can be aboard a ship the mission puts into space, such as a battleship with its own base. The original carries the player over from the mission before.
- Bases on mission objects (`[MsnSolar] base =`) can be targeted and docked at like stations.
- After a mission succeeds, the next story mission starts (M01a, M01b, M02 ... M13). The player carries over and is only moved when the next mission begins in another system. Mission objects stay in their own system, so a battleship a mission leaves behind is still there for the next one.
- When a room cutscene ends, the script hears `RTCDone`. A cutscene with `relocate_player = room, base` moves the player there, if need be to another system.
- `--win T` makes the running mission succeed at T seconds, to test the hand-over.
- `--thorn scene.thn,Object` plays a space cutscene around that object once you are in space. Ships the scene places itself are drawn too.
- `CallThorn = scene, object` plays a cutscene in space. Its camera takes over (Enter skips) and the flight HUD steps aside. The scene's "main object" marker is pinned to the object, so cameras and paths attached to it follow the ship. Ships placed by the scene itself are not drawn yet.
- `PlayerEnemyClamp = min, max`: at most `max` enemies attack the player at once, and the others keep 2.5 km away without firing. `RpopAttClamp = true` makes random traffic count too. This is our reading of the two actions. `Save` writes an autosave next to the quick save.
- Mission ships spawn with the ship, loadout and pilot from their `[NPCShipArch]`. Whether they are hostile follows your reputation with their faction.
- Radio calls and dialogs play one after another, spoken in the speaker's voice.
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
- `Invulnerable = ship, on [, ..., share]` stops hits at that share of the hull, or just short of destroyed without a share (our reading of the values). `AdjHealth` adds a share of the full hull, and `SetLifeTime` takes a ship out of the scene after that many seconds. `PlayerCanDock` / `PlayerCanTradelane = allowed [, exceptions]` and `LockDock` limit where the player may dock or ride a lane. `SetRep` sets a reputation, and `PlaySoundEffect` plays a sound. `MarkObj` marks a mission ship, label or object on the HUD with a diamond, its name and distance at any range. `PobjIdle` drops a ship's orders. `NagDistLeaving` / `NagDistTowards = name, speaker, line, x, y, z, text, distance` remind the player while they are farther than that distance from the point: the speaker says the line and the text shows, every 20 s until `NagOff`. Both the reading and the interval are ours.
- Screenshot runs log how many triggers fired, which are waiting and which actions aren't handled yet. Nothing from the scripts
is stored in this repository: the unit tests use their own small scripts.

## Sound (phase 7, in progress)

`decode_wav` reads the sound files of the player's own install. These are RIFF WAVE files that hold either plain PCM (8 or 16 bit) or MP3 (format 0x55, decoded with minimp3), and come out as 16-bit samples. The corpus test decodes all of them and reports how long they last together. `game::Mixer` mixes any number of sounds into 16-bit stereo, each with its own volume, looping or not, and linearly resampled to the output rate. The client opens one SDL audio stream that pulls from it. Screenshot runs open no audio device, and `--no-sound` turns sound off. `SoundDb` reads the `[Sound]` lists of the audio ini files (nickname, file, attenuation in dB, streamer), and `parse_system_music` reads a system's `[Music]` (space, danger, battle). In space the client loops the system's space track at the volume its list gives, and switches tracks when the system changes. With `SDL_AUDIO_DRIVER=dummy` a screenshot run also opens a (silent) device, for tests. The corpus test checks that every system's tracks name a known sound with a file. The track follows the moment (`game::choose_mood`). Enemies within 5 km bring the danger track, and they must be past 6.5 km before it ends. Shots on the player or on an enemy bring the battle track, which holds for 10 s after the last hit. Tracks cross-fade over 2 s (`Mixer::fade`). These distances and times are our own estimates and can be tuned. Docked, each room's `[Room_Sound]` decides: its music cross-fades in, and a room without music keeps the track that plays. Its ambience (from `gf_sounds.ini`) loops beside it and fades out on leaving. The corpus test checks that every room's music and ambience resolve to a sound with a file. Cutscenes play their sound too: each `START_SOUND` starts its SOUND entity's sound (the entity's `template_name`) once or looping, at its list's volume plus the entity's attenuation. Loops stop when the scene ends or after the event's duration, and a scene's sounds are decoded when it starts. 3,685 of the 4,317 SOUND entities name a sound from the sound lists, the scenes' spoken lines among them (`story_sounds.ini`). The other 632 are voice-profile placeholders that name no sound. Scene distance falloff (`dmin`/`dmax`) and `START_AUDIO_PROP_ANIM` volume changes are not played yet. In space, guns play their munition's `one_shot_sound` (once per munition kind and volley), the player's engine loops its `rumble_sound` and plays its cruise start, loop and stop sounds, and the shield plays its collapse and rebuilt sounds. One-shots fade with distance from the player by the sound's `range = min, max`: full up to min, then min/distance tapering to silence at max (our own curve). Other ships' engines hum too: the nearest six within their `rumble_sound`'s range loop it, as loud as their distance allows, and fade out once they leave that range or are gone. Pitch changes with speed come later. `VoiceDb` reads the voices lists (`audio/voices_*.ini`). Each `[Voice]` is followed by its lines (`[Sound]` msg, attenuation, duration), and a voice's lines live in `audio/<voice>.utf`, one WAVE leaf per line named `0x%08X` of `fl_create_id(msg)`. The corpus test finds and decodes all 23,995 lines of the 109 voices. Mission radio calls (`SendComm`, `EtherComm`, `StartDialog`) now speak: the speaking ship's NPC names its voice, the line plays, and the call lasts as long as the clip. Bases answer a docking request over the radio in the voice of the base or its dock ring (`voice =` in the system file). The answer is "this is", then the base's name, then granted, or the answer to a hostile ship, played one fragment after another (`game::dock_reply_lines`). The data has the fragments but not the order the original strings them in, so this order is our reading of the fragment names: `+` asks for a follow-up and `-` ends a phrase. When the player launches, the base clears them for a berth (stations), a ring (planets, through their dock ring) or a mooring (capital ships and liners). Which one comes from the dock's archetype name (`game::dock_kind`), our own rule. The corpus test checks that every part is a line of the base's voice. Traffic chatter comes later. A mission's `PlayMusic = space, danger, battle, motif [, fade [, once]]` swaps in its own tracks (`none` keeps the system's) and can play a motif over the mood, once or looping, with its own cross-fade time. `PlayMusic = no_params` gives the music back. We read these values from their shapes in the data (`game::parse_play_music`).

## Characters (phase 6d, in progress)

Character bodies, heads and hands are deformable models (`.dfm`), and `DfmModel` reads them:
- the detail levels (`MultiLevel/MeshN`), with the points, normals, uvs and per-corner indices of each level;
- the triangle strips of each material group;
- the skin chains that tie each point to its bones;
- the bones themselves, which are the `.3db` nodes in file order with their bind transforms, named by their `Cmpnd` parts.

Animations (`.anm`) are read by `AnmLibrary`. Each script has joint maps (parent and child bone) and object maps, and each map has a keyframed channel of times, positions and rotations. Rotations are stored as full quaternions (0x04) or packed into three shorts (0x40 / 0x80). The corpus test reads all 2,794 scripts in the data.

`CharacterParts` reads `characters/bodyparts.ini` and `characters/costumes.ini`. Bodyparts gives the mesh of each body, head, hand and accessory; a `[Skeleton]` section sets the sex of the parts after it, and accessories also name the hardpoints they attach to. A costume puts a head, body, two hands and its accessories together. The corpus test checks that every part a costume names exists and has its mesh in the data.

Skinning (`game/skin`) poses the skeleton and moves the mesh with it. `pose_bones` walks the joints of `Cmpnd/Cons` from the root down. Each joint is offset by its origin and turned by the animation's rotation at that time, with keys blended spherically; without one it keeps its rest rotation. `skin_points` then moves every point by its bones, weighted. Despite its name, the stored "Bone to root" maps root space into the bone (its inverse is where the bone sits), and body joints store no rotation of their own, so the rest rotations come from those stored transforms. The corpus test checks that the joints rebuild every stored bind pose; 201 of the 204 meshes match exactly (a hurt pose, a torture rig and a creature place a few joints elsewhere).

`parse_mbases` reads `missions/mbases.ini`: for each base, the rooms with their crowd size and their fixed characters (a spot in the room's hardpoint scene and an idle script), and the base's characters with the parts they wear. Heads and hands hang on the body by hardpoints of the same name (`hp_neck`, `hp_left b`, `hp_right b`), and `attach_part` places a part so that its hardpoint lands on the body's. The renderer loads a character mesh per instance (`load_character`) and poses it each frame (`pose_character`) into its own vertex buffer. In a room, the fixed characters (the bartender, the dealers) stand on their marker. They wear their body, head and hands, and play the looping motion of their idle script (`START_MOTION` events, read into `ThnScript::motions`), lifted by the animation's root height. `--no-characters` leaves the rooms empty.

`game::ThnPlayer` plays a scene script over time. It handles camera cuts (`SET_CAMERA`), entities moving and turning (`START_SPATIAL_PROP_ANIM`, blended linearly and spherically from where each entity was), and a camera's field of view (`START_CAMERA_PROP_ANIM`). Character motions come from `ThnScript::motions`. Paths, curves, sounds and particles follow later. In the client, a cutscene queued for a room (`AddRTC`) plays on entry. It takes over the room camera through `ThnPlayer::view` until the scene ends, and then the room's own camera returns. The scene's characters wear the costume their entity names. They stand where the scene puts them and play the motion they started last. Entities also follow paths (`START_PATH_ANIMATION` along a `MOTION_PATH`): a Catmull-Rom spline through the path's points, in the path entity's frame, from the start to the stop percent. Sounds, particles and rotation along paths come later. The corpus test checks that every scene character finds its costume and body, and how many motions find their animation. The corpus test plays every scene through and checks that the moves and cuts name its own entities and that every cut gives a valid view. The order of the stored rotation (w first) is not yet confirmed. The corpus test poses all 497 fixed characters in their idle animation. Open point: the feet then end up about 15 cm below the floor on average, so the lift may need another reference. The corpus test reads all character meshes in the data and checks their strips and skin.

## Navigation map

`M` opens the map of the system you are in, seen from above on an 8 x 8 grid (A-H across, 1-8 down). It shows the stars, planets, bases, jump gates and holes with their names, the trade lanes, where you are and which way you're heading, and the mission waypoint. `--map` opens it for screenshot runs.

## Random traffic (phase 6e, in progress)

Systems fill up with traffic from their own population zones, as in the original:
- Each zone in the system file names its encounters (`encounter = file, difficulty, chance`, with `faction` weights), its `density`, `repop_time` and `density_restriction`s.
- When you come near a zone, it sends an encounter from `missions/encounters/*.ini` once per `repop_time` until it holds its density. The ships come from the faction's `npc_ship` list (`missions/faction_prop.ini`), matched to the slot's ship class (`missions/shipclasses.ini`) and level.
- They appear out of sight (2.5 to 7 km away) inside the zone, fly in formation behind their leader, wander through the zone, and traders head for the nearest base and dock. Ships you leave more than 14 km behind make room for new ones.
- Ships from a faction that's hostile to you attack. Missions can turn traffic off (`RandomPop`) or keep it out of a sphere (`RandomPopSphere`).
- `--no-traffic` turns it off.
- `--no-characters` shows the rooms without their characters.

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
- `third_party/minimp3/minimp3.h` by lieff: CC0 public domain dedication (at the top of the file).
- `fonts/saira-condensed/`: Saira Condensed by Omnibus-Type, SIL Open Font License 1.1 (`OFL.txt`).
