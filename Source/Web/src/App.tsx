import { useEffect, useMemo, useRef, useState } from 'react';
import type { PointerEvent, ReactElement } from 'react';

import { readCapabilities } from './lib/capabilities';
import { DiscWorkerClient } from './lib/discWorkerClient';
import type { BrowserCapability, MountedDisc, ReadRange, ReadSample } from './lib/discTypes';
import { bytesToAscii, bytesToHex, formatBytes, formatOffset } from './lib/format';

const SAMPLE_RANGES: ReadRange[] = [
  { label: 'Disc header', offset: 0x0, length: 0x80 },
  { label: 'Magic word', offset: 0x18, length: 0x4 },
  { label: 'Wii region', offset: 0x4e000, length: 0x20 },
  { label: 'Apploader', offset: 0x2440, length: 0x40 }
];

function FileIcon(): ReactElement {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24">
      <path d="M6 2h8l4 4v16H6z" />
      <path d="M14 2v5h5" />
      <path d="M8.5 13h7M8.5 17h5" />
    </svg>
  );
}

function FolderIcon(): ReactElement {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24">
      <path d="M3 6.5h6l2 2h10v9.5H3z" />
      <path d="M3 8.5v-2.5c0-1.1.9-2 2-2h4l2 2h5c1.1 0 2 .9 2 2v.5" />
    </svg>
  );
}

function PlayIcon(): ReactElement {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24">
      <path d="M8 5v14l11-7z" />
    </svg>
  );
}

function ChipIcon(): ReactElement {
  return (
    <svg aria-hidden="true" viewBox="0 0 24 24">
      <rect x="7" y="7" width="10" height="10" rx="1.5" />
      <path d="M4 9h3M4 15h3M17 9h3M17 15h3M9 4v3M15 4v3M9 17v3M15 17v3" />
    </svg>
  );
}

function StatusPill({ available }: { available: boolean }): ReactElement {
  return <span className={available ? 'status ok' : 'status blocked'}>{available ? 'Ready' : 'Off'}</span>;
}

type BootState = 'idle' | 'booting' | 'success' | 'blocked';
type WasmExportFunction = (...args: number[]) => number | void;
type CCallReturn = number | string | null;
type DolphinCoreModule = {
  ccall: (
    ident: string,
    returnType: 'number' | 'string' | null,
    argTypes: Array<'number' | 'string'>,
    args: Array<number | string>
  ) => CCallReturn;
  HEAPU8: Uint8Array;
  FS: {
    analyzePath: (path: string) => { exists: boolean };
    mkdir: (path: string) => void;
    writeFile: (path: string, data: Uint8Array) => void;
  };
};
type DolphinCoreFactory = (moduleOverrides?: Record<string, unknown>) => Promise<DolphinCoreModule>;
type NandMount = { name: string; fileCount: number; bytes: number };
type WiimotePointerState = { x: number; y: number; buttons: number; active: boolean };
type FrameStats = { fps: number; frames: number; lastVersion: number; sampleMs: number };
type CorePerfStats = { fps: number; vps: number; speed: number; maxSpeed: number };
type NativeFrameStats = {
  copyCount: number;
  copyMs: number;
  copyMiB: number;
  cpuMs: number;
  cpuSamples: number;
  swRasterMs: number;
  swRasterBatches: number;
  swRasterVertices: number;
  webglUploadMs: number;
  webglUploadMiB: number;
  webglUploads: number;
  webglPresentMs: number;
  webglPresents: number;
  xfbCopyMs: number;
  xfbCopyMiB: number;
  xfbCopies: number;
  presentedFrames: number;
  nativeDraws: number;
  nativeVerticesOrIndices: number;
  nativeFramebufferCopies: number;
  nativeReadbacks: number;
  nativeTextureLoads: number;
  nativeGlErrors: number;
};
type DrawMetrics = { uploadMs: number; totalMs: number; bytes: number };
type FrameDrawResult = {
  drawn: boolean;
  version: number;
  label: string;
  source: 'xfb' | 'efb' | 'none';
  draw?: DrawMetrics;
  efbCaptureMs?: number;
};
type BenchmarkStats = {
  draws: number;
  avgDrawMs: number;
  avgUploadMs: number;
  ccallCount: number;
  ccallRate: number;
  ccallMs: number;
  ccallMsRate: number;
  avgCcallMs: number;
  efbSnapshots: number;
  avgEfbCaptureMs: number;
  nativeCopies: number;
  avgNativeCopyMs: number;
  nativeCopyMiB: number;
  cpuMsRate: number;
  swRasterMsRate: number;
  swRasterBatchesRate: number;
  avgSwRasterMs: number;
  webglUploadMsPerFrame: number;
  webglPresentMsPerFrame: number;
  xfbCopyMsPerFrame: number;
  presentedFramesRate: number;
  nativeDrawsRate: number;
  nativeVerticesOrIndicesRate: number;
  nativeFramebufferCopiesRate: number;
  nativeReadbacksRate: number;
  nativeTextureLoadsRate: number;
  nativeGlErrorsRate: number;
  sampleMs: number;
};
type FramePresenterMode = 'WebGL2 blit' | 'Canvas 2D' | 'Native WebGL2 backend';
type FramePresenter = {
  mode: FramePresenterMode;
  draw: (module: DolphinCoreModule, pointer: number, width: number, height: number, stride: number) => DrawMetrics;
};

const REAL_CORE_JS_URL = '/dolphin-core/dolphin-web-core.js';
const STUB_CORE_WASM_URL = '/dolphin-core/dolphin.wasm';
const RENDERER_QUERY_PARAM = 'renderer';
const MMU_QUERY_PARAM = 'mmu';
const CORE_STATE_LABELS = ['Uninitialized', 'Paused', 'Running', 'Stopping', 'Starting'];
const BOOT_FILE_DIR = '/dolphin-user/import';
const MAX_BOOT_COPY_BYTES = 256 * 1024 * 1024;
const WIIMOTE_MOUSE_SCALE = 10000;
const PRESENTATION_INTERVAL_MS = 1000 / 30;
const EFB_SNAPSHOT_INTERVAL_MS = 250;
const MMU_WARNING_LOG_LIMIT = 3;
const NATIVE_GX_NO_GAME_FRAME_WARNING_MS = 6000;
const EMPTY_FRAME_STATS: FrameStats = { fps: 0, frames: 0, lastVersion: 0, sampleMs: 0 };
const EMPTY_CORE_PERF_STATS: CorePerfStats = { fps: 0, vps: 0, speed: 0, maxSpeed: 0 };
const EMPTY_NATIVE_FRAME_STATS: NativeFrameStats = {
  copyCount: 0,
  copyMs: 0,
  copyMiB: 0,
  cpuMs: 0,
  cpuSamples: 0,
  swRasterMs: 0,
  swRasterBatches: 0,
  swRasterVertices: 0,
  webglUploadMs: 0,
  webglUploadMiB: 0,
  webglUploads: 0,
  webglPresentMs: 0,
  webglPresents: 0,
  xfbCopyMs: 0,
  xfbCopyMiB: 0,
  xfbCopies: 0,
  presentedFrames: 0,
  nativeDraws: 0,
  nativeVerticesOrIndices: 0,
  nativeFramebufferCopies: 0,
  nativeReadbacks: 0,
  nativeTextureLoads: 0,
  nativeGlErrors: 0
};
const EMPTY_BENCHMARK_STATS: BenchmarkStats = {
  draws: 0,
  avgDrawMs: 0,
  avgUploadMs: 0,
  ccallCount: 0,
  ccallRate: 0,
  ccallMs: 0,
  ccallMsRate: 0,
  avgCcallMs: 0,
  efbSnapshots: 0,
  avgEfbCaptureMs: 0,
  nativeCopies: 0,
  avgNativeCopyMs: 0,
  nativeCopyMiB: 0,
  cpuMsRate: 0,
  swRasterMsRate: 0,
  swRasterBatchesRate: 0,
  avgSwRasterMs: 0,
  webglUploadMsPerFrame: 0,
  webglPresentMsPerFrame: 0,
  xfbCopyMsPerFrame: 0,
  presentedFramesRate: 0,
  nativeDrawsRate: 0,
  nativeVerticesOrIndicesRate: 0,
  nativeFramebufferCopiesRate: 0,
  nativeReadbacksRate: 0,
  nativeTextureLoadsRate: 0,
  nativeGlErrorsRate: 0,
  sampleMs: 0
};

let dolphinWebCcallCount = 0;
let dolphinWebCcallMilliseconds = 0;
let dolphinWebMmuWarningLogCount = 0;
let dolphinWebMmuWarningSuppressionLogged = false;

