# Dolphin Web Performance Roadmap

This web target is a proof-of-life runtime today. The goal is to turn it into a benchmarked path toward browser-native emulation instead of treating the current software renderer and cached interpreter build as nearly playable.

## Phase 1: Benchmark Platform

Track these metrics on the same WAD, same browser, and same machine before each architecture change:

- Dolphin core speed, FPS, VPS, and max speed.
- Produced/displayed frame rate from the browser frame bridge.
- Cached interpreter active time.
- Software GX rasterizer time, batch rate, and vertex rate.
- Native software renderer XFB copy time and copied MiB/s.
- Native WebGL texture upload and present time.
- JS/WebGL draw time and texture upload time for the software fallback presenter.
- JS/WASM bridge call rate and bridge overhead.
- EFB fallback snapshot rate and capture time.
- WASM heap/memory growth and browser memory pressure.

The app exposes the current browser-side sample as `window.__dolphinBenchmarkStats`, frame rate as `window.__dolphinFrameStats`, and Dolphin core counters as `window.__dolphinPerfStats`. The native WebGL2 path also exports cumulative counters through the Emscripten bridge so the React UI can separate cached-interpreter CPU time, software rasterizer time, WebGL upload, WebGL present, XFB copy, and JS/WASM bridge overhead.

Current WiiFin WAD baseline on the local Chrome/CDP run:

- Produced frame rate: about 11-12 FPS over a 12 second sample.
- Dolphin core speed: about 18-20% of full speed.
- Browser WebGL2 draw/upload: roughly 0.06 ms draw and 0.04 ms upload per displayed frame.
- Final native software-frame copy: roughly 0.03 ms per copied frame.
- JS/WASM bridge calls: roughly 160 calls/s.

This means the current final blit path is measurable but not the main wall for this WAD. The next profiling target is deeper: CPU execution and software rasterization inside Dolphin before the RGBA frame reaches the web bridge.

## Phase 2: Browser GPU Renderer

Replace the software-renderer frame export with a real browser GPU backend:

- Start with WebGL2 for broad browser support.
- Create EFB/XFB textures and framebuffers directly in the backend.
- Keep texture cache and present work on the GPU.
- Avoid readbacks unless a game explicitly requires them.
- Treat WebGPU as a second backend once the browser video abstraction is proven.

The first gate is not perfect compatibility. It is proving this path:

```text
Dolphin video backend -> browser GPU -> canvas
```

instead of:

```text
Dolphin software renderer -> CPU RGBA frame -> JS upload -> canvas
```

Current scaffold:

- `VideoBackends/WebGL` registers an Emscripten-only `WebGL2` backend with
  conservative browser GPU capability metadata.
- The backend is selected by default for the browser harness, while
  `?renderer=software` keeps the older fallback path available.
- The backend currently owns the browser WebGL2 context and presents the
  software renderer's XFB through native C++ WebGL2. The presenter now reuses
  texture storage and updates frames with `glTexSubImage2D` instead of
  reallocating with `glTexImage2D` every frame. That removes one avoidable cost,
  but it is still a hybrid path rather than real GX-to-WebGL2 rendering.
- `WebGLVertexManager` is now compiled as an inactive GX vertex-upload scaffold.
  It owns a VAO, VBO, and IBO and stages committed vertex/index ranges with
  `glBufferSubData`; it is not selected by default until shader translation and
  native vertex-format binding are ready.
- `?renderer=webgl-gx` selects the guarded native GX experiment. That path uses
  WebGL2 textures/framebuffers, the WebGL2 vertex manager, native vertex-format
  VAOs, basic pipeline state binding, UBO uploads, and the default
  `TextureCacheBase`/hardware EFB interface. It is expected to be incomplete and
  may render black or hit shader gaps; the default `WebGL2` path remains the
  stable benchmark mode.
- `?mmu=1` enables Dolphin MMU mode before boot for titles that rely on DSI
  exception behavior. This is a compatibility mode and is expected to be slower
  than the default cached-interpreter path with MMU off.

Immediate implementation order:

1. Add a WebGL2 context owner that binds to the browser canvas supplied through
   `WindowSystemType::Web`.
2. Implement a minimal `AbstractGfx` that can clear, present, and own the EFB/XFB
   framebuffers without CPU readback.
3. Add texture and framebuffer wrappers for WebGL2 `GLuint` resources.
4. Add the simplest shader/pipeline path needed by the WiiFin WAD, then expand
   toward normal Dolphin shader generation.

## Phase 3: Tiered PPC-to-Wasm Dynarec Prototype

Build the dynarec outside full Dolphin first:

- Tier 0: cached interpreter fallback for all code.
- Tier 1: baseline PPC basic-block compiler to Wasm for hot blocks.
- Tier 2: optimized hot-block compiler after correctness is proven.
- Batch hot blocks into modules instead of compiling every tiny block separately.
- Patch a dispatch table when compiled blocks become available.
- Invalidate compiled blocks when guest code memory is written.

The first gate is a small PPC test loop that runs correctly and materially faster than the cached interpreter.

## Phase 4: Hybrid Compatibility

The expected long-term runtime is hybrid:

- Cached interpreter for cold, unsupported, or suspicious code.
- PPC-to-Wasm dynarec for hot normal code.
- WebGL2/WebGPU backend for video.
- Threaded CPU/GPU/DSP/audio where browser isolation permits it.
- Compatibility fixes for EFB access, self-modifying code, dynamic code loading, timing, and memory edge cases.

Stable 30/60 FPS for general GameCube/Wii software likely requires both the browser GPU backend and the tiered dynarec. Build flags, SIMD, pthreads, and React/canvas improvements are useful hygiene, but not the main architecture.
