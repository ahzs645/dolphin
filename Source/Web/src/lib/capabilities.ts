import type { BrowserCapability } from './discTypes';

declare global {
  interface Navigator {
    gpu?: unknown;
  }
}

function hasWebGL2(): boolean {
  const canvas = document.createElement('canvas');
  const context = canvas.getContext('webgl2');
  return context !== null;
}

export function readCapabilities(): BrowserCapability[] {
  const fileHandlePrototype =
    typeof FileSystemFileHandle === 'undefined' ? null : FileSystemFileHandle.prototype;

  return [
    {
      key: 'filePicker',
      label: 'File picker',
      available: 'showOpenFilePicker' in window,
      note: 'Chromium handle API'
    },
    {
      key: 'opfs',
      label: 'OPFS',
      available: Boolean(navigator.storage?.getDirectory),
      note: 'Writable user data'
    },
    {
      key: 'opfsSyncAccess',
      label: 'OPFS sync',
      available: Boolean(fileHandlePrototype && 'createSyncAccessHandle' in fileHandlePrototype),
      note: 'Worker persistence path'
    },
    {
      key: 'sharedArrayBuffer',
      label: 'Shared memory',
      available: typeof SharedArrayBuffer !== 'undefined',
      note: 'Required for pthreads'
    },
    {
      key: 'crossOriginIsolated',
      label: 'COOP/COEP',
      available: window.crossOriginIsolated,
      note: 'Threading headers'
    },
    {
      key: 'webgl2',
      label: 'WebGL2',
      available: hasWebGL2(),
      note: 'First renderer target'
    },
    {
      key: 'webgpu',
      label: 'WebGPU',
      available: 'gpu' in navigator,
      note: 'Future renderer target'
    },
    {
      key: 'gamepad',
      label: 'Gamepad',
      available: 'getGamepads' in navigator,
      note: 'Controller input'
    },
    {
      key: 'worker',
      label: 'Worker',
      available: typeof Worker !== 'undefined',
      note: 'Disc IO and runtime'
    }
  ];
}