declare global {
  interface Window {
    __dolphinCoreFactory?: DolphinCoreFactory;
    __dolphinCoreFactoryPromise?: Promise<DolphinCoreFactory>;
    __dolphinCoreModule?: DolphinCoreModule;
    __dolphinFrameStats?: FrameStats;
    __dolphinPerfStats?: CorePerfStats;
    __dolphinBenchmarkStats?: BenchmarkStats;
    __dolphinFramePresenterMode?: FramePresenterMode;
  }
}

function stateLabel(state: number): string {
  return CORE_STATE_LABELS[state] ?? `Unknown ${state}`;
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

function hasExtension(name: string, extensions: string[]): boolean {
  const lowerName = name.toLowerCase();
  return extensions.some((extension) => lowerName.endsWith(extension));
}

function isDirectBootFile(name: string): boolean {
  return hasExtension(name, ['.wad', '.dol', '.elf']);
}

function bootFileLabel(name: string): string {
  return hasExtension(name, ['.wad']) ? 'Boot WAD' : 'Boot file';
}

function sanitizeFileName(name: string): string {
  return name.replace(/[^A-Za-z0-9._-]/g, '_') || 'boot-file.wad';
}

function clampUnit(value: number): number {
  return Math.min(1, Math.max(0, value));
}

function wiimotePointerLabel(state: WiimotePointerState): string {
  if (!state.active) {
    return 'Mouse ready';
  }

  const pressed: string[] = [];
  if (state.buttons & 1) pressed.push('A');
  if (state.buttons & 2) pressed.push('B');
  if (state.buttons & 4) pressed.push('Home');

  const position = `${Math.round(state.x * 100)}%, ${Math.round(state.y * 100)}%`;
  return pressed.length > 0 ? `IR ${position} ${pressed.join('+')}` : `IR ${position}`;
}

function frameRateLabel(stats: FrameStats): string {
  return stats.sampleMs > 0 ? `${stats.fps.toFixed(1)} fps` : 'Measuring';
}

function coreSpeedLabel(stats: CorePerfStats): string {
  return stats.speed > 0 ? `${(stats.speed * 100).toFixed(0)}%` : 'Measuring';
}

function millisecondsLabel(value: number): string {
  return value > 0 ? `${value.toFixed(2)} ms` : '0.00 ms';
}

function readCorePerfStats(module: DolphinCoreModule): CorePerfStats {
  return {
    fps: ccallNumber(module, 'dolphin_web_perf_fps'),
    vps: ccallNumber(module, 'dolphin_web_perf_vps'),
    speed: ccallNumber(module, 'dolphin_web_perf_speed'),
    maxSpeed: ccallNumber(module, 'dolphin_web_perf_max_speed')
  };
}

function readNativeFrameStats(module: DolphinCoreModule): NativeFrameStats {
  return {
    copyCount: ccallNumber(module, 'dolphin_web_frame_copy_count'),
    copyMs: ccallNumber(module, 'dolphin_web_frame_copy_milliseconds'),
    copyMiB: ccallNumber(module, 'dolphin_web_frame_copy_megabytes'),
    cpuMs: ccallNumber(module, 'dolphin_web_perf_cpu_milliseconds'),
    cpuSamples: ccallNumber(module, 'dolphin_web_perf_cpu_samples'),
    swRasterMs: ccallNumber(module, 'dolphin_web_perf_sw_raster_milliseconds'),
    swRasterBatches: ccallNumber(module, 'dolphin_web_perf_sw_raster_batches'),
    swRasterVertices: ccallNumber(module, 'dolphin_web_perf_sw_raster_vertices'),
    webglUploadMs: ccallNumber(module, 'dolphin_web_perf_webgl_upload_milliseconds'),
    webglUploadMiB: ccallNumber(module, 'dolphin_web_perf_webgl_upload_megabytes'),
    webglUploads: ccallNumber(module, 'dolphin_web_perf_webgl_uploads'),
    webglPresentMs: ccallNumber(module, 'dolphin_web_perf_webgl_present_milliseconds'),
    webglPresents: ccallNumber(module, 'dolphin_web_perf_webgl_presents'),
    xfbCopyMs: ccallNumber(module, 'dolphin_web_perf_xfb_copy_milliseconds'),
    xfbCopyMiB: ccallNumber(module, 'dolphin_web_perf_xfb_copy_megabytes'),
    xfbCopies: ccallNumber(module, 'dolphin_web_perf_xfb_copies'),
    presentedFrames: ccallNumber(module, 'dolphin_web_perf_presented_frames'),
    nativeDraws: ccallNumber(module, 'dolphin_web_perf_native_draws'),
    nativeVerticesOrIndices: ccallNumber(module, 'dolphin_web_perf_native_vertices_or_indices'),
    nativeFramebufferCopies: ccallNumber(module, 'dolphin_web_perf_native_framebuffer_copies'),
    nativeReadbacks: ccallNumber(module, 'dolphin_web_perf_native_readbacks'),
    nativeTextureLoads: ccallNumber(module, 'dolphin_web_perf_native_texture_loads'),
    nativeGlErrors: ccallNumber(module, 'dolphin_web_perf_native_gl_errors')
  };
}

function fileNameFromUrl(url: URL): string {
  const lastSegment = url.pathname.split('/').filter(Boolean).at(-1);
  return lastSegment ? decodeURIComponent(lastSegment) : 'remote.wad';
}

async function hasRealCoreArtifact(): Promise<boolean> {
  const response = await fetch(REAL_CORE_JS_URL, { cache: 'no-store', method: 'HEAD' });
  return response.ok;
}

async function loadRealDolphinCore(): Promise<DolphinCoreModule> {
  const factory = await loadRealDolphinCoreFactory();

  return factory({
    locateFile: (fileName: string) => `/dolphin-core/${fileName}`,
    print: (message: string) => console.log(`[dolphin] ${message}`),
    printErr: (message: string) => {
      if (!message.trim()) {
        return;
      }

      if (isMmuMemoryWarning(message)) {
        dolphinWebMmuWarningLogCount += 1;
        if (dolphinWebMmuWarningLogCount <= MMU_WARNING_LOG_LIMIT) {
          console.warn(`[dolphin] ${message}`);
        } else if (!dolphinWebMmuWarningSuppressionLogged) {
          console.warn(
            '[dolphin] Suppressing repeated invalid-memory/MMU warnings. Retry with mmu=1 to enable Dolphin MMU exception handling for this title.'
          );
          dolphinWebMmuWarningSuppressionLogged = true;
        }
      } else {
        console.warn(`[dolphin] ${message}`);
      }

      window.dispatchEvent(new CustomEvent('dolphin-core-stderr', { detail: message }));
    }
  });
}

function loadRealDolphinCoreFactory(): Promise<DolphinCoreFactory> {
  if (window.__dolphinCoreFactory) {
    return Promise.resolve(window.__dolphinCoreFactory);
  }

  window.__dolphinCoreFactoryPromise ??= new Promise((resolve, reject) => {
    const timeout = window.setTimeout(() => {
      reject(new Error(`Timed out loading ${REAL_CORE_JS_URL}`));
    }, 30000);

    window.addEventListener(
      'dolphin-core-ready',
      () => {
        window.clearTimeout(timeout);
        if (!window.__dolphinCoreFactory) {
          reject(new Error('Dolphin core script loaded without a module factory.'));
          return;
        }
        resolve(window.__dolphinCoreFactory);
      },
      { once: true }
    );

    const script = document.createElement('script');
    script.type = 'module';
    script.textContent = `
      import createDolphinCore from "${REAL_CORE_JS_URL}";
      window.__dolphinCoreFactory = createDolphinCore;
      window.dispatchEvent(new Event("dolphin-core-ready"));
    `;
    document.head.append(script);
  });

  return window.__dolphinCoreFactoryPromise;
}

function ccallNumber(module: DolphinCoreModule, ident: string): number {
  const startedAt = performance.now();
  try {
    return Number(module.ccall(ident, 'number', [], []));
  } finally {
    dolphinWebCcallCount += 1;
    dolphinWebCcallMilliseconds += performance.now() - startedAt;
  }
}

function ccallString(module: DolphinCoreModule, ident: string): string {
  const startedAt = performance.now();
  try {
    const value = module.ccall(ident, 'string', [], []);
    return typeof value === 'string' ? value : '';
  } finally {
    dolphinWebCcallCount += 1;
    dolphinWebCcallMilliseconds += performance.now() - startedAt;
  }
}

function getRequestedCoreRenderer(): string {
  const renderer = new URLSearchParams(window.location.search)
    .get(RENDERER_QUERY_PARAM)
    ?.toLowerCase();
  if (renderer === 'software' || renderer === 'sw') {
    return 'Software Renderer';
  }
  if (renderer === 'webgl-gx' || renderer === 'webgl2-gx' || renderer === 'native-gx') {
    return 'WebGL2 GX';
  }
  return 'WebGL2';
}

function isTruthyQueryParam(value: string | null): boolean {
  if (!value) {
    return false;
  }

  return ['1', 'true', 'yes', 'on'].includes(value.toLowerCase());
}

function isMmuRequested(): boolean {
  return isTruthyQueryParam(new URLSearchParams(window.location.search).get(MMU_QUERY_PARAM));
}

function isMmuMemoryWarning(message: string): boolean {
  const lowerMessage = message.toLowerCase();
  return (
    lowerMessage.includes('invalid read from') ||
    lowerMessage.includes('invalid write to') ||
    lowerMessage.includes('enable mmu') ||
    lowerMessage.includes('game probably would have crashed on real hardware')
  );
}

function urlWithMmuEnabled(): string {
  const url = new URL(window.location.href);
  url.searchParams.set(MMU_QUERY_PARAM, '1');
  return url.toString();
}

function resetDolphinWarningCounters(): void {
  dolphinWebMmuWarningLogCount = 0;
  dolphinWebMmuWarningSuppressionLogged = false;
}

function initializeCoreRenderer(module: DolphinCoreModule): number {
  const renderer = getRequestedCoreRenderer();
  const enableMmu = isMmuRequested() ? 1 : 0;

  const startedAt = performance.now();
  try {
    return Number(
      module.ccall('dolphin_web_initialize_with_options', 'number', ['string', 'number'], [renderer, enableMmu])
    );
  } finally {
    dolphinWebCcallCount += 1;
    dolphinWebCcallMilliseconds += performance.now() - startedAt;
  }
}

function isNativeWebGL2CoreRenderer(): boolean {
  return getRequestedCoreRenderer().startsWith('WebGL2');
}

function isExperimentalWebGL2GXRenderer(): boolean {
  return getRequestedCoreRenderer() === 'WebGL2 GX';
}

function createShader(gl: WebGL2RenderingContext, type: number, source: string): WebGLShader {
  const shader = gl.createShader(type);
  if (!shader) {
    throw new Error('WebGL2 shader allocation failed');
  }

  gl.shaderSource(shader, source);
  gl.compileShader(shader);

  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const log = gl.getShaderInfoLog(shader) ?? 'unknown shader error';
    gl.deleteShader(shader);
    throw new Error(log);
  }

  return shader;
}

