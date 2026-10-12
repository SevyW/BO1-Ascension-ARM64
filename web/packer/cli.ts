// cli.ts - bo1-pack: turns the player's own Black Ops install into game file packages for the BO1 Zombies web build
// (web/shared/gamepack.ts has the format, web/packer/README.md the usage). Runs on the player's machine with Node 18+
// or Bun; nothing is uploaded. main.ts runs it; web/scripts/build.ts bundles both into one file, served as
// <BASE_PATH>tools/bo1-pack.mjs.
//
//   node bo1-pack.mjs <Black Ops folder> [--out dir] [--only base,zombie_theater] [--language english] [--list] [--force]
//   node bo1-pack.mjs --verify <package.tar>...
//
// Exit codes: 0 done, 1 bad arguments or an I/O error, 2 nothing to package (or the base files are incomplete),
// 3 a verified package failed.

import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import * as fs from "node:fs/promises";
import path from "node:path";
import { parseArgs } from "node:util";
import {
  CHECKSUMS_NAME,
  encodeManifest,
  encodeTarHeader,
  formatChecksums,
  MANIFEST_NAME,
  PACKER_VERSION,
  PackageError,
  parseChecksums,
  readPackage,
  TAR_BLOCK,
  TAR_TRAILER,
  tarPadding,
  type ReadAt,
} from "../shared/gamepack";
import { MAPS } from "../shared/roster";
import { formatBytes, type PickedFile } from "../client/src/assets/manifest";
import {
  archiveBytes as packageBytes,
  knownLanguages,
  knownPackageIds,
  localizationSource,
  manifestFor,
  planPackages,
  type PackagePlan,
  type PlannedPackage,
} from "./plan";

const TOOL = `bo1-pack ${PACKER_VERSION}`;
const RELEVANT = /\.(ff|iwd)$/i;
const SKIP_DIRS = new Set(["$recycle.bin", "system volume information", ".git", "node_modules", "redist"]);
const MAX_DEPTH = 8;
const CHUNK = 4 << 20;

const USAGE = `${TOOL} - packages your own Black Ops files for the BO1 Zombies web build (nothing is uploaded)

usage:
  node bo1-pack.mjs <Black Ops folder> [options]     write the packages
  node bo1-pack.mjs --verify <package.tar>...       check packages against their checksums

The folder is the install that holds zone/ and main/ (or its parent). Writes one package with the files every map
needs (bo1-zombies-base-<language>.tar) and one per map found (bo1-zombies-<map>-<language>.tar). Import them on
the site's "Game files" screen with "Add packages".

options:
  -o, --out <dir>         where to write the packages (default: ./bo1-packages)
      --only <ids>        comma-separated packages to write: base and/or map ids (default: all found)
  -l, --language <name>   the language to package (default: the install's localization.txt)
      --list              show what would be written, write nothing
  -f, --force             rewrite packages that are already up to date
      --verify            check the given packages (sizes and sha256 of every file)
  -h, --help              this text
      --version           print the version

map ids: ${MAPS.map((m) => m.id).join(", ")}`;

class UsageError extends Error {}

function out(line = ""): void {
  process.stdout.write(`${line}\n`);
}

function err(line: string): void {
  process.stderr.write(`${line}\n`);
}

function mapLabel(p: PlannedPackage): string {
  return p.map ? `${p.map} (${MAPS.find((m) => m.id === p.map)?.name ?? p.map})` : "base (files every map needs)";
}

// ----- scanning -----

async function scan(dir: string): Promise<PickedFile[]> {
  const top = path.basename(path.resolve(dir));
  const files: PickedFile[] = [];
  const walk = async (abs: string, rel: string, depth: number): Promise<void> => {
    let entries: import("node:fs").Dirent[];
    try {
      entries = await fs.readdir(abs, { withFileTypes: true });
    } catch (e) {
      if (depth === 0) throw e;
      return; // unreadable subfolder
    }
    for (const e of entries) {
      const childAbs = path.join(abs, e.name);
      const childRel = `${rel}/${e.name}`;
      let isDir = e.isDirectory();
      let isFile = e.isFile();
      if (e.isSymbolicLink()) {
        const st = await fs.stat(childAbs).catch(() => null);
        isDir = !!st?.isDirectory();
        isFile = !!st?.isFile();
      }
      if (isDir) {
        if (depth < MAX_DEPTH && !SKIP_DIRS.has(e.name.toLowerCase())) await walk(childAbs, childRel, depth + 1);
      } else if (isFile && (RELEVANT.test(e.name) || e.name.toLowerCase() === "localization.txt")) {
        const st = await fs.stat(childAbs);
        files.push({ path: childRel, size: st.size });
      }
    }
  };
  await walk(dir, top, 0);
  return files;
}

