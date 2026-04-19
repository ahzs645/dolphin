import type { DiscHeader, DiscWorkerRequest, MountedDisc } from '../lib/discTypes';

const GAMECUBE_DISC_MAGIC = 0xc2339f3d;
const WII_DISC_MAGIC = 0x5d1c9ea3;
const RVZ_MAGIC = 0x52565a01;
const WIA_MAGIC = 0x57494101;
const WBFS_MAGIC = 0x57424653;
const GCZ_MAGIC_LITTLE_ENDIAN = 0x01c00bb1;

let mountedFile: File | null = null;

function readSlice(offset: number, length: number): ArrayBuffer {
  if (!mountedFile) {
    throw new Error('No file is mounted');
  }

  if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(length) || offset < 0 || length < 0) {
    throw new Error('Invalid read range');
  }

  const end = Math.min(offset + length, mountedFile.size);
  const blob = mountedFile.slice(offset, end);

  if (typeof FileReaderSync === 'undefined') {
    throw new Error('FileReaderSync is unavailable in this worker');
  }

  return new FileReaderSync().readAsArrayBuffer(blob);
}

function readAscii(bytes: Uint8Array, offset: number, length: number): string {
  let value = '';

  for (let index = offset; index < offset + length && index < bytes.length; index += 1) {
    const byte = bytes[index];
    if (byte === 0) {
      break;
    }
    value += byte >= 32 && byte <= 126 ? String.fromCharCode(byte) : '.';
  }

  return value.trim();
}

function readU32BE(bytes: Uint8Array, offset: number): number {
  if (offset + 4 > bytes.length) {
    return 0;
  }

  return (
    ((bytes[offset] << 24) >>> 0) |
    (bytes[offset + 1] << 16) |
    (bytes[offset + 2] << 8) |
    bytes[offset + 3]
  );
}

function readU32LE(bytes: Uint8Array, offset: number): number {
  if (offset + 4 > bytes.length) {
    return 0;
  }

  return (
    bytes[offset] |
    (bytes[offset + 1] << 8) |
    (bytes[offset + 2] << 16) |
    ((bytes[offset + 3] << 24) >>> 0)
  );
}

function detectContainer(bytes: Uint8Array): string | null {
  const be = readU32BE(bytes, 0);
  const le = readU32LE(bytes, 0);

  if (be === RVZ_MAGIC) {
    return 'RVZ';
  }
  if (be === WIA_MAGIC) {
    return 'WIA';
  }
  if (be === WBFS_MAGIC) {
    return 'WBFS';
  }
  if (le === GCZ_MAGIC_LITTLE_ENDIAN) {
    return 'GCZ';
  }

  return null;
}

function readDiscHeader(): DiscHeader {
  const bytes = new Uint8Array(readSlice(0, 0x500));
  const magic = readU32BE(bytes, 0x18);
  const container = detectContainer(bytes);

  return {
    gameId: readAscii(bytes, 0, 6),
    makerCode: readAscii(bytes, 4, 2),
    discNumber: bytes[6] ?? 0,
    revision: bytes[7] ?? 0,
    title: readAscii(bytes, 0x20, 0x60),
    magicHex: `0x${magic.toString(16).toUpperCase().padStart(8, '0')}`,
    platform:
      magic === GAMECUBE_DISC_MAGIC
        ? 'gamecube'
        : magic === WII_DISC_MAGIC
          ? 'wii'
          : container
            ? 'container'
            : 'unknown',
    container
  };
}

function mountFile(file: File): MountedDisc {
  mountedFile = file;

  return {
    id: `${file.name}:${file.size}:${file.lastModified}`,
    name: file.name,
    size: file.size,
    type: file.type || 'application/octet-stream',
    lastModified: file.lastModified,
    header: readDiscHeader()
  };
}

self.addEventListener('message', (event: MessageEvent<DiscWorkerRequest>) => {
  try {
    if (event.data.type === 'mountFile') {
      const mounted = mountFile(event.data.file);
      self.postMessage({
        requestId: event.data.requestId,
        ok: true,
        type: 'mounted',
        payload: mounted
      });
      return;
    }

    const startedAt = performance.now();
    const buffer = readSlice(event.data.offset, event.data.length);
    const elapsedMs = performance.now() - startedAt;

    self.postMessage(
      {
        requestId: event.data.requestId,
        ok: true,
        type: 'readRange',
        payload: {
          offset: event.data.offset,
          length: buffer.byteLength,
          elapsedMs,
          buffer
        }
      },
      [buffer]
    );
  } catch (error) {
    self.postMessage({
      requestId: event.data.requestId,
      ok: false,
      error: error instanceof Error ? error.message : 'Unknown disc worker error'
    });
  }
});

