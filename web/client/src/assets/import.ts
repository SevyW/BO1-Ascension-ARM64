// import.ts - copies the player's own Black Ops files into OPFS (bo1/game/...). Nothing is uploaded.
//
// 1. The player picks a folder: window.showDirectoryPicker() (Chromium) or <input type=file webkitdirectory>
//    (Firefox, Safari). Several picks add up (e.g. zone/ and main/ separately).
// 2. Each pick is matched against manifest.ts (root detection, case-insensitive); the page shows what was found.
// 3. run() streams every needed file into OPFS (copy.ts), skipping files already present with the same size
//    (resume), with overall and per-file progress and cancel; then writes bo1/import.json.
//
// Game file packages (web/shared/gamepack.ts, made by bo1-pack from the player's own install) are read in place: the
// tar index gives each file's offset, and the import copies Blob slices of the .tar. Each package is its own batch.

import { looksLikePackageName, readPackage, type PackageManifest, type ReadAt } from "../../../shared/gamepack";
import { MAPS } from "../../../shared/roster";
import { writeBlob, writeText, CopyCancelled } from "./copy";
import {
  classifyGamePath,
  computeAvailability,
  findLanguage,
  matchSelection,
  parseLocalizationLanguage,
  planImport,
  playableMaps,
  type Availability,
  type ImportPlan,
  type MatchedFile,
} from "./manifest";
import { DATA_INDEX, GAME_DIR, IMPORT_INDEX, listFiles, readJson, readText, removeEntry, requestPersist } from "./opfs";

export interface ImportIndex {
  files: Record<string, number>;
  maps: string[];
}

export interface ImportedState {
  index: ImportIndex;
  language: string | null;
  availability: Availability;
}

/** Reads bo1/import.json and localization.txt: what the player has imported. */
export async function loadImported(): Promise<ImportedState> {
  const raw = await readJson<Partial<ImportIndex>>(IMPORT_INDEX);
  const index: ImportIndex = {
    files: raw && typeof raw.files === "object" && raw.files ? raw.files : {},
    maps: Array.isArray(raw?.maps) ? raw.maps : [],
  };
  const locText = await readText(`${GAME_DIR}/localization.txt`);
  const language = locText ? parseLocalizationLanguage(locText) : null;
  const availability = computeAvailability(
    Object.entries(index.files).map(([path, size]) => ({ path, size })),
    language,
  );
  return { index, language, availability };
}

async function existingGameFiles(): Promise<Map<string, number>> {
  const m = new Map<string, number>();
  for (const f of await listFiles(GAME_DIR)) m.set(f.path.toLowerCase(), f.size);
  return m;
}

/** Rebuilds bo1/import.json from what is actually in OPFS (after an import, or from the files view). */
export async function rebuildIndex(): Promise<ImportedState> {
  const files: Record<string, number> = {};
  for (const f of await listFiles(GAME_DIR)) {
    const info = classifyGamePath(f.path, { includeVideo: true });
    if (info) files[f.path] = f.size;
  }
  const locText = await readText(`${GAME_DIR}/localization.txt`);
  const language = locText ? parseLocalizationLanguage(locText) : null;
  const availability = computeAvailability(
    Object.entries(files).map(([path, size]) => ({ path, size })),
    language,
  );
  const index: ImportIndex = { files, maps: playableMaps(availability) };
  await writeText(IMPORT_INDEX, JSON.stringify(index, null, 1));
  return { index, language, availability };
}

/** Bytes per map of the imported map zones (zone/Common/<map>(_patch).ff and the localized copies). */
export function importedMapBytes(index: ImportIndex): Record<string, number> {
  const out: Record<string, number> = {};
  for (const [path, size] of Object.entries(index.files)) {
    const info = classifyGamePath(path);
    if (info?.kind === "map-zone" && info.map) out[info.map] = (out[info.map] ?? 0) + size;
  }
  return out;
}

/** Removes one map's zones from OPFS and rebuilds the index; the base files and the other maps stay. */
export async function removeImportedMap(mapId: string): Promise<ImportedState> {
  for (const f of await listFiles(GAME_DIR)) {
    const info = classifyGamePath(f.path);
    if (info?.kind === "map-zone" && info.map === mapId) await removeEntry(`${GAME_DIR}/${f.path}`);
  }
  return rebuildIndex();
}

