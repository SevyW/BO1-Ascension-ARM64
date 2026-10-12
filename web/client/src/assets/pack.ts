// pack.ts - makes game file packages in the page: the same packages bo1-pack writes (web/shared/gamepack.ts), from
// the player's own install, for importing in another browser or on another computer. Nothing is uploaded.
//
// 1. The player picks the install (the importer's pickers); packer/plan.ts plans the packages.
// 2. Every file of a package is hashed (sha256, read once, in chunks; shared/sha256.ts).
// 3. The package is assembled as a Blob of tar headers, padding and the picked Files themselves, so nothing is copied
//    into memory. It is then either written into a folder the player picks (Chromium: showDirectoryPicker +
//    createWritable, in 16 MB slices), or offered as a download link (every other browser).

import { CHECKSUMS_NAME, encodeManifest, encodeTarHeader, formatChecksums, MANIFEST_NAME, PACKER_VERSION, TAR_TRAILER, tarPadding, type PackageManifest } from "../../../shared/gamepack";
import { Sha256 } from "../../../shared/sha256";
import { archiveBytes, localizationSource, manifestFor, planPackages, type PackagePlan, type PlannedFile, type PlannedPackage } from "../../../packer/plan";
import { openPackage, type SourceFile } from "./import";

export const PACK_TOOL = `bo1-pack ${PACKER_VERSION} (web)`;
const WRITE_SLICE = 16 << 20;

export class PackCancelled extends Error {
  constructor() {
    super("cancelled");
  }
}

/** Plans the packages for a picked install; reads its localization.txt for the language. */
export async function planFromSources(files: SourceFile[], language: string | null = null): Promise<{ plan: PackagePlan; localizationText: string | null }> {
  let localizationText: string | null = null;
  const loc = localizationSource(planPackages(files, { language }));
  const src = loc ? files.find((f) => f.path === loc) : undefined;
  if (src) {
    try {
      localizationText = await (await src.file()).slice(0, 4096).text();
    } catch {
      /* unreadable: the planner falls back to the localized files found */
    }
  }
  return { plan: planPackages(files, { language, localizationText }), localizationText };
}

/** Chromium: packages can be written straight into a folder the player picks. Elsewhere they are downloads. */
export function supportsFolderOutput(): boolean {
  if (typeof window === "undefined") return false;
  const w = window as unknown as { showDirectoryPicker?: unknown; FileSystemFileHandle?: { prototype: { createWritable?: unknown } } };
  return typeof w.showDirectoryPicker === "function" && typeof w.FileSystemFileHandle?.prototype.createWritable === "function";
}

/** Asks for the folder to write the packages into. Resolves null when the player cancels. */
export async function pickOutputFolder(): Promise<FileSystemDirectoryHandle | null> {
  const picker = (window as unknown as { showDirectoryPicker: (o?: object) => Promise<FileSystemDirectoryHandle> }).showDirectoryPicker;
  try {
    return await picker.call(window, { id: "bo1-packages", mode: "readwrite", startIn: "downloads" });
  } catch (e) {
    if (e instanceof DOMException && e.name === "AbortError") return null;
    throw e;
  }
}

/** sha256 of a Blob, read in the browser's chunks; onBytes gets the bytes read so far. */
export async function hashBlob(blob: Blob, onBytes?: (n: number) => void, signal?: AbortSignal): Promise<string> {
  const hash = new Sha256();
  const reader = blob.stream().getReader();
  let n = 0;
  try {
    for (;;) {
      if (signal?.aborted) throw new PackCancelled();
      const { done, value } = await reader.read();
      if (done) break;
      hash.update(value);
      n += value.byteLength;
      onBytes?.(n);
    }
  } catch (e) {
    await reader.cancel().catch(() => undefined);
    throw e;
  }
  if (n !== blob.size) throw new Error(`read ${n} of ${blob.size} bytes`);
  return hash.hex();
}

function seconds(ms: number): number {
  return Math.floor(ms / 1000);
}

/**
 * The package as a Blob: the manifest, every game file (the source Blob itself, not a copy) and the checksums,
 * each behind its tar header, then the trailer. `blobs` and `sums` are keyed by package path.
 */