function createProgram(gl: WebGL2RenderingContext, vertexSource: string, fragmentSource: string): WebGLProgram {
  const program = gl.createProgram();
  if (!program) {
    throw new Error('WebGL2 program allocation failed');
  }

  const vertexShader = createShader(gl, gl.VERTEX_SHADER, vertexSource);
  const fragmentShader = createShader(gl, gl.FRAGMENT_SHADER, fragmentSource);

  gl.attachShader(program, vertexShader);
  gl.attachShader(program, fragmentShader);
  gl.linkProgram(program);
  gl.deleteShader(vertexShader);
  gl.deleteShader(fragmentShader);

  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
    const log = gl.getProgramInfoLog(program) ?? 'unknown program link error';
    gl.deleteProgram(program);
    throw new Error(log);
  }

  return program;
}

function createFramePresenter(canvas: HTMLCanvasElement): FramePresenter {
  const gl = canvas.getContext('webgl2', {
    alpha: false,
    antialias: false,
    depth: false,
    stencil: false
  });

  if (!gl) {
    return createCanvas2dPresenter(canvas);
  }

  const program = createProgram(
    gl,
    `#version 300 es
    precision highp float;
    out vec2 v_uv;
    void main() {
      vec2 raw = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
      gl_Position = vec4(raw * 2.0 - 1.0, 0.0, 1.0);
      v_uv = vec2(raw.x, 1.0 - raw.y);
    }`,
    `#version 300 es
    precision highp float;
    uniform sampler2D u_frame;
    in vec2 v_uv;
    out vec4 out_color;
    void main() {
      out_color = texture(u_frame, v_uv);
    }`
  );

  const texture = gl.createTexture();
  if (!texture) {
    throw new Error('WebGL2 texture allocation failed');
  }

  gl.useProgram(program);
  gl.uniform1i(gl.getUniformLocation(program, 'u_frame'), 0);
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);

  let textureWidth = 0;
  let textureHeight = 0;

  return {
    mode: 'WebGL2 blit',
    draw(module, pointer, width, height, stride) {
      const startedAt = performance.now();
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
      }

      const bytesPerRow = width * 4;
      if (stride < bytesPerRow || pointer <= 0) {
        throw new Error('Invalid frame buffer stride');
      }

      gl.viewport(0, 0, width, height);
      gl.useProgram(program);
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, texture);

      if (textureWidth !== width || textureHeight !== height) {
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
        textureWidth = width;
        textureHeight = height;
      }

      const uploadStartedAt = performance.now();
      if (stride % 4 === 0) {
        gl.pixelStorei(gl.UNPACK_ROW_LENGTH, stride / 4);
        gl.texSubImage2D(
          gl.TEXTURE_2D,
          0,
          0,
          0,
          width,
          height,
          gl.RGBA,
          gl.UNSIGNED_BYTE,
          module.HEAPU8.subarray(pointer, pointer + stride * height)
        );
        gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
      } else {
        gl.texSubImage2D(
          gl.TEXTURE_2D,
          0,
          0,
          0,
          width,
          height,
          gl.RGBA,
          gl.UNSIGNED_BYTE,
          packRgbaRows(module, pointer, width, height, stride)
        );
      }

      const uploadMs = performance.now() - uploadStartedAt;
      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
      return { uploadMs, totalMs: performance.now() - startedAt, bytes: stride * height };
    }
  };
}

function createCanvas2dPresenter(canvas: HTMLCanvasElement): FramePresenter {
  const context = canvas.getContext('2d');
  if (!context) {
    throw new Error('Canvas 2D unavailable');
  }

  let imageData: ImageData | null = null;

  return {
    mode: 'Canvas 2D',
    draw(module, pointer, width, height, stride) {
      const startedAt = performance.now();
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
        imageData = null;
      }

      imageData ??= new ImageData(width, height);
      const uploadStartedAt = performance.now();
      copyRgbaRows(module.HEAPU8, pointer, width, height, stride, imageData.data);
      context.putImageData(imageData, 0, 0);
      return { uploadMs: performance.now() - uploadStartedAt, totalMs: performance.now() - startedAt, bytes: stride * height };
    }
  };
}

function packRgbaRows(
  module: DolphinCoreModule,
  pointer: number,
  width: number,
  height: number,
  stride: number
): Uint8Array {
  const pixels = new Uint8Array(width * height * 4);
  copyRgbaRows(module.HEAPU8, pointer, width, height, stride, pixels);
  return pixels;
}

function copyRgbaRows(
  heap: Uint8Array,
  pointer: number,
  width: number,
  height: number,
  stride: number,
  target: Uint8Array | Uint8ClampedArray
): void {
  const bytesPerRow = width * 4;
  for (let y = 0; y < height; y += 1) {
    const sourceStart = pointer + y * stride;
    const sourceEnd = sourceStart + bytesPerRow;
    target.set(heap.subarray(sourceStart, sourceEnd), y * bytesPerRow);
  }
}

function drawLatestFrame(
  module: DolphinCoreModule,
  presenter: FramePresenter,
  previousVersion: number,
  allowEfbSnapshot: boolean
): FrameDrawResult {
  if (ccallNumber(module, 'dolphin_web_has_frame') !== 1) {
    if (!allowEfbSnapshot) {
      return { drawn: false, version: previousVersion, label: 'Waiting for XFB frame', source: 'none' };
    }

    const efbCaptureStartedAt = performance.now();
    ccallNumber(module, 'dolphin_web_capture_efb');
    const efbCaptureMs = performance.now() - efbCaptureStartedAt;
    const efbWidth = ccallNumber(module, 'dolphin_web_efb_width');
    const efbHeight = ccallNumber(module, 'dolphin_web_efb_height');
    const efbStride = ccallNumber(module, 'dolphin_web_efb_stride');
    const efbPointer = ccallNumber(module, 'dolphin_web_efb_buffer');
    const label = `EFB snapshot ${efbWidth}x${efbHeight}`;

    if (efbWidth <= 0 || efbHeight <= 0 || efbStride < efbWidth * 4 || efbPointer <= 0) {
      return {
        drawn: false,
        version: previousVersion,
        label: 'Waiting for EFB pixels',
        source: 'none',
        efbCaptureMs
      };
    }

    const draw = presenter.draw(module, efbPointer, efbWidth, efbHeight, efbStride);
    return { drawn: true, version: previousVersion < 0 ? previousVersion : -1, label, source: 'efb', draw, efbCaptureMs };
  }

  const version = ccallNumber(module, 'dolphin_web_frame_version');
  const width = ccallNumber(module, 'dolphin_web_frame_width');
  const height = ccallNumber(module, 'dolphin_web_frame_height');
  const stride = ccallNumber(module, 'dolphin_web_frame_stride');
  const pointer = ccallNumber(module, 'dolphin_web_frame_buffer');

  if (version === previousVersion || width <= 0 || height <= 0 || stride < width * 4 || pointer <= 0) {
    return { drawn: false, version, label: version > 0 ? `Frame ${version}` : 'Waiting for XFB frame', source: 'none' };
  }

  const draw = presenter.draw(module, pointer, width, height, stride);
  return { drawn: true, version, label: `Frame ${version} ${width}x${height}`, source: 'xfb', draw };
}

