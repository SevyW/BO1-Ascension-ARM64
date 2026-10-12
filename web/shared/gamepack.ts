// gamepack.ts - game file packages: the player turns their own Black Ops install into one package with the files
// every Zombies map needs plus one package per map (web/packer, `bo1-pack`), then drops the packages into the page's
// importer (client/src/assets/import.ts). Packages stay on the player's machine; the server never sees them.
//
// Pure code (no fs, no DOM), shared by the packer and the page: names, the manifest, tar headers, and reading a
// package's index through a read callback (Blob.slice in the browser, a file handle in Node/Bun).
//
// A package is a plain POSIX ustar archive, uncompressed (fastfiles and IWDs are compressed already):
//   bo1-package.json       the manifest (PackageManifest), always the first entry
//   <game files>           paths relative to the game root with the retail directory names, exactly as the importer
//                          stores them: localization.txt, zone/Common/<zone>.ff, zone/<Language>/<prefix><zone>.ff,
//                          main/iw_*.iwd, main/localized_<language>_iwd*.iwd
//   bo1-package.sha256     "<sha256>  <path>" per game file (sha256sum -c format), always the last entry
// Extracting one with any tar tool gives a partial install that the folder importer accepts as well.

export const PACKAGE_FORMAT = "bo1-zombies-package";
export const PACKAGE_VERSION = 1;
export const MANIFEST_NAME = "bo1-package.json";
export const CHECKSUMS_NAME = "bo1-package.sha256";
export const PACKAGE_EXTENSION = ".tar";
export const BASE_PACKAGE_ID = "base";
/** The packer as served by the site: <BASE_PATH>tools/bo1-pack.mjs (web/scripts/build.ts). */
export const PACKER_PATH = "tools/bo1-pack.mjs";
export const PACKER_VERSION = 1;

const MAX_MANIFEST_BYTES = 1 << 20;
const MAX_ENTRIES = 4096;

export class PackageError extends Error {
  override name = "PackageError";
}

export type PackageKind = "base" | "map";

export interface PackageFile {
  path: string; // relative to the game root, "/" separators, retail names
  size: number;
}

export interface PackageManifest {
  format: typeof PACKAGE_FORMAT;
  version: number;
  id: string; // BASE_PACKAGE_ID or the map id (web/shared/roster.ts MAPS)
  kind: PackageKind;
  map: string | null; // the map id for kind "map", null for the base package
  language: string; // the install's language (localization.txt), which the localized files belong to: "english"
  created: string; // ISO 8601
  tool: string; // "bo1-pack 1"
  files: PackageFile[]; // in archive order
}

/** bo1-zombies-base-english.tar, bo1-zombies-zombie_theater-english.tar */
export function packageFileName(id: string, language: string): string {
  return `bo1-zombies-${id}-${language}${PACKAGE_EXTENSION}`;
}

export function looksLikePackageName(name: string): boolean {
  return name.toLowerCase().endsWith(PACKAGE_EXTENSION);
}

/** A relative path a package may contain: "/" separators, no "." or ".." segments, printable ASCII. */
export function isSafePackagePath(p: string): boolean {
  if (p.length === 0 || p.length > 255 || p.startsWith("/") || p.includes("\\")) return false;
  if (!/^[\x21-\x7e][\x20-\x7e]*$/.test(p)) return false;
  return p.split("/").every((s) => s.length > 0 && s !== "." && s !== ".." && s.trim() === s);
}

function fail(msg: string): never {
  throw new PackageError(msg);
}