/** Absolute path of a planned file's source (picked paths start with the scanned folder's own name). */
function sourcePath(dir: string, picked: string): string {
  const rest = picked.split("/").slice(1);
  return path.join(dir, ...rest);
}

// ----- writing -----

interface Progress {
  label: string;
  total: number;
  done: number;
  started: number;
  last: number;
}

const tty = process.stdout.isTTY === true;

function showProgress(p: Progress, final = false): void {
  const now = Date.now();
  if (!final && (!tty || now - p.last < 250)) return;
  p.last = now;
  const secs = (now - p.started) / 1000;
  const rate = secs > 0.5 ? ` ${formatBytes(p.done / secs)}/s` : "";
  const pct = p.total > 0 ? Math.floor((p.done / p.total) * 100) : 100;
  const line = `  ${p.label}  ${formatBytes(p.done)} / ${formatBytes(p.total)}  ${pct}%${rate}`;
  if (tty) process.stdout.write(`\r${line.padEnd(78)}${final ? "\n" : ""}`);
  else if (final) out(line);
}

/** The package's size on disk. */
function archiveBytes(p: PlannedPackage): number {
  return packageBytes(p, manifestFor(p, TOOL));
}

function fileReader(fh: fs.FileHandle): ReadAt {
  return async (offset, length) => {
    const buf = Buffer.alloc(length);
    const { bytesRead } = await fh.read(buf, 0, length, offset);
    return new Uint8Array(buf.buffer, buf.byteOffset, bytesRead);
  };
}

/** True when `file` is a package with exactly this plan's files (same paths and sizes, same language). */
async function upToDate(file: string, p: PlannedPackage): Promise<boolean> {
  let fh: fs.FileHandle | null = null;
  try {
    fh = await fs.open(file, "r");
    const st = await fh.stat();
    const pkg = await readPackage(fileReader(fh), st.size);
    const m = pkg.manifest;
    if (m.id !== p.id || m.language !== p.language || m.files.length !== p.files.length || !pkg.checksums) return false;
    return m.files.every((f, i) => f.path === p.files[i]!.path && f.size === p.files[i]!.size);
  } catch {
    return false;
  } finally {
    await fh?.close();
  }
}

async function writeAll(fh: fs.FileHandle, data: Uint8Array): Promise<void> {
  let off = 0;
  while (off < data.length) {
    const { bytesWritten } = await fh.write(data, off, data.length - off);
    if (bytesWritten <= 0) throw new Error("write failed (disk full?)");
    off += bytesWritten;
  }
}

async function writePackage(dir: string, outDir: string, p: PlannedPackage): Promise<{ file: string; bytes: number }> {
  const target = path.join(outDir, p.fileName);
  const part = `${target}.part`;
  const manifest = encodeManifest(manifestFor(p, TOOL));
  const total = archiveBytes(p);
  const progress: Progress = { label: p.fileName, total, done: 0, started: Date.now(), last: 0 };
  const zeros = new Uint8Array(TAR_BLOCK);
  const fh = await fs.open(part, "w");
  try {
    const entry = async (name: string, data: Uint8Array, mtime: number) => {
      await writeAll(fh, encodeTarHeader({ path: name, size: data.length, mtime }));
      await writeAll(fh, data);
      await writeAll(fh, zeros.subarray(0, tarPadding(data.length)));
      progress.done += TAR_BLOCK + data.length + tarPadding(data.length);
    };
    const now = Math.floor(Date.now() / 1000);
    await entry(MANIFEST_NAME, manifest, now);
    const sums: { path: string; sha256: string }[] = [];
    for (const f of p.files) {
      const src = sourcePath(dir, f.source);
      const st = await fs.stat(src);
      if (st.size !== f.size) throw new Error(`${src} changed size while packaging; run bo1-pack again`);
      await writeAll(fh, encodeTarHeader({ path: f.path, size: f.size, mtime: Math.floor(st.mtimeMs / 1000) }));
      progress.done += TAR_BLOCK;
      const hash = createHash("sha256");
      let n = 0;
      for await (const chunk of createReadStream(src, { highWaterMark: CHUNK })) {
        const b = chunk as Buffer;
        hash.update(b);
        await writeAll(fh, b);
        n += b.length;
        progress.done += b.length;
        showProgress(progress);
      }
      if (n !== f.size) throw new Error(`${src}: read ${n} of ${f.size} bytes; run bo1-pack again`);
      await writeAll(fh, zeros.subarray(0, tarPadding(f.size)));
      progress.done += tarPadding(f.size);
      sums.push({ path: f.path, sha256: hash.digest("hex") });
    }
    await entry(CHECKSUMS_NAME, formatChecksums(sums), now);
    await writeAll(fh, new Uint8Array(TAR_TRAILER));
    progress.done += TAR_TRAILER;
    await fh.sync();
  } catch (e) {
    await fh.close().catch(() => undefined);
    await fs.rm(part, { force: true });
    if (tty) process.stdout.write("\n");
    throw e;
  }
  await fh.close();
  const written = (await fs.stat(part)).size;
  if (written !== total) {
    await fs.rm(part, { force: true });
    throw new Error(`${p.fileName}: wrote ${written} bytes, expected ${total}`);
  }
  await fs.rename(part, target);
  showProgress(progress, true);
  return { file: target, bytes: total };
}