export function packageBlob(manifest: PackageManifest, blobs: Map<string, Blob>, sums: Map<string, string>, mtime = Date.now()): Blob {
  const parts: BlobPart[] = [];
  const entry = (path: string, data: Blob | Uint8Array<ArrayBuffer>, size: number, ms: number) => {
    parts.push(encodeTarHeader({ path, size, mtime: seconds(ms) }), data, new Uint8Array(tarPadding(size)));
  };
  const head = encodeManifest(manifest);
  entry(MANIFEST_NAME, head, head.length, mtime);
  const list: { path: string; sha256: string }[] = [];
  for (const f of manifest.files) {
    const blob = blobs.get(f.path);
    const sha256 = sums.get(f.path);
    if (!blob || !sha256) throw new Error(`${f.path} is missing`);
    if (blob.size !== f.size) throw new Error(`${f.path} has ${blob.size} bytes, expected ${f.size}`);
    entry(f.path, blob, f.size, (blob as Partial<File>).lastModified ?? mtime);
    list.push({ path: f.path, sha256 });
  }
  const sumsText = formatChecksums(list);
  entry(CHECKSUMS_NAME, sumsText, sumsText.length, mtime);
  parts.push(new Uint8Array(TAR_TRAILER));
  return new Blob(parts);
}

/** True when `dir` already holds this package with the same files, sizes and language (like bo1-pack). */
export async function packageUpToDate(dir: FileSystemDirectoryHandle, p: PlannedPackage): Promise<boolean> {
  try {
    const file = await (await dir.getFileHandle(p.fileName)).getFile();
    const pkg = await openPackage(file, p.fileName);
    const m = pkg.manifest;
    if (m.id !== p.id || m.language !== p.language || m.files.length !== p.files.length) return false;
    return m.files.every((f, i) => f.path === p.files[i]!.path && f.size === p.files[i]!.size);
  } catch {
    return false;
  }
}

/**
 * Writes `blob` to dir/name. Chromium writes into a temporary file and replaces the target on close(), so a failed
 * or cancelled write leaves an older package as it was; a new name is removed again.
 */
export async function writeToFolder(dir: FileSystemDirectoryHandle, name: string, blob: Blob, onBytes?: (n: number) => void, signal?: AbortSignal): Promise<void> {
  const existed = await dir.getFileHandle(name).then(
    () => true,
    () => false,
  );
  const fh = await dir.getFileHandle(name, { create: true });
  const writable = await fh.createWritable({ keepExistingData: false });
  try {
    for (let off = 0; off < blob.size; off += WRITE_SLICE) {
      if (signal?.aborted) throw new PackCancelled();
      await writable.write(blob.slice(off, Math.min(off + WRITE_SLICE, blob.size)));
      onBytes?.(Math.min(off + WRITE_SLICE, blob.size));
    }
    await writable.close();
  } catch (e) {
    await writable.abort().catch(() => undefined);
    if (!existed) await dir.removeEntry(name).catch(() => undefined);
    throw e;
  }
}

// ----- the job -----

export type PackTarget = { kind: "folder"; dir: FileSystemDirectoryHandle } | { kind: "download" };

export interface PackProgress {
  phase: "checking" | "hashing" | "writing";
  pkg: string | null; // the package's file name
  pkgIndex: number; // 0-based
  pkgCount: number;
  current: string | null; // the game file being hashed
  bytesTotal: number;
  bytesDone: number;
  bytesPerSec: number;
}

export interface PackResult {
  id: string;
  fileName: string;
  bytes: number;
  status: "written" | "up-to-date" | "ready";
  blob?: Blob; // "ready": the package to download
}

export interface PackOutcome {
  results: PackResult[];
  cancelled: boolean;
  error: string | null;
}

/**
 * Makes `packages` from the picked `files` (picked path -> SourceFile, as planned). Folder: packages already up to
 * date are skipped, the rest are hashed and written. Download: every package is hashed and returned as a Blob.
 */
