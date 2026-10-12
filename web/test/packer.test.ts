// packer.test.ts - the package format (shared/gamepack.ts), the planner (packer/plan.ts) and the bo1-pack CLI
// (packer/cli.ts) against a fake install of random bytes in a temp folder.

import { afterAll, beforeAll, describe, expect, spyOn, test } from "bun:test";
import { spawnSync } from "node:child_process";
import { mkdir, mkdtemp, open, readFile, rm, stat, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import path from "node:path";
import {
  decodeTarHeader,
  encodeTarHeader,
  PackageError,
  packageFileName,
  readPackage,
  readTarIndex,
  TAR_BLOCK,
  validateManifest,
  type ReadAt,
} from "../shared/gamepack";
import { main } from "../packer/cli";
import { planPackages } from "../packer/plan";

const CORE = ["code_pre_gfx_mp", "code_post_gfx_mp", "code_post_gfx", "patch_mp", "patch", "ui_mp", "frontend", "common_zombie", "common_zombie_patch"];
const CORE_LOC = ["code_pre_gfx_mp", "code_post_gfx_mp", "code_post_gfx", "patch_mp", "patch", "common_zombie"];

function fakeInstall(): { path: string; size: number }[] {
  const files: { path: string; size: number }[] = [{ path: "localization.txt", size: 9 }];
  for (const z of [...CORE, "zombie_theater", "zombie_theater_patch", "zombie_pentagon", "zombie_cosmodrome", "mp_nuked"]) {
    files.push({ path: `zone/Common/${z}.ff`, size: 1000 + z.length });
  }
  for (const z of [...CORE_LOC, "ui_mp", "zombie_theater", "zombie_pentagon", "mp_nuked"]) {
    files.push({ path: `zone/English/en_${z}.ff`, size: 700 + z.length });
  }
  files.push(
    { path: "zone/French/fr_zombie_theater.ff", size: 50 },
    { path: "main/iw_00.iwd", size: 70000 },
    { path: "main/iw_01.iwd", size: 513 },
    { path: "main/localized_english_iwd00.iwd", size: 4096 },
    { path: "main/localized_french_iwd00.iwd", size: 100 },
    { path: "main/video/intro.bik", size: 777 },
    { path: "Redist/vcredist.exe", size: 10 },
  );
  return files;
}

function fileRead(bytes: Uint8Array): ReadAt {
  return async (off, len) => bytes.subarray(off, off + len);
}

describe("tar headers", () => {
  test("round trip, ustar fields, checksum", () => {
    const h = encodeTarHeader({ path: "zone/English/en_zombie_theater.ff", size: 123456789, mtime: 1700000000 });
    expect(h.length).toBe(TAR_BLOCK);
    expect(new TextDecoder().decode(h.subarray(257, 262))).toBe("ustar");
    const e = decodeTarHeader(h, 1024);
    expect(e).toEqual({ path: "zone/English/en_zombie_theater.ff", type: "0", size: 123456789, offset: 1024 + TAR_BLOCK });
  });

  test("long paths use the prefix field", () => {
    const long = `${"a".repeat(120)}/${"b".repeat(90)}.ff`;
    expect(decodeTarHeader(encodeTarHeader({ path: long, size: 1 }))?.path).toBe(long);
    expect(() => encodeTarHeader({ path: `x/${"c".repeat(101)}`, size: 1 })).toThrow(PackageError);
  });

  test("rejects unsafe paths and damaged headers", () => {
    for (const p of ["../x.ff", "/abs.ff", "a\\b.ff", "a//b.ff", "zone/./x.ff", "", "ü.ff"]) {
      expect(() => encodeTarHeader({ path: p, size: 1 })).toThrow(PackageError);
    }
    const h = encodeTarHeader({ path: "x.ff", size: 5 });
    h[0] = 0x79;
    expect(() => decodeTarHeader(h)).toThrow(/checksum/);
    expect(decodeTarHeader(new Uint8Array(TAR_BLOCK))).toBeNull();
  });
});

describe("manifest validation", () => {
  const good = {
    format: "bo1-zombies-package",
    version: 1,
    id: "zombie_theater",
    kind: "map",
    map: "zombie_theater",
    language: "english",
    created: "2026-10-08T00:00:00.000Z",
    tool: "bo1-pack 1",
    files: [{ path: "zone/Common/zombie_theater.ff", size: 3 }],
  };

  test("accepts a good manifest", () => {
    expect(validateManifest(good).map).toBe("zombie_theater");
  });

  test("rejects bad ones", () => {
    const bad: unknown[] = [
      null,
      { ...good, format: "zip" },
      { ...good, version: 99 },
      { ...good, kind: "base" },
      { ...good, map: "zombie_moon" },
      { ...good, id: "Bad Id" },
      { ...good, language: "EN!" },
      { ...good, files: [] },
      { ...good, files: [{ path: "../evil.ff", size: 1 }] },
      { ...good, files: [{ path: "a.ff", size: -1 }] },
      { ...good, files: [{ path: "a.ff", size: 1 }, { path: "A.ff", size: 1 }] },
      { ...good, files: [{ path: "bo1-package.json", size: 1 }] },
    ];
    for (const b of bad) expect(() => validateManifest(b)).toThrow(PackageError);
  });
});

describe("planPackages", () => {
  const picked = fakeInstall().map((f) => ({ path: `Call of Duty Black Ops/${f.path}`, size: f.size }));

  test("base + one package per complete map, chosen language only", () => {
    const plan = planPackages(picked, { localizationText: "english\r\n" });
    expect(plan.root).toBe("Call of Duty Black Ops");
    expect(plan.language?.name).toBe("english");
    expect(plan.languagesFound).toEqual(["english", "french"]);
    expect(plan.coreMissing).toEqual([]);
    const base = plan.base!;
    expect(base.fileName).toBe("bo1-zombies-base-english.tar");
    const paths = base.files.map((f) => f.path);
    expect(paths[0]).toBe("localization.txt");
    expect(paths).toContain("zone/Common/frontend.ff");
    expect(paths).toContain("zone/English/en_ui_mp.ff"); // optional localized core zone, packaged when present
    expect(paths).toContain("main/iw_00.iwd");
    expect(paths).toContain("main/localized_english_iwd00.iwd");
    expect(paths.some((p) => /french|fr_|video|mp_nuked|zombie_|Redist/i.test(p.replace("common_zombie", "")))).toBe(false);
    expect(base.files.length).toBe(1 + CORE.length + CORE_LOC.length + 1 + 3);
    expect(plan.ignored).toBeGreaterThan(0);
    expect(plan.maps.map((m) => m.id)).toEqual(["zombie_theater", "zombie_pentagon"]);
    expect(plan.maps[0]!.files.map((f) => f.path)).toEqual([
      "zone/Common/zombie_theater.ff",
      "zone/Common/zombie_theater_patch.ff",
      "zone/English/en_zombie_theater.ff",
    ]);
    expect(plan.incompleteMaps).toEqual([{ id: "zombie_cosmodrome", missing: ["zone/English/en_zombie_cosmodrome.ff"] }]);
  });

  test("another language reports what the base lacks", () => {
    const plan = planPackages(picked, { language: "french" });
    expect(plan.language?.name).toBe("french");
    expect(plan.coreMissing).toContain("zone/French/fr_code_pre_gfx_mp.ff");
    expect(plan.maps.map((m) => m.id)).toEqual(["zombie_theater"]);
    expect(plan.maps[0]!.fileName).toBe("bo1-zombies-zombie_theater-french.tar");
  });

  test("a folder without game files", () => {
    const plan = planPackages([{ path: "Downloads/readme.txt", size: 3 }]);
    expect(plan.base).toBeNull();
    expect(plan.maps).toEqual([]);
  });
});

describe("bo1-pack", () => {
  let dir = "";
  let game = "";
  let outDir = "";
  let quiet: { mockRestore(): void }[] = [];

  beforeAll(async () => {
    dir = await mkdtemp(path.join(tmpdir(), "bo1-pack-test-"));
    game = path.join(dir, "Call of Duty Black Ops");
    outDir = path.join(dir, "out");
    let seed = 1;
    for (const f of fakeInstall()) {
      const p = path.join(game, ...f.path.split("/"));
      await mkdir(path.dirname(p), { recursive: true });
      if (f.path === "localization.txt") {
        await writeFile(p, "english\r\n");
        continue;
      }
      const bytes = new Uint8Array(f.size);
      for (let i = 0; i < bytes.length; i++) bytes[i] = (seed = (seed * 1103515245 + 12345) >>> 0) >>> 24;
      await writeFile(p, bytes);
    }
    quiet = [spyOn(process.stdout, "write").mockImplementation(() => true), spyOn(process.stderr, "write").mockImplementation(() => true)];
  });

  afterAll(async () => {
    for (const q of quiet) q.mockRestore();
    await rm(dir, { recursive: true, force: true });
  });

  const pkgPath = (id: string) => path.join(outDir, packageFileName(id, "english"));

  test("writes base and map packages that hold the install's bytes", async () => {
    expect(await main([game, "--out", outDir])).toBe(0);
    for (const id of ["base", "zombie_theater", "zombie_pentagon"]) {
      const bytes = new Uint8Array(await readFile(pkgPath(id)));
      expect(bytes.length % TAR_BLOCK).toBe(0);
      const pkg = await readPackage(fileRead(bytes), bytes.length);
      expect(pkg.manifest.id).toBe(id);
      expect(pkg.manifest.language).toBe("english");
      expect(pkg.checksums).not.toBeNull();
      for (const f of pkg.files) {
        const src = new Uint8Array(await readFile(path.join(game, ...f.path.split("/"))));
        expect(bytes.subarray(f.offset, f.offset + f.size)).toEqual(src);
      }
    }
    await expect(stat(pkgPath("zombie_cosmodrome"))).rejects.toThrow();
  });

  test("a second run leaves up-to-date packages alone; --force rewrites", async () => {
    const before = (await stat(pkgPath("zombie_theater"))).mtimeMs;
    await Bun.sleep(20);
    expect(await main([game, "--out", outDir, "--only", "zombie_theater"])).toBe(0);
    expect((await stat(pkgPath("zombie_theater"))).mtimeMs).toBe(before);
    expect(await main([game, "--out", outDir, "--only", "zombie_theater", "--force"])).toBe(0);
    expect((await stat(pkgPath("zombie_theater"))).mtimeMs).toBeGreaterThan(before);
  });

  test("--verify passes good packages and fails damaged or cut ones", async () => {
    expect(await main(["--verify", pkgPath("base"), pkgPath("zombie_theater")])).toBe(0);
    const bad = path.join(dir, "bad.tar");
    const bytes = new Uint8Array(await readFile(pkgPath("zombie_theater")));
    const pkg = await readPackage(fileRead(bytes), bytes.length);
    bytes[pkg.files[0]!.offset + 10]! ^= 0xff;
    await writeFile(bad, bytes);
    expect(await main(["--verify", bad])).toBe(3);
    const cut = path.join(dir, "cut.tar");
    await writeFile(cut, bytes.subarray(0, 3 * TAR_BLOCK));
    expect(await main(["--verify", cut])).toBe(3);
    await expect(readTarIndex(fileRead(bytes.subarray(0, 3 * TAR_BLOCK)), 3 * TAR_BLOCK)).rejects.toThrow(/incomplete/);
  });

  test("refuses an incomplete base, unknown ids and missing folders", async () => {
    expect(await main([game, "--out", outDir, "--language", "french", "--list"])).toBe(2);
    expect(await main([game, "--only", "nope"])).toBe(1);
    expect(await main([path.join(dir, "missing")])).toBe(1);
    expect(await main([outDir, "--list"])).toBe(2);
    expect(await main([])).toBe(1);
  });

  test("system tar reads the packages", async () => {
    const r = spawnSync("tar", ["-tf", pkgPath("zombie_pentagon")], { encoding: "utf8" });
    if (r.error) return; // no tar on this machine
    expect(r.status).toBe(0);
    expect(r.stdout.trim().split("\n")).toEqual([
      "bo1-package.json",
      "zone/Common/zombie_pentagon.ff",
      "zone/English/en_zombie_pentagon.ff",
      "bo1-package.sha256",
    ]);
  });

  test("the written size is what the header chain says", async () => {
    const fh = await open(pkgPath("base"), "r");
    try {
      const size = (await fh.stat()).size;
      const read: ReadAt = async (off, len) => {
        const b = Buffer.alloc(len);
        const { bytesRead } = await fh.read(b, 0, len, off);
        return new Uint8Array(b.buffer, b.byteOffset, bytesRead);
      };
      const entries = await readTarIndex(read, size);
      const last = entries[entries.length - 1]!;
      expect(last.path).toBe("bo1-package.sha256");
      expect(size - (last.offset + last.size + ((TAR_BLOCK - (last.size % TAR_BLOCK)) % TAR_BLOCK))).toBe(2 * TAR_BLOCK);
    } finally {
      await fh.close();
    }
  });
});