/** Removes the imported files, the synced data and both indexes (bo1/home, the engine's settings, stays). */
export async function deleteAllImported(): Promise<void> {
  await removeEntry(GAME_DIR);
  await removeEntry(IMPORT_INDEX);
  await removeEntry(DATA_INDEX);
}

// ----- picking -----

export interface SourceFile {
  path: string; // relative to the selection, starting with the picked folder's name (packages: the game root)
  size: number;
  file: () => Promise<Blob>; // a File, or a slice of a package
}

const RELEVANT = /\.(ff|iwd|bik)$/i;
const SKIP_DIRS = new Set(["$recycle.bin", "system volume information", ".git", "node_modules", "redist"]);
const MAX_DEPTH = 8;

function relevant(name: string): boolean {
  return RELEVANT.test(name) || name.toLowerCase() === "localization.txt" || looksLikePackageName(name);
}

export function supportsDirectoryPicker(): boolean {
  return typeof (window as unknown as { showDirectoryPicker?: unknown }).showDirectoryPicker === "function";
}

interface IterableDir {
  name: string;
  entries(): AsyncIterable<[string, FileSystemHandle]>;
}

/** Opens the directory picker and lists the relevant files. Resolves null when the user cancels. */
export async function pickWithDirectoryPicker(onScan?: (filesSeen: number, dirsSeen: number) => void): Promise<SourceFile[] | null> {
  const picker = (window as unknown as { showDirectoryPicker: (o?: object) => Promise<FileSystemDirectoryHandle> })
    .showDirectoryPicker;
  let root: FileSystemDirectoryHandle;
  try {
    root = await picker.call(window, { id: "bo1-install", mode: "read" });
  } catch (e) {
    if (e instanceof DOMException && e.name === "AbortError") return null;
    throw e;
  }
  const out: SourceFile[] = [];
  let files = 0;
  let dirs = 0;
  const walk = async (dir: IterableDir, prefix: string, depth: number): Promise<void> => {
    dirs++;
    for await (const [name, handle] of dir.entries()) {
      if (handle.kind === "directory") {
        if (depth < MAX_DEPTH && !SKIP_DIRS.has(name.toLowerCase())) {
          await walk(handle as unknown as IterableDir, `${prefix}${name}/`, depth + 1);
        }
      } else {
        files++;
        if (relevant(name)) {
          const fh = handle as FileSystemFileHandle;
          const f = await fh.getFile();
          out.push({ path: `${prefix}${name}`, size: f.size, file: () => fh.getFile() });
        }
      }
      if ((files + dirs) % 200 === 0) onScan?.(files, dirs);
    }
  };
  await walk(root as unknown as IterableDir, `${root.name}/`, 0);
  onScan?.(files, dirs);
  return out;
}

/** The <input type=file webkitdirectory> fallback. */
export function sourcesFromFileList(list: FileList | File[]): SourceFile[] {
  const out: SourceFile[] = [];
  for (const f of Array.from(list)) {
    if (!relevant(f.name)) continue;
    out.push({ path: f.webkitRelativePath || f.name, size: f.size, file: async () => f });
  }
  return out;
}

// ----- packages -----

export interface OpenedPackage {
  name: string; // the .tar's file name
  manifest: PackageManifest;
  bytes: number; // the whole archive
  sources: SourceFile[]; // manifest paths (from the game root), each a slice of the archive
}

export interface PackageFailure {
  name: string;
  message: string;
}

/** Reads a package's tar index and manifest (one block per entry); the data stays in the Blob until the import. */
export async function openPackage(blob: Blob, name = (blob as Partial<File>).name ?? "package.tar"): Promise<OpenedPackage> {
  const read: ReadAt = async (offset, length) => new Uint8Array(await blob.slice(offset, offset + length).arrayBuffer());
  const pkg = await readPackage(read, blob.size);
  return {
    name,
    manifest: pkg.manifest,
    bytes: blob.size,
    sources: pkg.files.map((f) => ({ path: f.path, size: f.size, file: async () => blob.slice(f.offset, f.offset + f.size) })),
  };
}

function baseName(path: string): string {
  return path.split("/").pop() || path;
}

// ----- the import session -----