async function freeBytes(dir: string): Promise<number | null> {
  const statfs = (fs as unknown as { statfs?: (p: string) => Promise<{ bavail: number; bsize: number }> }).statfs;
  if (typeof statfs !== "function") return null;
  try {
    const s = await statfs(dir);
    return s.bavail * s.bsize;
  } catch {
    return null;
  }
}

// ----- commands -----

function printPlan(plan: PackagePlan, chosen: PlannedPackage[]): void {
  out(`language: ${plan.language?.name ?? "unknown"}${plan.languagesFound.length > 1 ? ` (found: ${plan.languagesFound.join(", ")}; pick another with --language)` : ""}`);
  if (plan.coreMissing.length > 0) {
    out(`base files incomplete, missing: ${plan.coreMissing.join(", ")}`);
  }
  for (const m of plan.incompleteMaps) out(`skipping ${m.id}: missing ${m.missing.join(", ")}`);
  out("");
  for (const p of chosen) {
    out(`${p.fileName}  ${formatBytes(archiveBytes(p))}  ${p.files.length} files  ${mapLabel(p)}`);
  }
}

async function pack(dir: string, opts: { out: string; only: string[] | null; language: string | null; list: boolean; force: boolean }): Promise<number> {
  const st = await fs.stat(dir).catch(() => null);
  if (!st?.isDirectory()) throw new UsageError(`not a folder: ${dir}`);
  out(`scanning ${path.resolve(dir)}`);
  const files = await scan(dir);
  // read the localization.txt at the detected root (the planner only sees names and sizes)
  const loc = localizationSource(planPackages(files, { language: opts.language }));
  const localizationText = loc ? await fs.readFile(sourcePath(dir, loc), "utf8").catch(() => null) : null;
  const plan = planPackages(files, { language: opts.language, localizationText });
  if (opts.language && !plan.language) throw new UsageError(`unknown language "${opts.language}" (known: ${knownLanguages().join(", ")})`);
  if (plan.root === null || (!plan.base && plan.maps.length === 0)) {
    err("no Black Ops Zombies files found there. Point bo1-pack at the folder that holds zone/ and main/.");
    return 2;
  }
  const all = [...(plan.base ? [plan.base] : []), ...plan.maps];
  const chosen = opts.only ? all.filter((p) => opts.only!.includes(p.id)) : all;
  if (opts.only) {
    const missing = opts.only.filter((id) => !all.some((p) => p.id === id));
    for (const id of missing) err(`${id}: not found in this install${plan.incompleteMaps.some((m) => m.id === id) ? " (incomplete)" : ""}`);
  }
  printPlan(plan, chosen);
  const wantsBase = chosen.some((p) => p.kind === "base");
  if (wantsBase && plan.coreMissing.length > 0) {
    err("\nthe base package would not start a game; fix the install (or pick the right folder) and run again.");
    return 2;
  }
  if (chosen.length === 0) {
    err("nothing to write");
    return 2;
  }
  if (opts.list) return 0;

  const outDir = path.resolve(opts.out);
  await fs.mkdir(outDir, { recursive: true });
  const todo: PlannedPackage[] = [];
  out("");
  for (const p of chosen) {
    if (!opts.force && (await upToDate(path.join(outDir, p.fileName), p))) out(`${p.fileName}  up to date`);
    else todo.push(p);
  }
  const need = todo.reduce((n, p) => n + archiveBytes(p), 0);
  const free = await freeBytes(outDir);
  if (free !== null && need > free) {
    err(`not enough disk space in ${outDir}: need ${formatBytes(need)}, ${formatBytes(free)} free`);
    return 1;
  }
  for (const p of todo) await writePackage(dir, outDir, p);
  out(`\n${chosen.length} package${chosen.length === 1 ? "" : "s"} in ${outDir}`);
  out(`import them on the site: Game files -> Add packages`);
  return 0;
}

