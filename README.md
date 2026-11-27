# Sterrenvaarder

An open-source, clean-room engine for playing **Freelancer (2003)** on modern systems,
built from scratch and fed by the data files of your own legally owned copy.

Goals: any resolution and aspect ratio (16:9, 21:9, 32:9) with a centred HUD, a modern
renderer with upscaling, a layered mod system, and portability (Windows, Linux, macOS,
and later consoles).

> Sterrenvaarder contains **no** game code or assets. It reads them from your own install.
> Freelancer is a trademark of Microsoft; this project is not affiliated with Microsoft.

## Status: phase 1, data layer

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