/** Checks a parsed manifest; throws PackageError naming the first problem. */
export function validateManifest(raw: unknown): PackageManifest {
  if (!raw || typeof raw !== "object") fail("the manifest is not a JSON object");
  const m = raw as Record<string, unknown>;
  if (m.format !== PACKAGE_FORMAT) fail("not a BO1 Zombies package (unknown format)");
  if (typeof m.version !== "number" || !Number.isInteger(m.version)) fail("the manifest has no version");
  if (m.version > PACKAGE_VERSION) fail(`package version ${m.version} is newer than this page reads (${PACKAGE_VERSION}); reload the page`);
  if (typeof m.id !== "string" || !/^[a-z0-9_]{1,64}$/.test(m.id)) fail("the manifest has a bad id");
  if (m.kind !== "base" && m.kind !== "map") fail("the manifest has a bad kind");
  if (m.kind === "base" && (m.id !== BASE_PACKAGE_ID || m.map !== null)) fail("a base package must have id \"base\" and no map");
  if (m.kind === "map" && m.map !== m.id) fail("a map package's id must be its map");
  if (typeof m.language !== "string" || !/^[a-z]{2,16}$/.test(m.language)) fail("the manifest has a bad language");
  if (typeof m.created !== "string" || typeof m.tool !== "string") fail("the manifest lacks created/tool");
  if (!Array.isArray(m.files) || m.files.length === 0) fail("the manifest lists no files");
  const seen = new Set<string>();
  for (const f of m.files as unknown[]) {
    const e = f as Record<string, unknown> | null;
    if (!e || typeof e.path !== "string" || !isSafePackagePath(e.path)) fail("the manifest lists a bad path");
    if (typeof e.size !== "number" || !Number.isSafeInteger(e.size) || e.size < 0) fail(`bad size for ${e.path}`);
    const key = e.path.toLowerCase();
    if (key === MANIFEST_NAME || key === CHECKSUMS_NAME) fail(`the manifest lists ${e.path}`);
    if (seen.has(key)) fail(`the manifest lists ${e.path} twice`);
    seen.add(key);
  }
  return {
    format: PACKAGE_FORMAT,
    version: m.version,
    id: m.id,
    kind: m.kind,
    map: m.kind === "map" ? (m.map as string) : null,
    language: m.language,
    created: m.created,
    tool: m.tool,
    files: (m.files as PackageFile[]).map((f) => ({ path: f.path, size: f.size })),
  };
}

export function encodeManifest(m: PackageManifest): Uint8Array<ArrayBuffer> {
  return new TextEncoder().encode(`${JSON.stringify(m, null, 1)}\n`);
}

/** sha256sum format: "<hex>  <path>\n" per line. */
export function formatChecksums(sums: { path: string; sha256: string }[]): Uint8Array<ArrayBuffer> {
  return new TextEncoder().encode(sums.map((s) => `${s.sha256}  ${s.path}\n`).join(""));
}

export function parseChecksums(text: string): Map<string, string> {
  const out = new Map<string, string>();
  for (const line of text.split(/\r?\n/)) {
    const m = /^([0-9a-f]{64}) [ *](.+)$/.exec(line);
    if (m && m[1] && m[2]) out.set(m[2], m[1]);
  }
  return out;
}

// ----- tar (POSIX ustar) -----

export const TAR_BLOCK = 512;
/** Two zero blocks end an archive. */
export const TAR_TRAILER = 2 * TAR_BLOCK;
/** ustar's 11 octal digits. */
export const TAR_MAX_SIZE = 8 ** 11 - 1;

export function tarPadding(size: number): number {
  return (TAR_BLOCK - (size % TAR_BLOCK)) % TAR_BLOCK;
}

/** Bytes one entry takes in the archive: header, data, padding. */
export function tarEntryBytes(size: number): number {
  return TAR_BLOCK + size + tarPadding(size);
}

/** The archive size for entries of these sizes (headers, data, padding, trailer). */
export function tarArchiveBytes(sizes: number[]): number {
  return sizes.reduce((n, s) => n + tarEntryBytes(s), 0) + TAR_TRAILER;
}

function putString(block: Uint8Array, at: number, len: number, s: string): void {
  for (let i = 0; i < s.length && i < len; i++) block[at + i] = s.charCodeAt(i);
}

function putOctal(block: Uint8Array, at: number, len: number, n: number): void {
  putString(block, at, len - 1, Math.floor(n).toString(8).padStart(len - 1, "0"));
  block[at + len - 1] = 0;
}

