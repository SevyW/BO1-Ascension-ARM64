import { describe, expect, test } from "bun:test";
import { createHash } from "node:crypto";
import { ImportSession, openPackage, type SourceFile } from "../client/src/assets/import";
import { canPlayMap, CORE_LOCALIZED_REQUIRED, CORE_ZONES, playableMaps } from "../client/src/assets/manifest";
import {
  BASE_PACKAGE_ID,
  CHECKSUMS_NAME,
  encodeManifest,
  encodeTarHeader,
  formatChecksums,
  MANIFEST_NAME,
  PACKAGE_FORMAT,
  PACKAGE_VERSION,
  packageDataBytes,
  PackageError,
  packageFileName,
  TAR_TRAILER,
  tarPadding,
  type PackageManifest,
} from "../shared/gamepack";

interface FakeFile {
  path: string;
  data: Uint8Array;
}

/** Tiny fake content that differs per path and size. */
function fake(path: string, size: number): FakeFile {
  const data = new Uint8Array(size);
  for (let i = 0; i < size; i++) data[i] = (path.charCodeAt(i % path.length) + i) & 0xff;
  return { path, data };
}

function text(path: string, s: string): FakeFile {
  return { path, data: new TextEncoder().encode(s) };
}

/** gamepack.ts returns Uint8Array<ArrayBufferLike>; Blob parts want an ArrayBuffer-backed view. */
function part(u: Uint8Array): BlobPart {
  return u as Uint8Array<ArrayBuffer>;
}

/** A tar of raw entries (header, data, padding each) plus the trailer. */
function tarParts(entries: FakeFile[]): BlobPart[] {
  const parts: BlobPart[] = [];
  for (const e of entries) parts.push(part(encodeTarHeader({ path: e.path, size: e.data.length })), part(e.data), new Uint8Array(tarPadding(e.data.length)));
  parts.push(new Uint8Array(TAR_TRAILER));
  return parts;
}

/** A package as bo1-pack writes it: manifest first, the game files, the checksums last. */
function buildPackage(id: string, language: string, files: FakeFile[]): File {
  const manifest: PackageManifest = {
    format: PACKAGE_FORMAT,
    version: PACKAGE_VERSION,
    id,
    kind: id === BASE_PACKAGE_ID ? "base" : "map",
    map: id === BASE_PACKAGE_ID ? null : id,
    language,
    created: "2026-10-08T00:00:00.000Z",
    tool: "test",
    files: files.map((f) => ({ path: f.path, size: f.data.length })),
  };
  const sums = files.map((f) => ({ path: f.path, sha256: createHash("sha256").update(f.data).digest("hex") }));
  const parts = tarParts([{ path: MANIFEST_NAME, data: encodeManifest(manifest) }, ...files, { path: CHECKSUMS_NAME, data: formatChecksums(sums) }]);
  return new File(parts, packageFileName(id, language));
}

const LANG = { english: { folder: "English", prefix: "en_" }, french: { folder: "French", prefix: "fr_" } } as const;

function baseFiles(language: keyof typeof LANG = "english"): FakeFile[] {
  const l = LANG[language];
  const files = [text("localization.txt", `${language}\r\nREFERENCE x\n`)];
  CORE_ZONES.forEach((z, i) => files.push(fake(`zone/Common/${z}.ff`, 100 + i)));
  CORE_LOCALIZED_REQUIRED.forEach((z, i) => files.push(fake(`zone/${l.folder}/${l.prefix}${z}.ff`, 50 + i)));
  files.push(fake("main/iw_00.iwd", 700), fake(`main/localized_${language}_iwd00.iwd`, 300));
  return files;
}

function mapFiles(map: string, language: keyof typeof LANG = "english"): FakeFile[] {
  const l = LANG[language];
  return [fake(`zone/Common/${map}.ff`, 1500), fake(`zone/Common/${map}_patch.ff`, 513), fake(`zone/${l.folder}/${l.prefix}${map}.ff`, 512)];
}

/** What a picked file looks like to the session (path relative to the selection). */
function source(path: string, blob: Blob): SourceFile {
  return { path, size: blob.size, file: async () => blob };
}

async function bytes(b: Blob): Promise<Uint8Array> {
  return new Uint8Array(await b.arrayBuffer());
}

