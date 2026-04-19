import { copyFile, mkdir, readdir, stat } from 'node:fs/promises';
import path from 'node:path';
import process from 'node:process';

const buildDir = process.env.DOLPHIN_WASM_BUILD_DIR ?? '/tmp/dolphin-wasm-probe';
const sourceDir = path.join(buildDir, 'Binaries');
const targetDir = path.resolve('public/dolphin-core');

async function main() {
  const entries = await readdir(sourceDir);
  const artifacts = entries.filter(
    (entry) => entry === 'dolphin-web-core.js' || entry === 'dolphin-web-core.wasm' || entry.startsWith('dolphin-web-core.')
  );

  if (!artifacts.includes('dolphin-web-core.js') || !artifacts.includes('dolphin-web-core.wasm')) {
    throw new Error(
      `Missing dolphin-web-core artifacts in ${sourceDir}. Build target dolphin-web-core before syncing.`
    );
  }

  await mkdir(targetDir, { recursive: true });

  for (const artifact of artifacts) {
    const source = path.join(sourceDir, artifact);
    const info = await stat(source);
    if (!info.isFile()) {
      continue;
    }
    await copyFile(source, path.join(targetDir, artifact));
  }

  console.log(`Copied ${artifacts.length} Dolphin web core artifact(s) from ${sourceDir}`);
}

main().catch((error) => {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});
