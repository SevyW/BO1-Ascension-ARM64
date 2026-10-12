// client-pack.test.ts - making packages in the page (client/src/assets/pack.ts), its sha256 (shared/sha256.ts), and
// the importer's map selection (client/src/assets/import.ts). The page's packages must be the ones bo1-pack writes:
// the CLI (packer/cli.ts) verifies them and lists the same entries and checksums.

import { afterAll, beforeAll, describe, expect, spyOn, test } from "bun:test";
import { createHash, randomBytes } from "node:crypto";
import { mkdir, mkdtemp, readFile, rm, stat, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import { importedMapBytes, ImportSession, openPackage, type SourceFile } from "../client/src/assets/import";
import { CORE_LOCALIZED_REQUIRED, CORE_ZONES } from "../client/src/assets/manifest";
import { hashBlob, makePackages, PACK_TOOL, packageBlob, planFromSources, type PackProgress, type PackTarget } from "../client/src/assets/pack";
import { CHECKSUMS_NAME, packageFileName, parseChecksums, readPackage, readTarIndex, type ReadAt } from "../shared/gamepack";
import { Sha256 } from "../shared/sha256";
import { main } from "../packer/cli";
import { archiveBytes, manifestFor, type PlannedPackage } from "../packer/plan";

const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");

function blobRead(blob: Blob): ReadAt {
  return async (off, len) => new Uint8Array(await blob.slice(off, off + len).arrayBuffer());
}

describe("sha256", () => {
  test("matches node:crypto at the block and padding boundaries", () => {
    for (const n of [0, 1, 3, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 129, 1000, 4097]) {
      const d = randomBytes(n);
      expect(new Sha256().update(d).hex()).toBe(sha(d));
    }
  });

  test("any chunking gives the same digest", () => {
    const d = randomBytes(300_001);
    let seed = 7;
    const h = new Sha256();
    for (let off = 0; off < d.length; ) {
      seed = (seed * 1103515245 + 12345) >>> 0;
      const n = Math.min(d.length - off, seed % 3000);
      h.update(d.subarray(off, off + n));
      off += n;
    }
    expect(h.hex()).toBe(sha(d));
    expect(() => h.update(new Uint8Array(1))).toThrow();
  });

  test("hashBlob streams a Blob and reports progress", async () => {
    const d = randomBytes(200_000);
    let last = 0;
    expect(await hashBlob(new Blob([d]), (n) => (last = n))).toBe(sha(d));
    expect(last).toBe(d.length);
    const ac = new AbortController();
    ac.abort();
    await expect(hashBlob(new Blob([d]), undefined, ac.signal)).rejects.toThrow(/cancelled/);
  });
});

// ----- a fake install, on disk (for bo1-pack) and as picked files (for the page) -----

const ROOT = "Call of Duty Black Ops";

function installFiles(): { path: string; data: Uint8Array }[] {
  let seed = 3;
  const bytes = (n: number) => {
    const b = new Uint8Array(n);
    for (let i = 0; i < n; i++) b[i] = (seed = (seed * 1103515245 + 12345) >>> 0) >>> 24;
    return b;
  };
  const files = [{ path: "localization.txt", data: new TextEncoder().encode("english\r\n") }];
  for (const z of [...CORE_ZONES, "zombie_theater", "zombie_theater_patch", "zombie_pentagon", "mp_nuked"]) files.push({ path: `zone/Common/${z}.ff`, data: bytes(600 + z.length * 7) });
  for (const z of [...CORE_LOCALIZED_REQUIRED, "zombie_theater", "zombie_pentagon"]) files.push({ path: `zone/English/en_${z}.ff`, data: bytes(300 + z.length) });
  files.push({ path: "zone/French/fr_zombie_theater.ff", data: bytes(40) });
  files.push({ path: "main/iw_00.iwd", data: bytes(9000) }, { path: "main/iw_01.iwd", data: bytes(513) }, { path: "main/localized_english_iwd00.iwd", data: bytes(2048) });
  files.push({ path: "main/video/intro.bik", data: bytes(77) });
  return files;
}

function picked(files: { path: string; data: Uint8Array }[], root = ROOT): SourceFile[] {
  return files.map((f) => {
    const file = new File([f.data as Uint8Array<ArrayBuffer>], path.basename(f.path), { lastModified: 1_700_000_000_000 });
    return { path: `${root}/${f.path}`, size: f.data.length, file: async () => file };
  });
}

const byPath = (files: SourceFile[]) => new Map(files.map((f) => [f.path, f]));

function all(plan: { base: PlannedPackage | null; maps: PlannedPackage[] }): PlannedPackage[] {
  return [...(plan.base ? [plan.base] : []), ...plan.maps];
}

/** A FileSystemDirectoryHandle stand-in with Chromium's write semantics (the target changes on close only). */
class MemoryDir {
  name = "packages";
  files = new Map<string, Blob>();
  removed: string[] = [];
  writes = 0;
  async getFileHandle(name: string, opts: { create?: boolean } = {}) {
    if (!this.files.has(name)) {
      if (!opts.create) throw new DOMException("not found", "NotFoundError");
      this.files.set(name, new Blob([]));
    }
    const dir = this;
    return {
      kind: "file",
      name,
      async getFile() {
        return new File([dir.files.get(name)!], name);
      },
      async createWritable() {
        const parts: Blob[] = [];
        return {
          async write(b: Blob) {
            dir.writes++;
            parts.push(b);
          },
          async close() {
            dir.files.set(name, new Blob(parts));
          },
          async abort() {},
        };
      },
    };
  }
  async removeEntry(name: string) {
    this.files.delete(name);
    this.removed.push(name);
  }
  handle(): FileSystemDirectoryHandle {
    return this as unknown as FileSystemDirectoryHandle;
  }
}

describe("planFromSources", () => {
  test("plans like bo1-pack and takes the language from localization.txt", async () => {
    const files = picked(installFiles());
    const { plan, localizationText } = await planFromSources(files);
    expect(localizationText).toBe("english\r\n");
    expect(plan.language?.name).toBe("english");
    expect(plan.languagesFound).toEqual(["english", "french"]);
    expect(plan.coreMissing).toEqual([]);
    expect(all(plan).map((p) => p.id)).toEqual(["base", "zombie_theater", "zombie_pentagon"]);
    // a french localization.txt picks french: the base lacks its localized zones
    const french = installFiles().map((f) => (f.path === "localization.txt" ? { ...f, data: new TextEncoder().encode("french\n") } : f));
    const fr = await planFromSources(picked(french));
    expect(fr.plan.language?.name).toBe("french");
    expect(fr.plan.coreMissing.length).toBeGreaterThan(0);
    // the override wins
    expect((await planFromSources(picked(french), "english")).plan.language?.name).toBe("english");
  });
});

describe("makePackages", () => {
  let dir = "";
  let quiet: { mockRestore(): void }[] = [];

  beforeAll(async () => {
    dir = await mkdtemp(path.join(tmpdir(), "bo1-webpack-test-"));
    for (const f of installFiles()) {
      const p = path.join(dir, ROOT, ...f.path.split("/"));
      await mkdir(path.dirname(p), { recursive: true });
      await writeFile(p, f.data);
    }
    quiet = [spyOn(process.stdout, "write").mockImplementation(() => true), spyOn(process.stderr, "write").mockImplementation(() => true)];
  });

  afterAll(async () => {
    for (const q of quiet) q.mockRestore();
    await rm(dir, { recursive: true, force: true });
  });

  test("download: the same entries and checksums as bo1-pack, and bo1-pack --verify passes", async () => {
    const files = picked(installFiles());
    const { plan } = await planFromSources(files);
    const seen: PackProgress[] = [];
    const out = await makePackages(all(plan), byPath(files), { kind: "download" }, (p) => seen.push(p), new AbortController().signal);
    expect(out.error).toBeNull();
    expect(out.cancelled).toBe(false);
    expect(out.results.map((r) => [r.id, r.status])).toEqual([["base", "ready"], ["zombie_theater", "ready"], ["zombie_pentagon", "ready"]]);
    const last = seen[seen.length - 1]!;
    expect(last.bytesDone).toBe(last.bytesTotal);
    expect(last.bytesTotal).toBe(all(plan).reduce((n, p) => n + p.dataBytes, 0));

    expect(await main([path.join(dir, ROOT), "--out", path.join(dir, "cli")])).toBe(0);
    for (const r of out.results) {
      const blob = r.blob!;
      expect(r.fileName).toBe(packageFileName(r.id, "english"));
      expect(blob.size).toBe(r.bytes);
      const web = await readPackage(blobRead(blob), blob.size);
      expect(web.manifest.tool).toBe(PACK_TOOL);
      const cliBytes = new Uint8Array(await readFile(path.join(dir, "cli", r.fileName)));
      const cliRead: ReadAt = async (o, l) => cliBytes.subarray(o, o + l);
      const cli = await readPackage(cliRead, cliBytes.length);
      expect(web.manifest.files).toEqual(cli.manifest.files);
      // the same entries in the same order; only the manifest differs (its "tool" says "(web)")
      const entries = async (read: ReadAt, size: number) => (await readTarIndex(read, size)).map((e, i) => (i === 0 ? [e.path] : [e.path, e.size]));
      expect(await entries(blobRead(blob), blob.size)).toEqual(await entries(cliRead, cliBytes.length));
      const sumsText = async (read: ReadAt, e: { offset: number; size: number }) => new TextDecoder().decode(await read(e.offset, e.size));
      const webSums = await sumsText(blobRead(blob), web.checksums!);
      expect(webSums).toBe(await sumsText(cliRead, cli.checksums!));
      for (const f of web.files) {
        const src = installFiles().find((x) => x.path === f.path)!;
        expect(parseChecksums(webSums).get(f.path)).toBe(sha(src.data));
        expect(new Uint8Array(await blob.slice(f.offset, f.offset + f.size).arrayBuffer())).toEqual(new Uint8Array(src.data));
      }
      // the importer opens it
      expect((await openPackage(blob, r.fileName)).manifest.id).toBe(r.id);
      // and bo1-pack verifies it
      const file = path.join(dir, `web-${r.fileName}`);
      await writeFile(file, new Uint8Array(await blob.arrayBuffer()));
      expect(await main(["--verify", file])).toBe(0);
    }
  });

  test("folder: writes, skips up-to-date packages on the next run, removes a new file when cancelled", async () => {
    const files = picked(installFiles());
    const { plan } = await planFromSources(files);
    const out = new MemoryDir();
    const target: PackTarget = { kind: "folder", dir: out.handle() };
    const first = await makePackages(all(plan), byPath(files), target, () => undefined, new AbortController().signal);
    expect(first.error).toBeNull();
    expect(first.results.map((r) => r.status)).toEqual(["written", "written", "written"]);
    for (const p of all(plan)) {
      const blob = out.files.get(p.fileName)!;
      expect(blob.size).toBe(archiveBytes(p, manifestFor(p, PACK_TOOL)));
      expect((await openPackage(blob, p.fileName)).manifest.files.length).toBe(p.files.length);
    }
    const writes = out.writes;
    let hashed = false;
    const second = await makePackages(all(plan), byPath(files), target, (p) => (hashed ||= p.phase === "hashing"), new AbortController().signal);
    expect(second.results.map((r) => r.status)).toEqual(["up-to-date", "up-to-date", "up-to-date"]);
    expect(out.writes).toBe(writes);
    expect(hashed).toBe(false);

    // cancelled while writing a package that was not there before: no file is left behind
    out.files.delete(plan.maps[0]!.fileName);
    const ac = new AbortController();
    const third = await makePackages(all(plan), byPath(files), target, (p) => p.phase === "writing" && ac.abort(), ac.signal);
    expect(third.cancelled).toBe(true);
    expect(out.files.has(plan.maps[0]!.fileName)).toBe(false);
    expect(out.removed).toContain(plan.maps[0]!.fileName);
  });

  test("a file that changed since the pick stops the job with a clear message", async () => {
    const files = picked(installFiles());
    const { plan } = await planFromSources(files);
    const iwd = files.find((f) => f.path.endsWith("iw_00.iwd"))!;
    iwd.file = async () => new File([new Uint8Array(10)], "iw_00.iwd");
    const out = await makePackages([plan.base!], byPath(files), { kind: "download" }, () => undefined, new AbortController().signal);
    expect(out.error).toMatch(/bo1-zombies-base-english\.tar: .*iw_00\.iwd changed size/);
    expect(out.results).toEqual([]);
  });

  test("packageBlob refuses a missing checksum or a wrong size", () => {
    const p: PlannedPackage = { id: "zombie_theater", kind: "map", map: "zombie_theater", language: "english", fileName: "x.tar", files: [{ source: "a", path: "zone/Common/zombie_theater.ff", size: 3 }], dataBytes: 3 };
    const m = manifestFor(p, PACK_TOOL);
    const blobs = new Map([["zone/Common/zombie_theater.ff", new Blob(["abc"])]]);
    expect(() => packageBlob(m, blobs, new Map())).toThrow(/missing/);
    expect(() => packageBlob(m, new Map([["zone/Common/zombie_theater.ff", new Blob(["abcd"])]]), new Map([["zone/Common/zombie_theater.ff", "0".repeat(64)]]))).toThrow(/expected 3/);
    const ok = packageBlob(m, blobs, new Map([["zone/Common/zombie_theater.ff", "0".repeat(64)]]));
    expect(ok.size).toBe(archiveBytes(p, m));
  });

  test("the checksum entry name is the shared one", async () => {
    const files = picked(installFiles());
    const { plan } = await planFromSources(files);
    const out = await makePackages([plan.maps[1]!], byPath(files), { kind: "download" }, () => undefined, new AbortController().signal);
    const blob = out.results[0]!.blob!;
    const names = (await readTarIndex(blobRead(blob), blob.size)).map((e) => e.path);
    expect(names[names.length - 1]).toBe(CHECKSUMS_NAME);
    expect((await stat(dir)).isDirectory()).toBe(true);
  });
});

describe("ImportSession map selection", () => {
  test("unchecked maps are left out of the copy; the base always comes along", async () => {
    const s = new ImportSession();
    await s.addPicked(picked(installFiles()));
    const choices = s.mapChoices();
    expect(choices.map((c) => [c.id, c.selected, c.missing])).toEqual([["zombie_theater", true, []], ["zombie_pentagon", true, []]]);
    const theater = choices[0]!;
    expect(theater.bytes).toBe(theater.copyBytes);
    const before = s.plan();
    s.setMapSelected("zombie_theater", false);
    const after = s.plan();
    expect(before.copyBytes - after.copyBytes).toBe(theater.bytes);
    expect(after.selected.some((f) => f.info.map === "zombie_theater")).toBe(false);
    expect(after.selected.some((f) => f.dest === "main/iw_00.iwd")).toBe(true);
    expect(s.mapChoices().find((c) => c.id === "zombie_theater")!.selected).toBe(false);
    const av = s.projected("english", after.selected);
    expect(av.maps.zombie_theater!.available).toBe(false);
    expect(av.maps.zombie_pentagon!.available).toBe(true);
    // a map already imported costs nothing to keep checked
    for (const f of after.selected) s.existing.set(f.dest.toLowerCase(), f.size);
    expect(s.mapChoices().find((c) => c.id === "zombie_pentagon")!.copyBytes).toBe(0);
    s.setMapSelected("zombie_theater", true);
    expect(s.plan().copy.every((f) => f.info.map === "zombie_theater")).toBe(true);
    s.clear();
    expect(s.excludedMaps.size).toBe(0);
  });

  test("a map without its localized zone is offered but marked not playable", async () => {
    const s = new ImportSession();
    await s.addPicked(picked([...installFiles(), { path: "zone/Common/zombie_cosmodrome.ff", data: new Uint8Array(9) }]));
    const c = s.mapChoices("english").find((m) => m.id === "zombie_cosmodrome")!;
    expect(c.selected).toBe(true);
    expect(c.missing).toEqual(["zone/English/en_zombie_cosmodrome.ff"]);
  });

  test("importedMapBytes sums each map's zones", () => {
    const files: Record<string, number> = {
      "zone/Common/zombie_theater.ff": 100,
      "zone/Common/zombie_theater_patch.ff": 10,
      "zone/English/en_zombie_theater.ff": 5,
      "zone/Common/patch.ff": 999,
      "main/iw_00.iwd": 999,
    };
    expect(importedMapBytes({ files, maps: [] })).toEqual({ zombie_theater: 115 });
  });
});