export async function makePackages(
  packages: PlannedPackage[],
  files: Map<string, SourceFile>,
  target: PackTarget,
  onProgress: (p: PackProgress) => void,
  signal: AbortSignal,
): Promise<PackOutcome> {
  const results: PackResult[] = [];
  const progress: PackProgress = { phase: "checking", pkg: null, pkgIndex: 0, pkgCount: packages.length, current: null, bytesTotal: 0, bytesDone: 0, bytesPerSec: 0 };
  let started = performance.now();
  let lastEmit = 0;
  let base = 0; // bytes done before the current step
  const emit = (step: number, force = false) => {
    progress.bytesDone = base + step;
    const now = performance.now();
    if (!force && now - lastEmit < 100) return;
    lastEmit = now;
    const secs = (now - started) / 1000;
    progress.bytesPerSec = secs > 0.5 ? progress.bytesDone / secs : 0;
    onProgress({ ...progress });
  };
  const todo: PlannedPackage[] = [];
  try {
    if (target.kind === "folder") {
      emit(0, true);
      for (const p of packages) {
        if (signal.aborted) throw new PackCancelled();
        if (await packageUpToDate(target.dir, p)) results.push({ id: p.id, fileName: p.fileName, bytes: archiveBytes(p, manifestFor(p, PACK_TOOL)), status: "up-to-date" });
        else todo.push(p);
      }
    } else {
      todo.push(...packages);
    }
    // folder: every byte is read once to hash it and once to write it; download: hashing is the whole job here
    progress.bytesTotal = todo.reduce((n, p) => n + p.dataBytes + (target.kind === "folder" ? archiveBytes(p, manifestFor(p, PACK_TOOL)) : 0), 0);
    progress.pkgCount = todo.length;
    started = performance.now();
    for (const [i, p] of todo.entries()) {
      progress.pkg = p.fileName;
      progress.pkgIndex = i;
      progress.phase = "hashing";
      const blobs = new Map<string, Blob>();
      const sums = new Map<string, string>();
      for (const f of p.files) {
        if (signal.aborted) throw new PackCancelled();
        const blob = await sourceBlob(files, f);
        progress.current = f.path;
        emit(0, true);
        sums.set(f.path, await hashBlob(blob, (n) => emit(n), signal));
        blobs.set(f.path, blob);
        base += f.size;
      }
      progress.current = null;
      const manifest = manifestFor(p, PACK_TOOL);
      const pkg = packageBlob(manifest, blobs, sums);
      const want = archiveBytes(p, manifest);
      if (pkg.size !== want) throw new Error(`${p.fileName}: assembled ${pkg.size} bytes, expected ${want}`);
      if (target.kind === "folder") {
        progress.phase = "writing";
        emit(0, true);
        await writeToFolder(target.dir, p.fileName, pkg, (n) => emit(n), signal);
        base += pkg.size;
        results.push({ id: p.id, fileName: p.fileName, bytes: pkg.size, status: "written" });
      } else {
        results.push({ id: p.id, fileName: p.fileName, bytes: pkg.size, status: "ready", blob: pkg });
      }
      emit(0, true);
    }
    return { results, cancelled: false, error: null };
  } catch (e) {
    if (e instanceof PackCancelled || signal.aborted) return { results, cancelled: true, error: null };
    let error = e instanceof Error ? e.message : String(e);
    if (e instanceof DOMException && e.name === "NotReadableError") error = `a file of the install could not be read (changed or moved since you picked it?); pick the folder again. ${error}`;
    else if (/quota|space|full/i.test(error)) error = `not enough disk space in the folder you picked. ${error}`;
    return { results, cancelled: false, error: progress.pkg ? `${progress.pkg}: ${error}` : error };
  }
}

async function sourceBlob(files: Map<string, SourceFile>, f: PlannedFile): Promise<Blob> {
  const src = files.get(f.source);
  if (!src) throw new Error(`${f.source} is no longer in the selection`);
  const blob = await src.file();
  if (blob.size !== f.size) throw new Error(`${f.source} changed size since you picked it (${blob.size} bytes, was ${f.size}); pick the folder again`);
  return blob;
}