export interface ImportProgress {
  filesTotal: number;
  filesDone: number;
  bytesTotal: number;
  bytesDone: number;
  current: string | null;
  currentBytes: number;
  currentSize: number;
  bytesPerSec: number;
}

export interface ImportOutcome {
  copied: number;
  skipped: number;
  cancelled: boolean;
  error: string | null;
  state: ImportedState;
}

interface Selected extends MatchedFile {
  src: SourceFile;
}

/** A map found in the picks: its zones' bytes, and what is still to copy. */
export interface MapChoice {
  id: string;
  bytes: number;
  copyBytes: number; // 0 when the map is imported already
  selected: boolean;
  missing: string[]; // the map's required files that are neither picked nor imported (it would not be playable)
}

export class ImportSession {
  private batches: { files: SourceFile[]; pkg: OpenedPackage | null }[] = [];
  includeVideo = false;
  existing = new Map<string, number>();
  ignored = 0;
  ignoredPaths: string[] = []; // of the folder picks: what the scan shows under "Files not imported"
  roots: (string | null)[] = []; // per folder pick (packages hold paths from the game root)
  packages: OpenedPackage[] = [];
  packageErrors: PackageFailure[] = []; // .tar files of the last add that could not be opened as packages
  excludedMaps = new Set<string>(); // maps the player unchecked: their zones are not copied

  async init(): Promise<void> {
    this.existing = await existingGameFiles();
  }

  get empty(): boolean {
    return this.batches.length === 0;
  }

  add(files: SourceFile[]): void {
    this.batches.push({ files, pkg: null });
  }

  /** A folder pick: loose files are one batch, every .tar in it is opened as a package (its own batch). */
  async addPicked(files: SourceFile[]): Promise<void> {
    const tars = files.filter((f) => looksLikePackageName(f.path));
    const loose = files.filter((f) => !looksLikePackageName(f.path));
    if (loose.length > 0 || tars.length === 0) this.add(loose);
    await this.addPackages(tars);
  }

  /**
   * Opens each file as a package and adds it as its own batch (the same name and size twice is added once). Files
   * that are not packages land in packageErrors and do not stop the others. Returns how many packages were added.
   */
  async addPackages(files: SourceFile[]): Promise<number> {
    this.packageErrors = [];
    let added = 0;
    for (const f of files) {
      const name = baseName(f.path);
      try {
        const pkg = await openPackage(await f.file(), name);
        if (this.packages.some((p) => p.name === pkg.name && p.bytes === pkg.bytes)) continue;
        this.packages.push(pkg);
        this.batches.push({ files: pkg.sources, pkg });
        added++;
      } catch (e) {
        this.packageErrors.push({ name, message: e instanceof Error ? e.message : String(e) });
      }
    }
    return added;
  }

  /** Packages made from an install in another language than `language` (the picked or imported localization.txt). */
  languageConflicts(language: string | null): OpenedPackage[] {
    if (!language) return [];
    const want = findLanguage(language)?.name ?? language;
    return this.packages.filter((p) => (findLanguage(p.manifest.language)?.name ?? p.manifest.language) !== want);
  }

  clear(): void {
    this.batches = [];
    this.packages = [];
    this.packageErrors = [];
    this.excludedMaps.clear();
  }

  /** Matched files of every pick, without the maps the player unchecked. */
  selected(): Selected[] {
    return this.matched().filter((f) => !(f.info.kind === "map-zone" && f.info.map && this.excludedMaps.has(f.info.map)));
  }

  /** The maps in the picks, in roster order, with their sizes and whether they are checked. */
  mapChoices(language: string | null = null): MapChoice[] {
    const matched = this.matched();
    const av = this.projected(language, matched);
    const byMap = new Map<string, MapChoice>();
    for (const f of matched) {
      if (f.info.kind !== "map-zone" || !f.info.map) continue;
      const c = byMap.get(f.info.map) ?? {
        id: f.info.map,
        bytes: 0,
        copyBytes: 0,
        selected: !this.excludedMaps.has(f.info.map),
        missing: av.maps[f.info.map]?.missing ?? [],
      };
      c.bytes += f.size;
      if (this.existing.get(f.dest.toLowerCase()) !== f.size) c.copyBytes += f.size;
      byMap.set(f.info.map, c);
    }
    return MAPS.filter((m) => byMap.has(m.id)).map((m) => byMap.get(m.id)!);
  }