async function verify(files: string[]): Promise<number> {
  let bad = 0;
  for (const file of files) {
    let fh: fs.FileHandle | null = null;
    try {
      fh = await fs.open(file, "r");
      const st = await fh.stat();
      const read = fileReader(fh);
      const pkg = await readPackage(read, st.size);
      if (!pkg.checksums) throw new PackageError(`no ${CHECKSUMS_NAME} in the package`);
      const sums = parseChecksums(new TextDecoder().decode(await read(pkg.checksums.offset, pkg.checksums.size)));
      const progress: Progress = { label: path.basename(file), total: pkg.files.reduce((n, f) => n + f.size, 0), done: 0, started: Date.now(), last: 0 };
      for (const f of pkg.files) {
        const want = sums.get(f.path);
        if (!want) throw new PackageError(`${f.path} has no checksum`);
        const hash = createHash("sha256");
        if (f.size > 0) {
          for await (const chunk of createReadStream(file, { start: f.offset, end: f.offset + f.size - 1, highWaterMark: CHUNK })) {
            hash.update(chunk as Buffer);
            progress.done += (chunk as Buffer).length;
            showProgress(progress);
          }
        }
        if (hash.digest("hex") !== want) throw new PackageError(`${f.path} does not match its checksum (damaged copy)`);
      }
      showProgress(progress, true);
      const m = pkg.manifest;
      out(`OK    ${file}  ${m.kind === "map" ? m.map : "base"}, ${m.language}, ${m.files.length} files`);
    } catch (e) {
      bad++;
      if (tty) process.stdout.write("\n");
      out(`FAIL  ${file}  ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      await fh?.close();
    }
  }
  return bad > 0 ? 3 : 0;
}

export async function main(argv: string[]): Promise<number> {
  let parsed;
  try {
    parsed = parseArgs({
      args: argv,
      allowPositionals: true,
      options: {
        out: { type: "string", short: "o" },
        only: { type: "string" },
        language: { type: "string", short: "l" },
        list: { type: "boolean" },
        force: { type: "boolean", short: "f" },
        verify: { type: "boolean" },
        help: { type: "boolean", short: "h" },
        version: { type: "boolean" },
      },
    });
  } catch (e) {
    err(e instanceof Error ? e.message : String(e));
    err("run with --help for usage");
    return 1;
  }
  const { values, positionals } = parsed;
  if (values.help) {
    out(USAGE);
    return 0;
  }
  if (values.version) {
    out(TOOL);
    return 0;
  }
  try {
    if (values.verify) {
      if (positionals.length === 0) throw new UsageError("--verify needs one or more package files");
      return await verify(positionals);
    }
    if (positionals.length !== 1) throw new UsageError("give exactly one Black Ops folder");
    let only: string[] | null = null;
    if (values.only) {
      only = values.only.split(",").map((s) => s.trim()).filter((s) => s.length > 0);
      const unknown = only.filter((id) => !knownPackageIds().includes(id));
      if (unknown.length > 0) throw new UsageError(`unknown package id: ${unknown.join(", ")} (known: ${knownPackageIds().join(", ")})`);
    }
    return await pack(positionals[0]!, {
      out: values.out ?? "bo1-packages",
      only,
      language: values.language ?? null,
      list: values.list === true,
      force: values.force === true,
    });
  } catch (e) {
    if (e instanceof UsageError) {
      err(e.message);
      err("run with --help for usage");
      return 1;
    }
    err(`error: ${e instanceof Error ? e.message : String(e)}`);
    return 1;
  }
}