describe("openPackage", () => {
  test("lists the manifest's files; each source is the file's bytes", async () => {
    const files = mapFiles("zombie_theater");
    const tar = buildPackage("zombie_theater", "english", files);
    const pkg = await openPackage(tar);
    expect(pkg.name).toBe("bo1-zombies-zombie_theater-english.tar");
    expect(pkg.bytes).toBe(tar.size);
    expect(pkg.manifest.kind).toBe("map");
    expect(pkg.manifest.map).toBe("zombie_theater");
    expect(packageDataBytes(pkg.manifest)).toBe(1500 + 513 + 512);
    expect(pkg.sources.map((s) => [s.path, s.size])).toEqual(files.map((f) => [f.path, f.data.length]));
    for (const [i, s] of pkg.sources.entries()) {
      const b = await s.file();
      expect(b.size).toBe(s.size);
      expect(await bytes(b)).toEqual(files[i]!.data);
    }
  });

  test("a name can be given for a Blob without one", async () => {
    const tar = buildPackage(BASE_PACKAGE_ID, "english", baseFiles());
    const pkg = await openPackage(new Blob([tar]), "renamed.tar");
    expect(pkg.name).toBe("renamed.tar");
    expect(pkg.manifest.kind).toBe("base");
  });

  test("a truncated package is an error", async () => {
    const tar = buildPackage("zombie_theater", "english", mapFiles("zombie_theater"));
    // cut inside the last game file's data
    await expect(openPackage(tar.slice(0, tar.size - TAR_TRAILER - 1500))).rejects.toThrow(PackageError);
    await expect(openPackage(tar.slice(0, tar.size - TAR_TRAILER - 1500))).rejects.toThrow(/incomplete/);
    // cut inside a header
    await expect(openPackage(tar.slice(0, 700))).rejects.toThrow(/incomplete/);
  });

  test("a tar that is not a package, and a file that is not a tar", async () => {
    const plain = new File(tarParts([text("readme.txt", "hello"), fake("zone/Common/patch.ff", 10)]), "backup.tar");
    await expect(openPackage(plain)).rejects.toThrow(PackageError);
    await expect(openPackage(plain)).rejects.toThrow(/not a BO1 Zombies package/);
    const noise = new File([new Uint8Array(2048).fill(0x41)], "noise.tar");
    await expect(openPackage(noise)).rejects.toThrow(/not a BO1 Zombies package/);
  });
});