  setMapSelected(mapId: string, on: boolean): void {
    if (on) this.excludedMaps.delete(mapId);
    else this.excludedMaps.add(mapId);
  }

  /** Matched files of every pick (each pick has its own root), de-duplicated by destination. */
  private matched(): Selected[] {
    const seen = new Set<string>();
    const out: Selected[] = [];
    this.ignored = 0;
    this.ignoredPaths = [];
    this.roots = [];
    for (const { files: batch, pkg } of this.batches) {
      const bySource = new Map(batch.map((s) => [s.path, s]));
      const m = matchSelection(batch, { includeVideo: this.includeVideo });
      if (!pkg) this.roots.push(m.root);
      this.ignored += m.ignored;
      if (!pkg) this.ignoredPaths.push(...m.ignoredPaths);
      for (const f of m.matched) {
        const key = f.dest.toLowerCase();
        const src = bySource.get(f.source);
        if (seen.has(key) || !src) continue;
        seen.add(key);
        out.push({ ...f, src });
      }
    }
    return out;
  }

  plan(): ImportPlan & { selected: Selected[] } {
    const selected = this.selected();
    return { ...planImport(selected, this.existing), selected };
  }

  /** Availability once the selection is imported (what is already imported plus what was picked). */
  projected(language: string | null, selected = this.selected()): Availability {
    const all = new Map<string, { path: string; size: number }>();
    for (const [path, size] of this.existing) all.set(path, { path, size });
    for (const f of selected) all.set(f.dest.toLowerCase(), { path: f.dest, size: f.size });
    return computeAvailability([...all.values()], language);
  }

  /** The language of the picked localization.txt (if one was picked), else null. */
  async pickedLanguage(selected = this.selected()): Promise<string | null> {
    const loc = selected.find((f) => f.info.kind === "localization");
    if (!loc) return null;
    try {
      return parseLocalizationLanguage(await (await loc.src.file()).slice(0, 256).text());
    } catch {
      return null;
    }
  }

  async run(onProgress: (p: ImportProgress) => void, signal: AbortSignal): Promise<ImportOutcome> {
    await requestPersist();
    const plan = this.plan();
    const progress: ImportProgress = {
      filesTotal: plan.copy.length,
      filesDone: 0,
      bytesTotal: plan.copyBytes,
      bytesDone: 0,
      current: null,
      currentBytes: 0,
      currentSize: 0,
      bytesPerSec: 0,
    };
    const started = performance.now();
    let error: string | null = null;
    let cancelled = false;
    let copied = 0;
    const emit = () => {
      const secs = (performance.now() - started) / 1000;
      progress.bytesPerSec = secs > 0.5 ? (progress.bytesDone + progress.currentBytes) / secs : 0;
      onProgress({ ...progress });
    };
    // biggest files last so the small zones (and a usable core) land first
    const order = [...(plan.copy as Selected[])].sort((a, b) => a.size - b.size);
    for (const f of order) {
      if (signal.aborted) {
        cancelled = true;
        break;
      }
      progress.current = f.dest;
      progress.currentBytes = 0;
      progress.currentSize = f.size;
      emit();
      try {
        const file = await f.src.file();
        const written = await writeBlob(
          `${GAME_DIR}/${f.dest}`,
          file,
          (n) => {
            progress.currentBytes = n;
            emit();
          },
          signal,
        );
        if (written !== f.size) throw new Error(`${f.dest}: wrote ${written} of ${f.size} bytes`);
        this.existing.set(f.dest.toLowerCase(), f.size);
        copied++;
        progress.filesDone++;
        progress.bytesDone += f.size;
        progress.currentBytes = 0;
        emit();
      } catch (e) {
        if (e instanceof CopyCancelled || signal.aborted) {
          cancelled = true;
        } else {
          error = e instanceof Error ? e.message : String(e);
          if (/quota|space|full/i.test(error)) error = `Not enough storage space for ${f.dest}. ${error}`;
        }
        break;
      }
    }
    progress.current = null;
    emit();
    const state = await rebuildIndex();
    return { copied, skipped: plan.skip.length, cancelled, error, state };
  }
}

export function mapNames(ids: string[]): string {
  return ids.map((id) => MAPS.find((m) => m.id === id)?.name ?? id).join(", ");
}
