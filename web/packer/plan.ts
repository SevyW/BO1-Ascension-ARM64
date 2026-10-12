// plan.ts - which of an install's files go into which package (pure; tested in web/test/packer.test.ts).
//
// The rules are the importer's (client/src/assets/manifest.ts): the same root detection and classification, so a
// package holds exactly what the folder importer would have copied for that part of the game.
//   base      localization.txt, every core and optional zone (zone/Common), their localized copies in the chosen
//             language, main/iw_*.iwd, main/localized_<language>_iwd*.iwd
//   <map>     zone/Common/<map>.ff and <map>_patch.ff, their localized copies in the chosen language
// Files of other languages, cinematics and everything Zombies does not use are left out.

import {
  BASE_PACKAGE_ID,
  encodeManifest,
  PACKAGE_FORMAT,
  PACKAGE_VERSION,
  packageFileName,
  tarArchiveBytes,
  type PackageKind,
  type PackageManifest,
} from "../shared/gamepack";
import { MAPS } from "../shared/roster";
import {
  computeAvailability,
  findLanguage,
  LANGUAGES,
  matchSelection,
  type Language,
  type MatchedFile,
  type PickedFile,
} from "../client/src/assets/manifest";

export interface PlannedFile {
  source: string; // as given in the input (relative to the scanned folder's parent)
  path: string; // inside the package (the importer's destination path)
  size: number;
}

export interface PlannedPackage {
  id: string;
  kind: PackageKind;
  map: string | null;
  language: string;
  fileName: string;
  files: PlannedFile[];
  dataBytes: number;
}

export interface PackagePlan {
  root: string | null; // the install root inside the scanned folder (manifest.ts detectRoot)
  language: Language | null;
  languagesFound: string[];
  base: PlannedPackage | null; // null when nothing of the base was found
  coreMissing: string[]; // required base files that are missing (the base package would not start a game)
  maps: PlannedPackage[];
  incompleteMaps: { id: string; missing: string[] }[]; // a map's zone was found but not all of its required files
  ignored: number;
}

export interface PlanOptions {
  language?: string | null; // overrides localization.txt
  localizationText?: string | null; // the install's localization.txt, when found
}

function byPath(a: PlannedFile, b: PlannedFile): number {
  const rank = (p: string) => (p.toLowerCase() === "localization.txt" ? 0 : p.toLowerCase().startsWith("zone/") ? 1 : 2);
  const x = a.path.toLowerCase();
  const y = b.path.toLowerCase();
  return rank(a.path) - rank(b.path) || (x < y ? -1 : x > y ? 1 : 0);
}

function pkg(id: string, kind: PackageKind, map: string | null, lang: Language, files: MatchedFile[]): PlannedPackage {
  const planned = files.map((f) => ({ source: f.source, path: f.dest, size: f.size })).sort(byPath);
  return {
    id,
    kind,
    map,
    language: lang.name,
    fileName: packageFileName(id, lang.name),
    files: planned,
    dataBytes: planned.reduce((n, f) => n + f.size, 0),
  };
}

/** The language to package: the override, else localization.txt, else english when present, else the first found. */
export function chooseLanguage(found: string[], opts: PlanOptions): Language | null {
  if (opts.language) return findLanguage(opts.language) ?? null;
  const fromFile = opts.localizationText?.split(/\r?\n/, 1)[0]?.trim();
  const loc = findLanguage(fromFile);
  if (loc) return loc;
  if (found.includes("english")) return findLanguage("english") ?? null;
  return findLanguage(found[0]) ?? null;
}

/**
 * Plans the packages for a scanned install. `files` uses the folder picker's convention: paths relative to the
 * scanned folder's parent, starting with the folder's own name ("Call of Duty Black Ops/zone/Common/patch.ff").
 */
export function planPackages(files: PickedFile[], opts: PlanOptions = {}): PackagePlan {
  const m = matchSelection(files);
  const found = [...new Set(m.matched.map((f) => f.info.language).filter((l): l is string => !!l))].sort();
  const lang = chooseLanguage(found, opts);
  const plan: PackagePlan = {
    root: m.root,
    language: lang,
    languagesFound: found,
    base: null,
    coreMissing: [],
    maps: [],
    incompleteMaps: [],
    ignored: m.ignored,
  };
  if (!lang) {
    plan.coreMissing = ["a language (no localization.txt and no localized zones found)"];
    return plan;
  }
  const inLanguage = m.matched.filter((f) => !f.info.localized || f.info.language === lang.name);
  const baseFiles = inLanguage.filter((f) => f.info.kind !== "map-zone" && f.info.kind !== "video");
  if (baseFiles.length > 0) plan.base = pkg(BASE_PACKAGE_ID, "base", null, lang, baseFiles);

  const av = computeAvailability(
    inLanguage.map((f) => ({ path: f.dest, size: f.size })),
    lang.name,
  );
  plan.coreMissing = av.core.missing;
  for (const map of MAPS) {
    const mapFiles = inLanguage.filter((f) => f.info.kind === "map-zone" && f.info.map === map.id);
    if (mapFiles.length === 0) continue;
    const a = av.maps[map.id];
    if (a && !a.available) {
      plan.incompleteMaps.push({ id: map.id, missing: a.missing });
      continue;
    }
    plan.maps.push(pkg(map.id, "map", map.id, lang, mapFiles));
  }
  return plan;
}

/** The picked path of the install's localization.txt (the one at the detected root), when the plan found it. */
export function localizationSource(plan: PackagePlan): string | null {
  return plan.base?.files.find((f) => f.path.toLowerCase() === "localization.txt")?.source ?? null;
}

/** The manifest of a planned package. `tool` names the writer: "bo1-pack 1" (cli.ts), "bo1-pack 1 (web)" (the page). */
export function manifestFor(p: PlannedPackage, tool: string, created = new Date().toISOString()): PackageManifest {
  return {
    format: PACKAGE_FORMAT,
    version: PACKAGE_VERSION,
    id: p.id,
    kind: p.kind,
    map: p.map,
    language: p.language,
    created,
    tool,
    files: p.files.map((f) => ({ path: f.path, size: f.size })),
  };
}

/** Bytes of the checksum entry: "<64 hex digits>  <path>\n" per file (package paths are ASCII). */
export function checksumBytes(p: PlannedPackage): number {
  return p.files.reduce((n, f) => n + 64 + 2 + f.path.length + 1, 0);
}

/** The package's size: manifest, game files and checksums, each with header and padding, plus the trailer. */
export function archiveBytes(p: PlannedPackage, manifest: PackageManifest): number {
  return tarArchiveBytes([encodeManifest(manifest).length, ...p.files.map((f) => f.size), checksumBytes(p)]);
}

/** Every package id the planner can produce, for argument checking. */
export function knownPackageIds(): string[] {
  return [BASE_PACKAGE_ID, ...MAPS.map((m) => m.id)];
}

export function knownLanguages(): string[] {
  return LANGUAGES.map((l) => l.name);
}