describe("ImportSession with packages", () => {
  const base = buildPackage(BASE_PACKAGE_ID, "english", baseFiles());
  const theater = buildPackage("zombie_theater", "english", mapFiles("zombie_theater"));

  test("base + map: each package is a batch at the game root; the map becomes playable", async () => {
    const s = new ImportSession();
    expect(await s.addPackages([source(base.name, base), source(theater.name, theater)])).toBe(2);
    expect(s.empty).toBe(false);
    expect(s.packages.map((p) => p.manifest.id)).toEqual([BASE_PACKAGE_ID, "zombie_theater"]);
    expect(s.packageErrors).toEqual([]);
    const plan = s.plan();
    const expected = [...baseFiles(), ...mapFiles("zombie_theater")];
    expect(plan.selected.map((f) => f.dest).sort()).toEqual(expected.map((f) => f.path).sort());
    expect(plan.copy).toHaveLength(expected.length);
    expect(plan.copyBytes).toBe(expected.reduce((n, f) => n + f.data.length, 0));
    expect(s.roots).toEqual([]); // no folder pick
    expect(s.ignored).toBe(0);
    const language = await s.pickedLanguage(plan.selected);
    expect(language).toBe("english");
    const av = s.projected(language, plan.selected);
    expect(av.core.complete).toBe(true);
    expect(canPlayMap(av, "zombie_theater")).toBe(true);
    expect(playableMaps(av)).toEqual(["zombie_theater"]);
    // the selected source is the package slice
    const zone = plan.selected.find((f) => f.dest === "zone/English/en_zombie_theater.ff")!;
    expect(await bytes(await zone.src.file())).toEqual(mapFiles("zombie_theater")[2]!.data);
    expect(s.languageConflicts(language)).toEqual([]);
  });

  test("a map package alone needs the core; with the base already imported it is playable", async () => {
    const s = new ImportSession();
    await s.addPackages([source(theater.name, theater)]);
    expect(await s.pickedLanguage()).toBeNull();
    expect(s.projected("english").core.complete).toBe(false);
    for (const f of baseFiles()) s.existing.set(f.path.toLowerCase(), f.data.length);
    expect(canPlayMap(s.projected("english"), "zombie_theater")).toBe(true);
    // already imported files with the same size are skipped (resume)
    const both = new ImportSession();
    await both.addPackages([source(base.name, base), source(theater.name, theater)]);
    both.existing = new Map(s.existing);
    const plan = both.plan();
    expect(plan.skip).toHaveLength(baseFiles().length);
    expect(plan.copy.map((f) => f.dest).sort()).toEqual(mapFiles("zombie_theater").map((f) => f.path).sort());
  });

  test("a bad package is reported and the others are added; the same package twice is added once", async () => {
    const s = new ImportSession();
    const cut = new File([theater.slice(0, theater.size - 3000)], theater.name);
    const added = await s.addPackages([source(`packs/${cut.name}`, cut), source(base.name, base), source(base.name, base)]);
    expect(added).toBe(1);
    expect(s.packages.map((p) => p.manifest.id)).toEqual([BASE_PACKAGE_ID]);
    expect(s.packageErrors).toHaveLength(1);
    expect(s.packageErrors[0]!.name).toBe(theater.name);
    expect(s.packageErrors[0]!.message).toMatch(/incomplete/);
    // the next add replaces the errors
    await s.addPackages([source(theater.name, theater)]);
    expect(s.packageErrors).toEqual([]);
    expect(s.packages).toHaveLength(2);
    s.clear();
    expect(s.empty).toBe(true);
    expect(s.packages).toEqual([]);
  });

  test("a folder pick with loose files and packages: one folder batch plus one batch per package", async () => {
    const s = new ImportSession();
    const loose = baseFiles().map((f) => source(`Call of Duty Black Ops/${f.path}`, new Blob([part(f.data)])));
    // the folder has its own copy of the map's Common zone with another size: the first pick's file wins
    const own = fake("zone/Common/zombie_theater.ff", 99);
    loose.push(source(`Call of Duty Black Ops/${own.path}`, new Blob([part(own.data)])));
    loose.push(source("Call of Duty Black Ops/BlackOps.exe", new Blob(["MZ"])));
    const notPkg = new File(tarParts([text("notes.txt", "x")]), "old-saves.tar");
    await s.addPicked([
      ...loose,
      source(`Call of Duty Black Ops/packages/${theater.name}`, theater),
      source(`Call of Duty Black Ops/packages/${notPkg.name}`, notPkg),
    ]);
    expect(s.roots).toEqual([]); // computed by selected()
    const plan = s.plan();
    expect(s.roots).toEqual(["Call of Duty Black Ops"]);
    expect(s.packages.map((p) => p.manifest.id)).toEqual(["zombie_theater"]);
    expect(s.packageErrors.map((e) => e.name)).toEqual(["old-saves.tar"]);
    expect(s.ignored).toBe(1); // BlackOps.exe
    const common = plan.selected.filter((f) => f.dest.toLowerCase() === "zone/common/zombie_theater.ff");
    expect(common).toHaveLength(1);
    expect(common[0]!.size).toBe(99);
    expect(plan.selected.some((f) => f.dest === "zone/English/en_zombie_theater.ff")).toBe(true);
    const language = await s.pickedLanguage(plan.selected);
    expect(language).toBe("english");
    expect(canPlayMap(s.projected(language, plan.selected), "zombie_theater")).toBe(true);
  });

  test("a folder pick with only packages adds no folder batch", async () => {
    const s = new ImportSession();
    await s.addPicked([source(`bo1-packages/${base.name}`, base), source(`bo1-packages/${theater.name}`, theater)]);
    s.plan();
    expect(s.roots).toEqual([]);
    expect(s.packages).toHaveLength(2);
    // an empty pick still counts as a pick (the page says nothing was found)
    const empty = new ImportSession();
    await empty.addPicked([]);
    expect(empty.empty).toBe(false);
    expect(empty.plan().selected).toEqual([]);
  });

  test("language: packages from another language are flagged", async () => {
    const french = buildPackage("zombie_moon", "french", mapFiles("zombie_moon", "french"));
    const s = new ImportSession();
    await s.addPackages([source(base.name, base), source(theater.name, theater), source(french.name, french)]);
    const language = await s.pickedLanguage();
    expect(language).toBe("english");
    expect(s.languageConflicts(language).map((p) => p.manifest.id)).toEqual(["zombie_moon"]);
    expect(s.languageConflicts(null)).toEqual([]);
    const av = s.projected(language);
    expect(canPlayMap(av, "zombie_theater")).toBe(true);
    expect(canPlayMap(av, "zombie_moon")).toBe(false); // its zones are fr_, the engine loads en_
  });
});
