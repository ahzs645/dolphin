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
type FramePresenterMode = 'WebGL2 blit' | 'Canvas 2D';
type FramePresenter = {
  mode: FramePresenterMode;
  draw: (module: DolphinCoreModule, pointer: number, width: number, height: number, stride: number) => void;
};

const REAL_CORE_JS_URL = '/dolphin-core/dolphin-web-core.js';
const STUB_CORE_WASM_URL = '/dolphin-core/dolphin.wasm';
const CORE_STATE_LABELS = ['Uninitialized', 'Paused', 'Running', 'Stopping', 'Starting'];
const BOOT_FILE_DIR = '/dolphin-user/import';
const MAX_BOOT_COPY_BYTES = 256 * 1024 * 1024;
const WIIMOTE_MOUSE_SCALE = 10000;
const PRESENTATION_INTERVAL_MS = 1000 / 30;
const EFB_SNAPSHOT_INTERVAL_MS = 250;
const EMPTY_FRAME_STATS: FrameStats = { fps: 0, frames: 0, lastVersion: 0, sampleMs: 0 };
const EMPTY_CORE_PERF_STATS: CorePerfStats = { fps: 0, vps: 0, speed: 0, maxSpeed: 0 };

declare global {
  interface Window {
    __dolphinCoreFactory?: DolphinCoreFactory;
    __dolphinCoreFactoryPromise?: Promise<DolphinCoreFactory>;
    __dolphinCoreModule?: DolphinCoreModule;
    __dolphinFrameStats?: FrameStats;
    __dolphinPerfStats?: CorePerfStats;
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

function readCorePerfStats(module: DolphinCoreModule): CorePerfStats {
  return {
    fps: ccallNumber(module, 'dolphin_web_perf_fps'),
    vps: ccallNumber(module, 'dolphin_web_perf_vps'),
    speed: ccallNumber(module, 'dolphin_web_perf_speed'),
    maxSpeed: ccallNumber(module, 'dolphin_web_perf_max_speed')
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
    printErr: (message: string) => console.warn(`[dolphin] ${message}`)
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
  return Number(module.ccall(ident, 'number', [], []));
}

function ccallString(module: DolphinCoreModule, ident: string): string {
  const value = module.ccall(ident, 'string', [], []);
  return typeof value === 'string' ? value : '';
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

      gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
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
      if (canvas.width !== width || canvas.height !== height) {
        canvas.width = width;
        canvas.height = height;
        imageData = null;
      }

      imageData ??= new ImageData(width, height);
      copyRgbaRows(module.HEAPU8, pointer, width, height, stride, imageData.data);
      context.putImageData(imageData, 0, 0);
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
): { drawn: boolean; version: number; label: string } {
  if (ccallNumber(module, 'dolphin_web_has_frame') !== 1) {
    if (!allowEfbSnapshot) {
      return { drawn: false, version: previousVersion, label: 'Waiting for XFB frame' };
    }

    ccallNumber(module, 'dolphin_web_capture_efb');
    const efbWidth = ccallNumber(module, 'dolphin_web_efb_width');
    const efbHeight = ccallNumber(module, 'dolphin_web_efb_height');
    const efbStride = ccallNumber(module, 'dolphin_web_efb_stride');
    const efbPointer = ccallNumber(module, 'dolphin_web_efb_buffer');
    const label = `EFB snapshot ${efbWidth}x${efbHeight}`;

    if (efbWidth <= 0 || efbHeight <= 0 || efbStride < efbWidth * 4 || efbPointer <= 0) {
      return { drawn: false, version: previousVersion, label: 'Waiting for EFB pixels' };
    }

    presenter.draw(module, efbPointer, efbWidth, efbHeight, efbStride);
    return { drawn: true, version: previousVersion < 0 ? previousVersion : -1, label };
  }

  const version = ccallNumber(module, 'dolphin_web_frame_version');
  const width = ccallNumber(module, 'dolphin_web_frame_width');
  const height = ccallNumber(module, 'dolphin_web_frame_height');
  const stride = ccallNumber(module, 'dolphin_web_frame_stride');
  const pointer = ccallNumber(module, 'dolphin_web_frame_buffer');

  if (version === previousVersion || width <= 0 || height <= 0 || stride < width * 4 || pointer <= 0) {
    return { drawn: false, version, label: version > 0 ? `Frame ${version}` : 'Waiting for XFB frame' };
  }

  presenter.draw(module, pointer, width, height, stride);
  return { drawn: true, version, label: `Frame ${version} ${width}x${height}` };
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
  const screenStatusRef = useRef('Waiting for browser software renderer');
  const bootStateRef = useRef<BootState>('idle');
  const frameStatsWindow = useRef({ startedAt: performance.now(), frames: 0, lastVersion: 0 });
  const lastPresentationAt = useRef(0);
  const lastEfbSnapshotAt = useRef(0);
  const autoBootStarted = useRef(false);
  const [capabilities, setCapabilities] = useState<BrowserCapability[]>([]);
  const [mountedDisc, setMountedDisc] = useState<MountedDisc | null>(null);
  const [nandMount, setNandMount] = useState<NandMount | null>(null);
  const [samples, setSamples] = useState<ReadSample[]>([]);
  const [isBusy, setIsBusy] = useState(false);
  const [isMountingNand, setIsMountingNand] = useState(false);
  const [bootState, setBootState] = useState<BootState>('idle');
  const [bootMessage, setBootMessage] = useState('Waiting for Dolphin WASM core');
  const [screenStatus, setScreenStatus] = useState('Waiting for browser software renderer');
  const [frameStats, setFrameStats] = useState<FrameStats>(EMPTY_FRAME_STATS);
  const [corePerfStats, setCorePerfStats] = useState<CorePerfStats>(EMPTY_CORE_PERF_STATS);
  const [framePresenterMode, setFramePresenterMode] = useState<FramePresenterMode>('Canvas 2D');
  const [wiimotePointer, setWiimotePointer] = useState<WiimotePointerState>({
    x: 0.5,
    y: 0.5,
    buttons: 0,
    active: false
  });
  const [error, setError] = useState<string | null>(null);
  const [isDragging, setIsDragging] = useState(false);

  const runtimeStatus = useMemo(
    () => [
      {
        label: 'WASM core',
        value: bootState === 'success' ? 'Ready' : bootState === 'blocked' ? 'Failed' : bootState === 'booting' ? 'Booting' : 'Pending'
      },
      { label: 'CPU mode', value: 'Cached interpreter' },
      { label: 'Renderer', value: framePresenterMode },
      { label: 'FPS', value: frameRateLabel(frameStats) },
      { label: 'Core speed', value: coreSpeedLabel(corePerfStats) },
      { label: 'Wii Remote', value: wiimotePointerLabel(wiimotePointer) },
      { label: 'Disc IO', value: mountedDisc ? 'Mounted' : 'Idle' },
      { label: 'NAND', value: nandMount ? `${nandMount.fileCount} files` : 'Missing' },
      { label: 'Boot target', value: mountedDisc && isDirectBootFile(mountedDisc.name) ? mountedDisc.name : 'Wii Menu' }
    ],
    [bootState, corePerfStats, framePresenterMode, frameStats, mountedDisc, nandMount, wiimotePointer]
  );

  const canBootMountedFile = Boolean(mountedDisc && mountedFile.current && isDirectBootFile(mountedDisc.name));

  function updateScreenStatus(nextStatus: string): void {
    if (nextStatus === screenStatusRef.current) {
      return;
    }

    screenStatusRef.current = nextStatus;
    setScreenStatus(nextStatus);
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

    window.__dolphinFrameStats = nextStats;
    window.__dolphinPerfStats = nextPerfStats;
    setFrameStats(nextStats);
    setCorePerfStats(nextPerfStats);
    statsWindow.startedAt = now;
    statsWindow.frames = 0;
  }

  function resetFrameStats(): void {
    frameVersion.current = 0;
    frameStatsWindow.current = { startedAt: performance.now(), frames: 0, lastVersion: 0 };
    window.__dolphinFrameStats = EMPTY_FRAME_STATS;
    window.__dolphinPerfStats = EMPTY_CORE_PERF_STATS;
    setFrameStats(EMPTY_FRAME_STATS);
    setCorePerfStats(EMPTY_CORE_PERF_STATS);
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
      delete window.__dolphinFramePresenterMode;
    };
  }, []);

  useEffect(() => {
    bootStateRef.current = bootState;
  }, [bootState]);

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

      if (module && canvas && now - lastPresentationAt.current >= PRESENTATION_INTERVAL_MS) {
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

    const initResult = ccallNumber(module, 'dolphin_web_initialize');
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

    resetFrameStats();
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
    setError(null);
    setBootState('booting');
    setBootMessage('Looking for the real Dolphin web core');

    try {
      if (await hasRealCoreArtifact()) {
        setBootMessage('Loading dolphin-web-core.js');
        const module = await ensureDolphinCore();
        const version = ccallNumber(module, 'dolphin_web_core_version');

        resetFrameStats();
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

        {error ? <div className="error-banner">{error}</div> : null}
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

        <div className="panel wide-panel screen-panel">
          <div className="panel-heading">
            <h2>Screen</h2>
            <StatusPill available={screenStatus.startsWith('Frame') || screenStatus.startsWith('EFB')} />
          </div>
          <div className="screen-stage">
            <canvas
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