/** Splits a path into ustar's prefix (155) and name (100) fields at a "/". */
function splitTarName(path: string): { prefix: string; name: string } {
  if (path.length <= 100) return { prefix: "", name: path };
  for (let i = path.length - 1; i > 0; i--) {
    if (path[i] !== "/") continue;
    const prefix = path.slice(0, i);
    const name = path.slice(i + 1);
    if (name.length > 0 && name.length <= 100 && prefix.length <= 155) return { prefix, name };
    if (name.length > 100) break;
  }
  fail(`path too long for a tar header: ${path}`);
}

/** A ustar header for a regular file (mode 0644, owner 0). */
export function encodeTarHeader(entry: { path: string; size: number; mtime?: number }): Uint8Array<ArrayBuffer> {
  if (!isSafePackagePath(entry.path)) fail(`bad path ${entry.path}`);
  if (!Number.isSafeInteger(entry.size) || entry.size < 0 || entry.size > TAR_MAX_SIZE) fail(`bad size for ${entry.path}`);
  const block = new Uint8Array(TAR_BLOCK);
  const { prefix, name } = splitTarName(entry.path);
  putString(block, 0, 100, name);
  putOctal(block, 100, 8, 0o644);
  putOctal(block, 108, 8, 0);
  putOctal(block, 116, 8, 0);
  putOctal(block, 124, 12, entry.size);
  putOctal(block, 136, 12, Math.max(0, Math.min(entry.mtime ?? 0, 8 ** 11 - 1)));
  block.fill(0x20, 148, 156); // checksum field counts as spaces
  block[156] = 0x30; // '0' regular file
  putString(block, 257, 6, "ustar\0");
  putString(block, 263, 2, "00");
  putString(block, 345, 155, prefix);
  let sum = 0;
  for (const b of block) sum += b;
  putString(block, 148, 6, sum.toString(8).padStart(6, "0"));
  block[154] = 0;
  block[155] = 0x20;
  return block;
}

function readString(block: Uint8Array, at: number, len: number): string {
  let end = at;
  while (end < at + len && block[end] !== 0) end++;
  let s = "";
  for (let i = at; i < end; i++) s += String.fromCharCode(block[i]!);
  return s;
}

function readNumber(block: Uint8Array, at: number, len: number): number {
  if ((block[at]! & 0x80) !== 0) {
    // GNU base-256
    let n = block[at]! & 0x7f;
    for (let i = at + 1; i < at + len; i++) n = n * 256 + block[i]!;
    return n;
  }
  const s = readString(block, at, len).trim();
  if (s.length === 0) return 0;
  if (!/^[0-7]+$/.test(s)) return Number.NaN;
  return Number.parseInt(s, 8);
}

export interface TarEntry {
  path: string;
  type: string; // "0" regular file, "5" directory, ...
  size: number;
  offset: number; // where the data starts in the archive
}

/** Decodes one header block; null for a zero block (end of archive). Throws on a damaged header. */
export function decodeTarHeader(block: Uint8Array, offset = 0): TarEntry | null {
  if (block.length < TAR_BLOCK) fail("truncated tar header");
  let zero = true;
  for (let i = 0; i < TAR_BLOCK; i++) {
    if (block[i] !== 0) {
      zero = false;
      break;
    }
  }
  if (zero) return null;
  const stored = readNumber(block, 148, 8);
  let sum = 0;
  for (let i = 0; i < TAR_BLOCK; i++) sum += i >= 148 && i < 156 ? 0x20 : block[i]!;
  if (stored !== sum) fail("damaged tar header (checksum mismatch); the file is not a package or is corrupt");
  const magic = readString(block, 257, 6);
  const name = readString(block, 0, 100);
  const prefix = magic.startsWith("ustar") ? readString(block, 345, 155) : "";
  const size = readNumber(block, 124, 12);
  if (!Number.isSafeInteger(size) || size < 0) fail(`bad size in the tar header of ${name}`);
  const type = block[156] === 0 ? "0" : String.fromCharCode(block[156]!);
  return { path: prefix ? `${prefix}/${name}` : name, type, size, offset: offset + TAR_BLOCK };
}

