import type { DiscWorkerRequest, DiscWorkerResponse, MountedDisc } from './discTypes';

type PendingRequest = {
  resolve: (response: DiscWorkerResponse) => void;
  reject: (reason: Error) => void;
};

export class DiscWorkerClient {
  private nextRequestId = 1;
  private readonly pending = new Map<number, PendingRequest>();
  private readonly worker: Worker;

  constructor() {
    this.worker = new Worker(new URL('../workers/discReader.worker.ts', import.meta.url), {
      type: 'module'
    });

    this.worker.addEventListener('message', (event: MessageEvent<DiscWorkerResponse>) => {
      const pending = this.pending.get(event.data.requestId);
      if (!pending) {
        return;
      }

      this.pending.delete(event.data.requestId);
      pending.resolve(event.data);
    });

    this.worker.addEventListener('error', (event) => {
      const error = new Error(event.message || 'Disc worker failed');
      for (const pending of this.pending.values()) {
        pending.reject(error);
      }
      this.pending.clear();
    });
  }

  terminate(): void {
    this.worker.terminate();
    this.pending.clear();
  }

  async mountFile(file: File): Promise<MountedDisc> {
    const response = await this.request({
      requestId: 0,
      type: 'mountFile',
      file
    });

    if (!response.ok) {
      throw new Error(response.error);
    }

    if (response.type !== 'mounted') {
      throw new Error('Unexpected worker response while mounting file');
    }

    return response.payload;
  }

  async readRange(offset: number, length: number): Promise<Uint8Array> {
    const response = await this.request({
      requestId: 0,
      type: 'readRange',
      offset,
      length
    });

    if (!response.ok) {
      throw new Error(response.error);
    }

    if (response.type !== 'readRange') {
      throw new Error('Unexpected worker response while reading range');
    }

    return new Uint8Array(response.payload.buffer);
  }

  private request(message: DiscWorkerRequest): Promise<DiscWorkerResponse> {
    const requestId = this.nextRequestId++;
    const request = { ...message, requestId } as DiscWorkerRequest;

    return new Promise((resolve, reject) => {
      this.pending.set(requestId, { resolve, reject });
      this.worker.postMessage(request);
    });
  }
}