function ensureFsDirectory(module: DolphinCoreModule, path: string): void {
  let current = '';
  for (const segment of path.split('/').filter(Boolean)) {
    current += `/${segment}`;
    if (!module.FS.analyzePath(current).exists) {
      module.FS.mkdir(current);
    }
  }
}

async function copyDirectoryToFs(
  module: DolphinCoreModule,
  directory: FileSystemDirectoryHandle,
  targetPath: string
): Promise<{ fileCount: number; bytes: number }> {
  ensureFsDirectory(module, targetPath);

  let fileCount = 0;
  let bytes = 0;

  for await (const [name, handle] of directory.entries()) {
    const nextPath = `${targetPath}/${name}`;
    if (handle.kind === 'directory') {
      const child = await copyDirectoryToFs(module, handle as FileSystemDirectoryHandle, nextPath);
      fileCount += child.fileCount;
      bytes += child.bytes;
      continue;
    }

    const file = await (handle as FileSystemFileHandle).getFile();
    const data = new Uint8Array(await file.arrayBuffer());
    module.FS.writeFile(nextPath, data);
    fileCount += 1;
    bytes += data.byteLength;
  }

  return { fileCount, bytes };
}

async function getChildDirectory(
  directory: FileSystemDirectoryHandle,
  name: string
): Promise<FileSystemDirectoryHandle | null> {
  try {
    return await directory.getDirectoryHandle(name);
  } catch {
    return null;
  }
}

async function resolveNandRoot(
  directory: FileSystemDirectoryHandle
): Promise<{ directory: FileSystemDirectoryHandle; label: string }> {
  const wiiDirectory = await getChildDirectory(directory, 'Wii');
  if (wiiDirectory) {
    return { directory: wiiDirectory, label: `${directory.name}/Wii` };
  }

  return { directory, label: directory.name };
}

async function waitForBootState(module: DolphinCoreModule): Promise<number> {
  const startedAt = performance.now();
  let currentState = ccallNumber(module, 'dolphin_web_state');

  while (performance.now() - startedAt < 3500) {
    await delay(125);
    currentState = ccallNumber(module, 'dolphin_web_pump_host_jobs');

    if (currentState === 2) {
      return currentState;
    }

    if (currentState === 0 && performance.now() - startedAt > 500) {
      return currentState;
    }
  }

  return currentState;
}

function CapabilityList({ capabilities }: { capabilities: BrowserCapability[] }): ReactElement {
  return (
    <div className="capability-grid">
      {capabilities.map((capability) => (
        <div className="capability" key={capability.key}>
          <div>
            <strong>{capability.label}</strong>
            <span>{capability.note}</span>
          </div>
          <StatusPill available={capability.available} />
        </div>
      ))}
    </div>
  );
}

function DiscSummary({ mountedDisc }: { mountedDisc: MountedDisc | null }): ReactElement {
  if (!mountedDisc) {
    return (
      <div className="empty-state">
        <FileIcon />
        <span>No disc mounted</span>
      </div>
    );
  }

  const { header } = mountedDisc;
  const platformLabel =
    header.platform === 'gamecube'
      ? 'GameCube'
      : header.platform === 'wii'
        ? 'Wii'
        : header.platform === 'container'
          ? header.container ?? 'Container'
          : 'Unknown';

  return (
    <div className="disc-summary">
      <div>
        <span className="field-label">File</span>
        <strong>{mountedDisc.name}</strong>
      </div>
      <div>
        <span className="field-label">Size</span>
        <strong>{formatBytes(mountedDisc.size)}</strong>
      </div>
      <div>
        <span className="field-label">Platform</span>
        <strong>{platformLabel}</strong>
      </div>
      <div>
        <span className="field-label">Game ID</span>
        <strong>{header.gameId || 'Unknown'}</strong>
      </div>
      <div>
        <span className="field-label">Magic</span>
        <strong>{header.magicHex}</strong>
      </div>
      <div className="wide">
        <span className="field-label">Title</span>
        <strong>{header.title || 'Unknown'}</strong>
      </div>
    </div>
  );
}

function ReadSamples({ samples }: { samples: ReadSample[] }): ReactElement {
  if (samples.length === 0) {
    return (
      <div className="empty-state compact">
        <ChipIcon />
        <span>No read samples</span>
      </div>
    );
  }

  return (
    <div className="sample-list">
      {samples.map((sample) => (
        <div className="sample" key={`${sample.label}:${sample.offset}`}>
          <div className="sample-heading">
            <div>
              <strong>{sample.label}</strong>
              <span>
                {formatOffset(sample.offset)} · {formatBytes(sample.length)}
              </span>
            </div>
            <span>{sample.elapsedMs.toFixed(2)} ms</span>
          </div>
          <code>{sample.hex}</code>
          <code>{sample.ascii}</code>
        </div>
      ))}
    </div>
  );
}

