export type DiscPlatform = 'gamecube' | 'wii' | 'container' | 'unknown';

export type CapabilityKey =
  | 'filePicker'
  | 'opfs'
  | 'opfsSyncAccess'
  | 'sharedArrayBuffer'
  | 'crossOriginIsolated'
  | 'webgl2'
  | 'webgpu'
  | 'gamepad'
  | 'worker';

export interface BrowserCapability {
  key: CapabilityKey;
  label: string;
  available: boolean;
  note: string;
}

export interface DiscHeader {
  gameId: string;
  makerCode: string;
  discNumber: number;
  revision: number;
  title: string;
  magicHex: string;
  platform: DiscPlatform;
  container: string | null;
}

export interface MountedDisc {
  id: string;
  name: string;
  size: number;
  type: string;
  lastModified: number;
  header: DiscHeader;
}

export interface ReadRange {
  label: string;
  offset: number;
  length: number;
}

export interface ReadSample extends ReadRange {
  elapsedMs: number;
  hex: string;
  ascii: string;
}

export interface MountFileRequest {
  requestId: number;
  type: 'mountFile';
  file: File;
}

export interface ReadRangeRequest {
  requestId: number;
  type: 'readRange';
  offset: number;
  length: number;
}

export type DiscWorkerRequest = MountFileRequest | ReadRangeRequest;

export type DiscWorkerResponse =
  | {
      requestId: number;
      ok: true;
      type: 'mounted';
      payload: MountedDisc;
    }
  | {
      requestId: number;
      ok: true;
      type: 'readRange';
      payload: {
        offset: number;
        length: number;
        elapsedMs: number;
        buffer: ArrayBuffer;
      };
    }
  | {
      requestId: number;
      ok: false;
      error: string;
    };