/** Reads `length` bytes at `offset` (may return fewer only at the end of the file). */
export type ReadAt = (offset: number, length: number) => Promise<Uint8Array>;

/** Lists a tar archive's entries by hopping from header to header (reads one block per entry). */
export async function readTarIndex(read: ReadAt, totalSize: number): Promise<TarEntry[]> {
  const out: TarEntry[] = [];
  let off = 0;
  while (off < totalSize) {
    if (off + TAR_BLOCK > totalSize) fail(`the package is incomplete (${totalSize} bytes); copy or build it again`);
    const e = decodeTarHeader(await read(off, TAR_BLOCK), off);
    if (!e) break;
    if (e.type === "x" || e.type === "g" || e.type === "L" || e.type === "K") {
      fail("the archive uses tar extensions packages never contain; build the package with bo1-pack");
    }
    if (e.offset + e.size > totalSize) {
      fail(`the package is incomplete (${e.path} ends past the end of the file); copy or build it again`);
    }
    out.push(e);
    if (out.length > MAX_ENTRIES) fail("too many entries for a package");
    off = e.offset + e.size + tarPadding(e.size);
  }
  return out;
}

export interface PackageContents {
  manifest: PackageManifest;
  files: (PackageFile & { offset: number })[]; // the manifest's files with their data offsets
  checksums: TarEntry | null;
}

/**
 * Reads and checks a package: the first entry is the manifest, every listed file is present as a regular entry with
 * the listed size. Entries the manifest does not list (other than the checksums) are an error.
 */
export async function readPackage(read: ReadAt, totalSize: number): Promise<PackageContents> {
  const head = totalSize >= TAR_BLOCK ? await read(0, TAR_BLOCK) : new Uint8Array(0);
  let firstPath: string | null = null;
  try {
    firstPath = head.length === TAR_BLOCK ? (decodeTarHeader(head)?.path ?? null) : null;
  } catch {
    /* not a tar header */
  }
  if (firstPath !== MANIFEST_NAME) fail(`not a BO1 Zombies package (no ${MANIFEST_NAME} first); build packages with bo1-pack`);
  const entries = await readTarIndex(read, totalSize);
  const first = entries[0];
  if (!first || first.path !== MANIFEST_NAME) fail(`not a BO1 Zombies package (no ${MANIFEST_NAME} first); build packages with bo1-pack`);
  if (first.size > MAX_MANIFEST_BYTES) fail("the manifest is too large");
  let raw: unknown;
  try {
    raw = JSON.parse(new TextDecoder().decode(await read(first.offset, first.size)));
  } catch {
    fail("the manifest is not valid JSON");
  }
  const manifest = validateManifest(raw);
  const byPath = new Map<string, TarEntry>();
  let checksums: TarEntry | null = null;
  for (const e of entries.slice(1)) {
    if (e.type !== "0" && e.type !== "7") continue; // directories are harmless
    if (e.path === CHECKSUMS_NAME) checksums = e;
    else byPath.set(e.path, e);
  }
  const files = manifest.files.map((f) => {
    const e = byPath.get(f.path);
    if (!e) fail(`${f.path} is listed in the manifest but missing from the package`);
    if (e.size !== f.size) fail(`${f.path} has ${e.size} bytes, the manifest says ${f.size}`);
    byPath.delete(f.path);
    return { ...f, offset: e.offset };
  });
  const extra = [...byPath.keys()];
  if (extra.length > 0) fail(`the package contains files its manifest does not list (${extra[0]})`);
  return { manifest, files, checksums };
}

/** Bytes of all game files in a package. */
export function packageDataBytes(m: PackageManifest): number {
  return m.files.reduce((n, f) => n + f.size, 0);
}
