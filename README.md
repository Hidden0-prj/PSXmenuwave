# PS1 3D Model Rotation Demo

Loads your `assets/model.obj` (+ `assets/PSOne_ON.png` texture), spins it in
place using the PS1's GTE, and draws it as textured triangles. Push to GitHub
and the included Actions workflow builds it into a `.psexe` you can run in
any PS1 emulator (DuckStation, PCSX-Redux, etc).

## How the build works

This repo does **not** contain its own copy of the PS1 toolchain headers or
the GPU/GTE driver code. Instead, `.github/scripts/buildRelease.sh`:

1. Builds (or restores from cache) a `mipsel-none-elf` GCC cross-compiler.
2. Converts `assets/model.obj` → `src/model_data.h` (vertex/face data) using
   `tools/obj_to_c.py`.
3. Converts `assets/PSOne_ON.png` → `src/texture_data.h` (raw 16bpp texture
   data) using `tools/png_to_c.py`.
4. Clones [spicyjpeg/ps1-bare-metal](https://github.com/spicyjpeg/ps1-bare-metal)
   (the actual PS1 SDK: GPU/GTE drivers, linker script, startup code, etc).
   The SDK doesn't have a generic "put your code here" folder - each
   tutorial step lives in its own numbered example folder - so the script
   finds an existing example that already has the ordering-table GTE/GPU
   code wired up (`src/08_spinningCube`, falling back to
   `src/07_orderingTable`) and replaces *that* folder's `main.c` with yours,
   alongside `model_data.h` and `texture_data.h`.
5. Builds the SDK's own CMake project (which builds every example folder,
   including the seven unmodified tutorial ones) and picks out the `.psexe`
   matching the folder that was replaced.

This all happens automatically in CI - just push and check the Actions tab
for `PS1_Model_Release.zip`.

## Building locally

You'll need the same `mipsel-none-elf` GCC toolchain and a checkout of
`ps1-bare-metal`. Easiest way to reproduce it exactly is to run the same
steps as CI:

```sh
python3 tools/obj_to_c.py assets/model.obj src/model_data.h
pip3 install pillow
python3 tools/png_to_c.py assets/PSOne_ON.png src/texture_data.h
bash .github/scripts/buildToolchain.sh gcc-mipsel-none-elf mipsel-none-elf
export PATH=$PATH:$(pwd)/gcc-mipsel-none-elf/bin
bash .github/scripts/buildRelease.sh
```

## Using your own model

- Replace `assets/model.obj` (Blender export, triangulated or quads are both
  fine - the converter fan-triangulates n-gons automatically) and
  `assets/PSOne_ON.png` (must be ≤256x256, this is a PS1 GPU limit on a
  single texture page).
- If your model is much bigger/smaller than the included one, tweak
  `VERTEX_SCALE` in `tools/obj_to_c.py` and/or `GTE_TRZ` in `src/main.c`
  (how far back the model is pushed from the camera) to keep it on screen.
- If faces appear to vanish/invert, your model's winding order may be
  opposite to what's expected - try enabling "Recalculate Normals" the other
  way in Blender before exporting, or flip the backface-cull comparison in
  `src/main.c` (`<= 0` → `>= 0`).