export function App(): ReactElement {
  const workerClient = useRef<DiscWorkerClient | null>(null);
  const dolphinCore = useRef<DolphinCoreModule | null>(null);
  const mountedFile = useRef<File | null>(null);
  const fileInput = useRef<HTMLInputElement | null>(null);
  const screenCanvas = useRef<HTMLCanvasElement | null>(null);
  const framePresenter = useRef<FramePresenter | null>(null);
  const frameVersion = useRef(0);
  const framePresenterModeRef = useRef<FramePresenterMode>('Canvas 2D');
  const screenStatusRef = useRef('Waiting for browser renderer');
  const bootStateRef = useRef<BootState>('idle');
  const frameStatsWindow = useRef({ startedAt: performance.now(), frames: 0, lastVersion: 0 });
  const benchmarkWindow = useRef({
    draws: 0,
    drawMs: 0,
    uploadMs: 0,
    efbSnapshots: 0,
    efbCaptureMs: 0,
    ccallCount: 0,
    ccallMs: 0,
    nativeCopyCount: 0,
    nativeCopyMs: 0,
    nativeCopyMiB: 0,
    cpuMs: 0,
    swRasterMs: 0,
    swRasterBatches: 0,
    webglUploadMs: 0,
    webglUploads: 0,
    webglPresentMs: 0,
    webglPresents: 0,
    xfbCopyMs: 0,
    xfbCopies: 0,
    presentedFrames: 0,
    nativeDraws: 0,
    nativeVerticesOrIndices: 0,
    nativeFramebufferCopies: 0,
    nativeReadbacks: 0,
    nativeTextureLoads: 0,
    nativeGlErrors: 0
  });
  const lastPresentationAt = useRef(0);
  const lastEfbSnapshotAt = useRef(0);
  const autoBootStarted = useRef(false);
  const mmuWarningCountRef = useRef(0);
  const mmuWarningBannerShown = useRef(false);
  const [capabilities, setCapabilities] = useState<BrowserCapability[]>([]);
  const [mountedDisc, setMountedDisc] = useState<MountedDisc | null>(null);
  const [nandMount, setNandMount] = useState<NandMount | null>(null);
  const [samples, setSamples] = useState<ReadSample[]>([]);
  const [isBusy, setIsBusy] = useState(false);
  const [isMountingNand, setIsMountingNand] = useState(false);
  const [bootState, setBootState] = useState<BootState>('idle');
  const [bootMessage, setBootMessage] = useState('Waiting for Dolphin WASM core');
  const [screenStatus, setScreenStatus] = useState('Waiting for browser renderer');
  const [frameStats, setFrameStats] = useState<FrameStats>(EMPTY_FRAME_STATS);
  const [corePerfStats, setCorePerfStats] = useState<CorePerfStats>(EMPTY_CORE_PERF_STATS);
  const [benchmarkStats, setBenchmarkStats] = useState<BenchmarkStats>(EMPTY_BENCHMARK_STATS);
  const [framePresenterMode, setFramePresenterMode] = useState<FramePresenterMode>('Canvas 2D');
  const [wiimotePointer, setWiimotePointer] = useState<WiimotePointerState>({
    x: 0.5,
    y: 0.5,
    buttons: 0,
    active: false
  });
  const [error, setError] = useState<string | null>(null);
  const [mmuWarningCount, setMmuWarningCount] = useState(0);
  const [isDragging, setIsDragging] = useState(false);
  const mmuRequested = useMemo(() => isMmuRequested(), []);

  const runtimeStatus = useMemo(
    () => [
      {
        label: 'WASM core',
        value: bootState === 'success' ? 'Ready' : bootState === 'blocked' ? 'Failed' : bootState === 'booting' ? 'Booting' : 'Pending'
      },
      { label: 'CPU mode', value: 'Cached interpreter' },
      { label: 'MMU', value: mmuRequested ? 'On' : 'Off' },
      { label: 'Renderer', value: framePresenterMode },
      { label: 'FPS', value: frameRateLabel(frameStats) },
      { label: 'Core speed', value: coreSpeedLabel(corePerfStats) },
      { label: 'Wii Remote', value: wiimotePointerLabel(wiimotePointer) },
      { label: 'Disc IO', value: mountedDisc ? 'Mounted' : 'Idle' },
      { label: 'NAND', value: nandMount ? `${nandMount.fileCount} files` : 'Missing' },
      { label: 'Boot target', value: mountedDisc && isDirectBootFile(mountedDisc.name) ? mountedDisc.name : 'Wii Menu' }
    ],
    [bootState, corePerfStats, framePresenterMode, frameStats, mountedDisc, mmuRequested, nandMount, wiimotePointer]
  );

  const benchmarkStatus = useMemo(
    () => [
      { label: 'CPU active', value: `${benchmarkStats.cpuMsRate.toFixed(1)} ms/s` },
      { label: 'SW raster', value: `${benchmarkStats.swRasterMsRate.toFixed(1)} ms/s` },
      { label: 'SW batches', value: `${benchmarkStats.swRasterBatchesRate.toFixed(0)}/s` },
      { label: 'WebGL upload', value: millisecondsLabel(benchmarkStats.webglUploadMsPerFrame) },
      { label: 'WebGL present', value: millisecondsLabel(benchmarkStats.webglPresentMsPerFrame) },
      { label: 'Presented', value: `${benchmarkStats.presentedFramesRate.toFixed(0)}/s` },
      { label: 'Native draws', value: `${benchmarkStats.nativeDrawsRate.toFixed(0)}/s` },
      { label: 'Native verts', value: `${benchmarkStats.nativeVerticesOrIndicesRate.toFixed(0)}/s` },
      { label: 'FB blits', value: `${benchmarkStats.nativeFramebufferCopiesRate.toFixed(0)}/s` },
      { label: 'Readbacks', value: `${benchmarkStats.nativeReadbacksRate.toFixed(0)}/s` },
      { label: 'Tex loads', value: `${benchmarkStats.nativeTextureLoadsRate.toFixed(0)}/s` },
      { label: 'GL errors', value: `${benchmarkStats.nativeGlErrorsRate.toFixed(0)}/s` },
      { label: 'XFB copy', value: millisecondsLabel(benchmarkStats.xfbCopyMsPerFrame) },
      { label: 'JS bridge', value: `${benchmarkStats.ccallMsRate.toFixed(2)} ms/s` },
      { label: 'Bridge calls', value: `${benchmarkStats.ccallRate.toFixed(0)}/s` },
      { label: 'EFB fallback', value: `${benchmarkStats.efbSnapshots}/s` },
      { label: 'Copied', value: `${benchmarkStats.nativeCopyMiB.toFixed(2)} MiB/s` }
    ],
    [benchmarkStats]
  );

  const canBootMountedFile = Boolean(mountedDisc && mountedFile.current && isDirectBootFile(mountedDisc.name));

  function updateScreenStatus(nextStatus: string): void {
    if (nextStatus === screenStatusRef.current) {
      return;
    }

    screenStatusRef.current = nextStatus;
    setScreenStatus(nextStatus);
  }

  function recordFrameDraw(result: FrameDrawResult): void {
    const stats = benchmarkWindow.current;

    if (result.draw) {
      stats.draws += 1;
      stats.drawMs += result.draw.totalMs;
      stats.uploadMs += result.draw.uploadMs;
    }

    if (typeof result.efbCaptureMs === 'number') {
      stats.efbSnapshots += 1;
      stats.efbCaptureMs += result.efbCaptureMs;
    }
  }

  function updateFrameStats(module: DolphinCoreModule, version: number): void {
    if (version <= 0) {
      return;
    }

    const now = performance.now();
    const statsWindow = frameStatsWindow.current;

    if (statsWindow.lastVersion === 0) {
      statsWindow.lastVersion = version;
      statsWindow.startedAt = now;
      statsWindow.frames = 0;
      window.__dolphinFrameStats = EMPTY_FRAME_STATS;
      return;
    } else if (version > statsWindow.lastVersion) {
      statsWindow.frames += version - statsWindow.lastVersion;
      statsWindow.lastVersion = version;
    }

    const sampleMs = now - statsWindow.startedAt;
    if (sampleMs < 1000) {
      return;
    }

    const nextStats = {
      fps: (statsWindow.frames * 1000) / sampleMs,
      frames: statsWindow.frames,
      lastVersion: statsWindow.lastVersion,
      sampleMs
    };
    const nextPerfStats = readCorePerfStats(module);
    const stats = benchmarkWindow.current;
    const nativeFrameStats = readNativeFrameStats(module);
    const ccallCount = dolphinWebCcallCount - stats.ccallCount;
    const ccallMs = dolphinWebCcallMilliseconds - stats.ccallMs;
    const nativeCopies = nativeFrameStats.copyCount - stats.nativeCopyCount;
    const nativeCopyMs = nativeFrameStats.copyMs - stats.nativeCopyMs;
    const nativeCopyMiB = nativeFrameStats.copyMiB - stats.nativeCopyMiB;
    const cpuMs = nativeFrameStats.cpuMs - stats.cpuMs;
    const swRasterMs = nativeFrameStats.swRasterMs - stats.swRasterMs;
    const swRasterBatches = nativeFrameStats.swRasterBatches - stats.swRasterBatches;
    const webglUploadMs = nativeFrameStats.webglUploadMs - stats.webglUploadMs;
    const webglUploads = nativeFrameStats.webglUploads - stats.webglUploads;
    const webglPresentMs = nativeFrameStats.webglPresentMs - stats.webglPresentMs;
    const webglPresents = nativeFrameStats.webglPresents - stats.webglPresents;
    const xfbCopyMs = nativeFrameStats.xfbCopyMs - stats.xfbCopyMs;
    const xfbCopies = nativeFrameStats.xfbCopies - stats.xfbCopies;
    const presentedFrames = nativeFrameStats.presentedFrames - stats.presentedFrames;
    const nativeDraws = nativeFrameStats.nativeDraws - stats.nativeDraws;
    const nativeVerticesOrIndices =
      nativeFrameStats.nativeVerticesOrIndices - stats.nativeVerticesOrIndices;
    const nativeFramebufferCopies =
      nativeFrameStats.nativeFramebufferCopies - stats.nativeFramebufferCopies;
    const nativeReadbacks = nativeFrameStats.nativeReadbacks - stats.nativeReadbacks;
    const nativeTextureLoads = nativeFrameStats.nativeTextureLoads - stats.nativeTextureLoads;
    const nativeGlErrors = nativeFrameStats.nativeGlErrors - stats.nativeGlErrors;
    const nextBenchmarkStats = {
      draws: stats.draws,
      avgDrawMs: stats.draws > 0 ? stats.drawMs / stats.draws : 0,
      avgUploadMs: stats.draws > 0 ? stats.uploadMs / stats.draws : 0,
      ccallCount,
      ccallRate: (ccallCount * 1000) / sampleMs,
      ccallMs,
      ccallMsRate: (ccallMs * 1000) / sampleMs,
      avgCcallMs: ccallCount > 0 ? ccallMs / ccallCount : 0,
      efbSnapshots: Math.round((stats.efbSnapshots * 1000) / sampleMs),
      avgEfbCaptureMs: stats.efbSnapshots > 0 ? stats.efbCaptureMs / stats.efbSnapshots : 0,
      nativeCopies,
      avgNativeCopyMs: nativeCopies > 0 ? nativeCopyMs / nativeCopies : 0,
      nativeCopyMiB: (nativeCopyMiB * 1000) / sampleMs,
      cpuMsRate: (cpuMs * 1000) / sampleMs,
      swRasterMsRate: (swRasterMs * 1000) / sampleMs,
      swRasterBatchesRate: (swRasterBatches * 1000) / sampleMs,
      avgSwRasterMs: swRasterBatches > 0 ? swRasterMs / swRasterBatches : 0,
      webglUploadMsPerFrame: webglUploads > 0 ? webglUploadMs / webglUploads : 0,
      webglPresentMsPerFrame: webglPresents > 0 ? webglPresentMs / webglPresents : 0,
      xfbCopyMsPerFrame: xfbCopies > 0 ? xfbCopyMs / xfbCopies : 0,
      presentedFramesRate: (presentedFrames * 1000) / sampleMs,
      nativeDrawsRate: (nativeDraws * 1000) / sampleMs,
      nativeVerticesOrIndicesRate: (nativeVerticesOrIndices * 1000) / sampleMs,
      nativeFramebufferCopiesRate: (nativeFramebufferCopies * 1000) / sampleMs,
      nativeReadbacksRate: (nativeReadbacks * 1000) / sampleMs,
      nativeTextureLoadsRate: (nativeTextureLoads * 1000) / sampleMs,
      nativeGlErrorsRate: (nativeGlErrors * 1000) / sampleMs,
      sampleMs
    };

    window.__dolphinFrameStats = nextStats;
    window.__dolphinPerfStats = nextPerfStats;
    window.__dolphinBenchmarkStats = nextBenchmarkStats;
    setFrameStats(nextStats);
    setCorePerfStats(nextPerfStats);
    setBenchmarkStats(nextBenchmarkStats);
    statsWindow.startedAt = now;
    statsWindow.frames = 0;
    benchmarkWindow.current = {
      draws: 0,
      drawMs: 0,
      uploadMs: 0,
      efbSnapshots: 0,
      efbCaptureMs: 0,
      ccallCount: dolphinWebCcallCount,
      ccallMs: dolphinWebCcallMilliseconds,
      nativeCopyCount: nativeFrameStats.copyCount,
      nativeCopyMs: nativeFrameStats.copyMs,
      nativeCopyMiB: nativeFrameStats.copyMiB,
      cpuMs: nativeFrameStats.cpuMs,
      swRasterMs: nativeFrameStats.swRasterMs,
      swRasterBatches: nativeFrameStats.swRasterBatches,
      webglUploadMs: nativeFrameStats.webglUploadMs,
      webglUploads: nativeFrameStats.webglUploads,
      webglPresentMs: nativeFrameStats.webglPresentMs,
      webglPresents: nativeFrameStats.webglPresents,
      xfbCopyMs: nativeFrameStats.xfbCopyMs,
      xfbCopies: nativeFrameStats.xfbCopies,
      presentedFrames: nativeFrameStats.presentedFrames,
      nativeDraws: nativeFrameStats.nativeDraws,
      nativeVerticesOrIndices: nativeFrameStats.nativeVerticesOrIndices,
      nativeFramebufferCopies: nativeFrameStats.nativeFramebufferCopies,
      nativeReadbacks: nativeFrameStats.nativeReadbacks,
      nativeTextureLoads: nativeFrameStats.nativeTextureLoads,
      nativeGlErrors: nativeFrameStats.nativeGlErrors
    };
  }

  function resetFrameStats(module?: DolphinCoreModule): void {
    const nativeFrameStats = module ? readNativeFrameStats(module) : EMPTY_NATIVE_FRAME_STATS;
    frameVersion.current = 0;
    frameStatsWindow.current = { startedAt: performance.now(), frames: 0, lastVersion: 0 };
    benchmarkWindow.current = {
      draws: 0,
      drawMs: 0,
      uploadMs: 0,
      efbSnapshots: 0,
      efbCaptureMs: 0,
      ccallCount: dolphinWebCcallCount,
      ccallMs: dolphinWebCcallMilliseconds,
      nativeCopyCount: nativeFrameStats.copyCount,
      nativeCopyMs: nativeFrameStats.copyMs,
      nativeCopyMiB: nativeFrameStats.copyMiB,
      cpuMs: nativeFrameStats.cpuMs,
      swRasterMs: nativeFrameStats.swRasterMs,
      swRasterBatches: nativeFrameStats.swRasterBatches,
      webglUploadMs: nativeFrameStats.webglUploadMs,
      webglUploads: nativeFrameStats.webglUploads,
      webglPresentMs: nativeFrameStats.webglPresentMs,
      webglPresents: nativeFrameStats.webglPresents,
      xfbCopyMs: nativeFrameStats.xfbCopyMs,
      xfbCopies: nativeFrameStats.xfbCopies,
      presentedFrames: nativeFrameStats.presentedFrames,
      nativeDraws: nativeFrameStats.nativeDraws,
      nativeVerticesOrIndices: nativeFrameStats.nativeVerticesOrIndices,
      nativeFramebufferCopies: nativeFrameStats.nativeFramebufferCopies,
      nativeReadbacks: nativeFrameStats.nativeReadbacks,
      nativeTextureLoads: nativeFrameStats.nativeTextureLoads,
      nativeGlErrors: nativeFrameStats.nativeGlErrors
    };
    window.__dolphinFrameStats = EMPTY_FRAME_STATS;
    window.__dolphinPerfStats = EMPTY_CORE_PERF_STATS;
    window.__dolphinBenchmarkStats = EMPTY_BENCHMARK_STATS;
    setFrameStats(EMPTY_FRAME_STATS);
    setCorePerfStats(EMPTY_CORE_PERF_STATS);
    setBenchmarkStats(EMPTY_BENCHMARK_STATS);
  }

  function resetMmuWarningState(): void {
    mmuWarningCountRef.current = 0;
    mmuWarningBannerShown.current = false;
    setMmuWarningCount(0);
    resetDolphinWarningCounters();
  }

  useEffect(() => {
    workerClient.current = new DiscWorkerClient();
    setCapabilities(readCapabilities());

    return () => {
      workerClient.current?.terminate();
      workerClient.current = null;
      dolphinCore.current?.ccall('dolphin_web_stop', null, [], []);
      dolphinCore.current = null;
      framePresenter.current = null;
      delete window.__dolphinCoreModule;
      delete window.__dolphinFrameStats;
      delete window.__dolphinPerfStats;
      delete window.__dolphinBenchmarkStats;
      delete window.__dolphinFramePresenterMode;
    };
  }, []);

  useEffect(() => {
    bootStateRef.current = bootState;
  }, [bootState]);

  useEffect(() => {
    const onCoreStderr = (event: Event) => {
      const message = (event as CustomEvent<string>).detail ?? '';
      if (!message.trim()) {
        return;
      }

      if (isMmuMemoryWarning(message)) {
        const nextCount = mmuWarningCountRef.current + 1;
        mmuWarningCountRef.current = nextCount;
        if (nextCount <= MMU_WARNING_LOG_LIMIT || nextCount % 25 === 0) {
          setMmuWarningCount(nextCount);
        }

        if (!mmuWarningBannerShown.current) {
          mmuWarningBannerShown.current = true;
          setError(
            mmuRequested
              ? 'Dolphin still reported invalid emulated memory access with MMU enabled. This is likely a browser-build compatibility issue for this title.'
              : 'Dolphin reported invalid emulated memory access. Retry with MMU enabled for better compatibility; it will run slower.'
          );
        }
      }
    };

    window.addEventListener('dolphin-core-stderr', onCoreStderr);
    return () => window.removeEventListener('dolphin-core-stderr', onCoreStderr);
  }, [mmuRequested]);

  useEffect(() => {
    const params = new URLSearchParams(window.location.search);
    if (autoBootStarted.current) {
      return;
    }

    const bootWadUrl = params.get('boot-wad');
    if (bootWadUrl) {
      autoBootStarted.current = true;
      void bootRemoteFile(bootWadUrl);
      return;
    }

    if (params.get('boot') === 'wii-menu') {
      autoBootStarted.current = true;
      void attemptWiiMenuBoot();
    }
  }, []);

  useEffect(() => {
    let animationFrame = 0;
    let cancelled = false;

    const tick = () => {
      const module = dolphinCore.current;
      const canvas = screenCanvas.current;
      const now = performance.now();

      if (
        module &&
        isNativeWebGL2CoreRenderer() &&
        now - lastPresentationAt.current >= PRESENTATION_INTERVAL_MS
      ) {
        lastPresentationAt.current = now;
        framePresenter.current = null;
        const nativeVersion = ccallNumber(module, 'dolphin_web_frame_version');
        if (framePresenterModeRef.current !== 'Native WebGL2 backend') {
          window.__dolphinFramePresenterMode = 'Native WebGL2 backend';
          framePresenterModeRef.current = 'Native WebGL2 backend';
          setFramePresenterMode('Native WebGL2 backend');
          updateScreenStatus('Native WebGL2 backend owns canvas');
        }
        updateFrameStats(module, nativeVersion);
        if (nativeVersion > 0 && nativeVersion !== frameVersion.current) {
          frameVersion.current = nativeVersion;
          updateScreenStatus(`Native frame ${nativeVersion}`);
        } else {
          if (
            isExperimentalWebGL2GXRenderer() &&
            bootStateRef.current === 'success' &&
            nativeVersion <= 1 &&
            now - frameStatsWindow.current.startedAt >= NATIVE_GX_NO_GAME_FRAME_WARNING_MS
          ) {
            updateScreenStatus('Native GX context is alive; no game GPU frame has presented yet');
          }
          setCorePerfStats(readCorePerfStats(module));
        }
      } else if (module && canvas && now - lastPresentationAt.current >= PRESENTATION_INTERVAL_MS) {
        lastPresentationAt.current = now;
        try {
          const presenter = framePresenter.current ?? createFramePresenter(canvas);
          framePresenter.current = presenter;

          if (presenter.mode !== framePresenterModeRef.current) {
            window.__dolphinFramePresenterMode = presenter.mode;
            framePresenterModeRef.current = presenter.mode;
            setFramePresenterMode(presenter.mode);
          }

          const allowEfbSnapshot =
            bootStateRef.current === 'success' &&
            now - lastEfbSnapshotAt.current >= EFB_SNAPSHOT_INTERVAL_MS;
          if (allowEfbSnapshot) {
            lastEfbSnapshotAt.current = now;
          }

          const result = drawLatestFrame(module, presenter, frameVersion.current, allowEfbSnapshot);
          recordFrameDraw(result);
          updateFrameStats(module, result.version);
          if (result.version !== frameVersion.current || result.label !== screenStatusRef.current) {
            frameVersion.current = result.version;
            updateScreenStatus(result.label);
          }
        } catch (frameError) {
          const message = frameError instanceof Error ? frameError.message : 'Canvas frame draw failed';
          updateScreenStatus(message);
        }
      }

      if (!cancelled) {
        animationFrame = window.requestAnimationFrame(tick);
      }
    };

    animationFrame = window.requestAnimationFrame(tick);
    return () => {
      cancelled = true;
      window.cancelAnimationFrame(animationFrame);
    };
  }, []);

  function updateWiimoteMouse(event: PointerEvent<HTMLCanvasElement>, active: boolean): void {
    event.preventDefault();

    const bounds = event.currentTarget.getBoundingClientRect();
    const xRatio = bounds.width > 0 ? clampUnit((event.clientX - bounds.left) / bounds.width) : 0.5;
    const yRatio = bounds.height > 0 ? clampUnit((event.clientY - bounds.top) / bounds.height) : 0.5;
    const buttons = active ? event.buttons & 7 : 0;
    const x = Math.round(xRatio * WIIMOTE_MOUSE_SCALE);
    const y = Math.round(yRatio * WIIMOTE_MOUSE_SCALE);

    dolphinCore.current?.ccall(
      'dolphin_web_set_wiimote_mouse',
      'number',
      ['number', 'number', 'number', 'number', 'number'],
      [0, x, y, buttons, active ? 1 : 0]
    );

    setWiimotePointer({ x: xRatio, y: yRatio, buttons, active });
  }

  function handleScreenPointerDown(event: PointerEvent<HTMLCanvasElement>): void {
    event.currentTarget.setPointerCapture(event.pointerId);
    updateWiimoteMouse(event, true);
  }

  function handleScreenPointerMove(event: PointerEvent<HTMLCanvasElement>): void {
    updateWiimoteMouse(event, true);
  }

  function handleScreenPointerUp(event: PointerEvent<HTMLCanvasElement>): void {
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }
    updateWiimoteMouse(event, true);
  }

  function handleScreenPointerLeave(event: PointerEvent<HTMLCanvasElement>): void {
    if (event.buttons === 0) {
      updateWiimoteMouse(event, false);
    }
  }

  function handleScreenPointerCancel(event: PointerEvent<HTMLCanvasElement>): void {
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }
    updateWiimoteMouse(event, false);
  }

  async function mountFile(file: File): Promise<void> {
    if (!workerClient.current) {
      return;
    }

    setError(null);
    setIsBusy(true);

    try {
      const mounted = await workerClient.current.mountFile(file);
      setMountedDisc(mounted);
      mountedFile.current = file;

      const nextSamples = await Promise.all(
        SAMPLE_RANGES.filter((range) => range.offset < file.size).map(async (range) => {
          const startedAt = performance.now();
          const bytes = await workerClient.current!.readRange(range.offset, range.length);
          return {
            ...range,
            length: bytes.byteLength,
            elapsedMs: performance.now() - startedAt,
            hex: bytesToHex(bytes),
            ascii: bytesToAscii(bytes)
          };
        })
      );

      setSamples(nextSamples);
    } catch (mountError) {
      setMountedDisc(null);
      mountedFile.current = null;
      setSamples([]);
      setError(mountError instanceof Error ? mountError.message : 'Failed to mount file');
    } finally {
      setIsBusy(false);
    }
  }

  async function openNativePicker(): Promise<void> {
    if (typeof window.showOpenFilePicker === 'function') {
      const [handle] = await window.showOpenFilePicker({
        multiple: false,
        types: [
          {
            description: 'Disc images',
            accept: {
              'application/octet-stream': ['.iso', '.rvz', '.gcz', '.wia', '.wbfs', '.ciso', '.dol', '.elf', '.wad']
            }
          }
        ]
      });
      await mountFile(await handle.getFile());
      return;
    }

    fileInput.current?.click();
  }

  async function ensureDolphinCore(): Promise<DolphinCoreModule> {
    if (!(await hasRealCoreArtifact())) {
      throw new Error('The real Dolphin web core is not available. Run npm run build:wasm:real first.');
    }

    const module = dolphinCore.current ?? (await loadRealDolphinCore());
    dolphinCore.current = module;
    window.__dolphinCoreModule = module;

    const initResult = initializeCoreRenderer(module);
    if (initResult <= 0) {
      throw new Error(ccallString(module, 'dolphin_web_last_status') || `Dolphin init failed with ${initResult}.`);
    }

    return module;
  }

  async function bootFileBytes(name: string, bytes: Uint8Array): Promise<void> {
    if (!isDirectBootFile(name)) {
      throw new Error('Only WAD, DOL, and ELF files can be direct-booted from the browser bridge.');
    }

    if (bytes.byteLength > MAX_BOOT_COPY_BYTES) {
      throw new Error(
        `Refusing to copy ${formatBytes(bytes.byteLength)} into WASM memory. Use this path for WAD/DOL/ELF files, not large disc images.`
      );
    }

    const module = await ensureDolphinCore();
    ensureFsDirectory(module, BOOT_FILE_DIR);

    const bootPath = `${BOOT_FILE_DIR}/${sanitizeFileName(name)}`;
    module.FS.writeFile(bootPath, bytes);

    resetFrameStats(module);
    setBootMessage(`Calling BootCore for ${name}`);
    const bootResult = Number(module.ccall('dolphin_web_boot_path', 'number', ['string'], [bootPath]));
    const status = ccallString(module, 'dolphin_web_last_status');

    if (bootResult !== 1 && bootResult !== 2) {
      throw new Error(status || `Dolphin rejected ${name} with status ${bootResult}.`);
    }

    const coreState = await waitForBootState(module);
    if (coreState === 0) {
      throw new Error(`${status || `${name} ended before reaching Running.`} Current core state: ${stateLabel(coreState)}.`);
    }

    setBootMessage(`${status || `BootCore accepted ${name}`}; state ${stateLabel(coreState)}`);
    setBootState('success');
  }

  async function bootFile(file: File): Promise<void> {
    resetMmuWarningState();
    setError(null);
    setBootState('booting');
    setBootMessage(`Copying ${file.name} into Dolphin MEMFS`);

    try {
      const bytes = new Uint8Array(await file.arrayBuffer());
      await bootFileBytes(file.name, bytes);
    } catch (bootError) {
      setBootState('blocked');
      setBootMessage(bootError instanceof Error ? bootError.message : `Failed to boot ${file.name}`);
    }
  }

  async function bootMountedFile(): Promise<void> {
    const file = mountedFile.current;
    if (!file) {
      setError('No local WAD, DOL, or ELF file is mounted.');
      return;
    }

    await bootFile(file);
  }

  async function bootRemoteFile(rawUrl: string): Promise<void> {
    resetMmuWarningState();
    setError(null);
    setBootState('booting');

    try {
      const url = new URL(rawUrl, window.location.href);
      const name = fileNameFromUrl(url);
      setBootMessage(`Fetching ${name}`);

      const response = await fetch(url, { cache: 'no-store' });
      if (!response.ok) {
        throw new Error(`Failed to fetch ${url.toString()}: HTTP ${response.status}`);
      }

      const file = new File([await response.arrayBuffer()], name, {
        type: response.headers.get('content-type') || 'application/octet-stream'
      });
      await mountFile(file);
      await bootFile(file);
    } catch (bootError) {
      setBootState('blocked');
      setBootMessage(bootError instanceof Error ? bootError.message : 'Failed to boot remote file');
    }
  }

  async function mountNandDirectory(): Promise<void> {
    if (typeof window.showDirectoryPicker !== 'function') {
      setError('This browser does not expose showDirectoryPicker. Use Chromium with File System Access enabled.');
      return;
    }

    setError(null);
    setIsMountingNand(true);
    setBootMessage('Preparing Dolphin filesystem for NAND mount');

    try {
      const directory = await window.showDirectoryPicker({ id: 'dolphin-wii-nand', mode: 'read' });
      const module = await ensureDolphinCore();
      const nandRoot = await resolveNandRoot(directory);
      const mounted = await copyDirectoryToFs(module, nandRoot.directory, '/dolphin-user/Wii');

      setNandMount({ name: nandRoot.label, ...mounted });
      setBootMessage(
        `Mounted ${mounted.fileCount} NAND files from ${nandRoot.label} (${formatBytes(mounted.bytes)})`
      );
      setBootState('idle');
    } catch (mountError) {
      setBootState('blocked');
      setBootMessage(mountError instanceof Error ? mountError.message : 'Failed to mount NAND folder');
    } finally {
      setIsMountingNand(false);
    }
  }

  async function attemptWiiMenuBoot(): Promise<void> {
    resetMmuWarningState();
    setError(null);
    setBootState('booting');
    setBootMessage('Looking for the real Dolphin web core');

    try {
      if (await hasRealCoreArtifact()) {
        setBootMessage('Loading dolphin-web-core.js');
        const module = await ensureDolphinCore();
        const version = ccallNumber(module, 'dolphin_web_core_version');

        resetFrameStats(module);
        setBootMessage('Calling BootCore for Wii Menu');
        const bootResult = ccallNumber(module, 'boot_wii_menu');
        const status = ccallString(module, 'dolphin_web_last_status');

        if (bootResult !== 1 && bootResult !== 2) {
          throw new Error(status || `Dolphin rejected the Wii Menu boot target with status ${bootResult}.`);
        }

        const coreState = await waitForBootState(module);
        if (coreState === 0) {
          throw new Error(
            `${status || 'Wii Menu boot ended before reaching Running.'} Current core state: ${stateLabel(coreState)}.`
          );
        }

        setBootMessage(`Dolphin web core v${version} accepted Wii Menu boot; state ${stateLabel(coreState)}`);
        setBootState('success');
        return;
      }

      setBootMessage(`Fetching ${STUB_CORE_WASM_URL}`);
      const response = await fetch(STUB_CORE_WASM_URL, { cache: 'no-store' });
      const contentType = response.headers.get('content-type') ?? '';

      if (!response.ok || !contentType.includes('wasm')) {
        throw new Error(
          'Dolphin WASM core is not available. Build the dolphin-web-core CMake target or the fallback stub before booting Wii Menu.'
        );
      }

      const { instance } = await WebAssembly.instantiateStreaming(response);
      const bootWiiMenu = instance.exports.boot_wii_menu;
      const coreVersion = instance.exports.dolphin_web_core_version;

      if (typeof bootWiiMenu !== 'function') {
        throw new Error('Dolphin WASM core loaded, but export boot_wii_menu was not found.');
      }

      const result = Number((bootWiiMenu as WasmExportFunction)());
      const version = typeof coreVersion === 'function' ? Number((coreVersion as WasmExportFunction)()) : 0;

      if (result !== 1) {
        throw new Error(`Dolphin WASM core rejected the Wii Menu boot target with status ${result}.`);
      }

      setBootMessage(`Fallback WASM stub v${version} accepted Wii Menu boot target`);
      setBootState('success');
    } catch (bootError) {
      setBootState('blocked');
      setBootMessage(bootError instanceof Error ? bootError.message : 'Wii Menu boot failed');
    }
  }

  function onDrop(event: React.DragEvent<HTMLElement>): void {
    event.preventDefault();
    setIsDragging(false);

    const [file] = Array.from(event.dataTransfer.files);
    if (file) {
      void mountFile(file);
    }
  }

  return (
    <main className="app-shell">
      <section
        className={`mount-panel ${isDragging ? 'dragging' : ''}`}
        onDragOver={(event) => {
          event.preventDefault();
          setIsDragging(true);
        }}
        onDragLeave={() => setIsDragging(false)}
        onDrop={onDrop}
      >
        <div className="title-block">
          <span>Dolphin Web</span>
          <h1>Browser runtime harness</h1>
        </div>

        <div className="mount-actions">
          <button className="primary-button" disabled={isBusy} type="button" onClick={() => void openNativePicker()}>
            <FileIcon />
            {isBusy ? 'Reading' : 'Open disc'}
          </button>
          <button
            className="secondary-button"
            disabled={isMountingNand || bootState === 'booting'}
            type="button"
            onClick={() => void mountNandDirectory()}
          >
            <FolderIcon />
            {isMountingNand ? 'Mounting' : 'Mount NAND'}
          </button>
          <button
            className="secondary-button"
            disabled={bootState === 'booting' || isMountingNand}
            type="button"
            onClick={() => void attemptWiiMenuBoot()}
          >
            <PlayIcon />
            {bootState === 'booting' ? 'Booting' : 'Boot Wii Menu'}
          </button>
          <button
            className="secondary-button"
            disabled={!canBootMountedFile || bootState === 'booting' || isMountingNand}
            type="button"
            onClick={() => void bootMountedFile()}
          >
            <PlayIcon />
            {mountedDisc ? bootFileLabel(mountedDisc.name) : 'Boot WAD'}
          </button>
          <input
            ref={fileInput}
            accept=".iso,.rvz,.gcz,.wia,.wbfs,.ciso,.dol,.elf,.wad"
            hidden
            type="file"
            onChange={(event) => {
              const file = event.target.files?.[0];
              if (file) {
                void mountFile(file);
              }
              event.currentTarget.value = '';
            }}
          />
        </div>

        {error ? (
          <div className="error-banner" role="alert">
            <span>
              {error}
              {mmuWarningCount > 1 ? ` (${mmuWarningCount} warnings)` : null}
            </span>
            {!mmuRequested && mmuWarningCount > 0 ? (
              <button type="button" onClick={() => window.location.assign(urlWithMmuEnabled())}>
                Retry with MMU
              </button>
            ) : null}
          </div>
        ) : null}
      </section>

      <section className="content-grid">
        <div className="panel">
          <div className="panel-heading">
            <h2>Mounted Disc</h2>
            <StatusPill available={Boolean(mountedDisc)} />
          </div>
          <DiscSummary mountedDisc={mountedDisc} />
        </div>

        <div className="panel">
          <div className="panel-heading">
            <h2>Runtime</h2>
            <StatusPill available={bootState === 'success'} />
          </div>
          <div className="runtime-grid">
            {runtimeStatus.map((item) => (
              <div key={item.label}>
                <span className="field-label">{item.label}</span>
                <strong>{item.value}</strong>
              </div>
            ))}
          </div>
          <div className={`boot-message ${bootState}`}>
            <span className="field-label">Boot log</span>
            <strong>{bootMessage}</strong>
          </div>
        </div>

        <div className="panel wide-panel">
          <div className="panel-heading">
            <h2>Benchmark</h2>
            <StatusPill available={benchmarkStats.sampleMs > 0} />
          </div>
          <div className="runtime-grid">
            {benchmarkStatus.map((item) => (
              <div key={item.label}>
                <span className="field-label">{item.label}</span>
                <strong>{item.value}</strong>
              </div>
            ))}
          </div>
        </div>

        <div className="panel wide-panel screen-panel">
          <div className="panel-heading">
            <h2>Screen</h2>
            <StatusPill
              available={
                screenStatus.startsWith('Frame') ||
                screenStatus.startsWith('EFB') ||
                framePresenterMode === 'Native WebGL2 backend'
              }
            />
          </div>
          <div className="screen-stage">
            <canvas
              id="dolphin-webgl-canvas"
              ref={screenCanvas}
              width="640"
              height="480"
              onPointerDown={handleScreenPointerDown}
              onPointerMove={handleScreenPointerMove}
              onPointerUp={handleScreenPointerUp}
              onPointerLeave={handleScreenPointerLeave}
              onPointerCancel={handleScreenPointerCancel}
              onContextMenu={(event) => event.preventDefault()}
            />
          </div>
          <div className="screen-status">
            <span className="field-label">Frame bridge</span>
            <strong>{screenStatus}</strong>
            <span className="field-label">Render path</span>
            <strong>{framePresenterMode}</strong>
            <span className="field-label">Displayed FPS</span>
            <strong>{frameRateLabel(frameStats)}</strong>
            <span className="field-label">Core speed</span>
            <strong>{coreSpeedLabel(corePerfStats)}</strong>
            <span className="field-label">Wii Remote</span>
            <strong>{wiimotePointerLabel(wiimotePointer)}</strong>
          </div>
        </div>

        <div className="panel wide-panel">
          <div className="panel-heading">
            <h2>Lazy Reads</h2>
            <StatusPill available={samples.length > 0} />
          </div>
          <ReadSamples samples={samples} />
        </div>

        <div className="panel wide-panel">
          <div className="panel-heading">
            <h2>Browser APIs</h2>
            <StatusPill available={capabilities.some((capability) => capability.available)} />
          </div>
          <CapabilityList capabilities={capabilities} />
        </div>
      </section>
    </main>
  );
}
