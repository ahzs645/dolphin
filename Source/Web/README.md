# Dolphin Web Harness

This is a React/Vite harness for the browser-side Dolphin Web spike. It now supports
loading a real Emscripten-built Dolphin Core bridge in addition to the tiny fallback
stub.

The current browser path can:

- select a local disc image without uploading it
- pass the `File` into a dedicated worker
- read offset/length ranges lazily from the local `Blob`
- load `dolphin-web-core.js` and `dolphin-web-core.wasm` as a pthread-enabled
  Emscripten module
- initialize the Dolphin UICommon/Core runtime with a browser MEMFS user directory
  and `WindowSystemType::Web`
- mount a local NAND directory into `/dolphin-user/Wii`
- attempt to boot the Wii Menu through `BootManager::BootCore`
- copy a mounted WAD/DOL/ELF into MEMFS and direct-boot it through
  `BootParameters::GenerateFromFile`
- render the software backend's presented frame into a browser canvas through a
  WebGL2 blitter, with Canvas 2D as a fallback
- send browser mouse movement and buttons to Wii Remote 1 through Dolphin's
  input overrider
- report browser APIs needed for later storage, rendering, input, and worker work

## Run the harness

Install the React app dependencies once:

```bash
npm install
```

Build and copy the real Dolphin WASM bridge from the Emscripten CMake build tree:

```bash
npm run build:wasm:real
```

Then start Vite:

```bash
npm run dev
```

The Vite server sends COOP/COEP headers so `SharedArrayBuffer` is available on browsers that support cross-origin isolation.

If the Emscripten build directory is not `/tmp/dolphin-wasm-probe`, set
`DOLPHIN_WASM_BUILD_DIR` before running `npm run build:wasm:real`.

```bash
DOLPHIN_WASM_BUILD_DIR=/path/to/dolphin-wasm-build npm run build:wasm:real
```

## Build the Emscripten target

From the repository root:

```bash
rm -rf /tmp/dolphin-wasm-probe
emcmake cmake -S . -B /tmp/dolphin-wasm-probe -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_GENERIC=ON \
  -DENABLE_QT=OFF \
  -DENABLE_NOGUI=ON \
  -DENABLE_CLI_TOOL=OFF \
  -DENABLE_TESTS=OFF \
  -DENABLE_VULKAN=OFF \
  -DENABLE_ALSA=OFF \
  -DENABLE_PULSEAUDIO=OFF \
  -DENABLE_CUBEB=OFF \
  -DENABLE_SDL=OFF \
  -DENABLE_LLVM=OFF \
  -DUSE_UPNP=OFF \
  -DUSE_DISCORD_PRESENCE=OFF \
  -DUSE_MGBA=OFF \
  -DENABLE_AUTOUPDATE=OFF \
  -DUSE_RETRO_ACHIEVEMENTS=OFF \
  -DENABLE_ANALYTICS=OFF \
  -DENCODE_FRAMEDUMPS=OFF \
  -DUSE_SYSTEM_LIBS=OFF

cmake --build /tmp/dolphin-wasm-probe --target dolphin-web-core
```

## Wii Menu boot requirements

The browser build cannot synthesize Nintendo NAND contents. To get past the current
expected `NANDMissing` state, use **Mount NAND** and select a locally dumped Wii NAND
or Dolphin Wii user directory containing the normal NAND layout, such as `title`,
`ticket`, `shared1`, and related system files.

The harness copies that directory into Emscripten MEMFS at `/dolphin-user/Wii` for
the current page session, then retries the Wii Menu boot path. The copy is local to
the browser process and is not uploaded.

## WAD boot testing

WAD, DOL, and ELF files are small enough to copy into Emscripten MEMFS and boot by
path. Mount one with **Open disc**, then use **Boot WAD** or **Boot file**.

For local automation, place a legally redistributable WAD under
`public/test-assets/` and load:

```text
http://127.0.0.1:5173/?boot-wad=/test-assets/example.wad
```

This path was tested with WiiFin v0.1.1 from its GitHub release. Dolphin Core
accepted the WAD, reached `Running`, and produced frames through the software
renderer to the browser canvas.

## Screen output

The first visual path uses Dolphin's Software Renderer through the browser window
system path. The Emscripten host passes `WindowSystemType::Web` into Dolphin Core,
and the software backend publishes the RGBA frame buffer from WASM memory instead
of opening a native window. The React app uploads that memory to a WebGL2 texture
and draws a full-canvas blit. If WebGL2 is unavailable, it falls back to Canvas 2D.

This is intentionally a bootstrap renderer. It proves Core → renderer → WASM
memory → browser canvas. A production path should replace this with a browser GPU
backend, likely WebGL2 first and WebGPU later.

## Browser platform boundary

The browser path is owned by Dolphin's host/platform layer, not by an external
scene renderer. The web bridge now passes `WindowSystemType::Web` through
`BootManager::BootCore`, controller initialization, and the active video backend.
That gives browser support a first-class place in Dolphin's existing platform
model.

Three.js, GSAP, or a separate canvas scene can be useful for surrounding UI, but
they should not be the Wii video renderer. The renderer should stay inside
Dolphin's `VideoBackendBase` pipeline so Core, VI/XFB/EFB presentation, input,
audio, and eventual save-state behavior all agree on the same host platform.

## Wii Remote mouse input

The web bridge registers Dolphin's existing touch input overrider for Wii Remote
1 and exports `dolphin_web_set_wiimote_mouse`. React pointer events from the
screen canvas call that export with normalized coordinates. Dolphin then sees
the mouse as normal emulated Wii Remote state:

- mouse position maps to IR X/Y pointing
- left mouse maps to A
- right mouse maps to B
- middle mouse maps to Home

## Fallback stub

`npm run build:wasm` still builds the tiny `core/dolphin_web_stub.cpp` fallback as
`public/dolphin-core/dolphin.wasm`. The app prefers the real Emscripten module when
`public/dolphin-core/dolphin-web-core.js` exists and falls back to the stub when it
does not.
