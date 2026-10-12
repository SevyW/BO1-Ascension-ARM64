// packview.ts - the "Make packages" card on the Game files screen (pack.ts does the work). Like the import, the job
// outlives the screen: leaving "Game files" keeps it going, and coming back shows it.

import { archiveBytes, manifestFor, type PackagePlan, type PlannedPackage } from "../../../packer/plan";
import { MAPS } from "../../../shared/roster";
import { nav } from "../input/nav";
import { clear, h, put, toast } from "../ui/dom";
import { icons } from "../ui/icons";
import { bar, duration } from "../ui/progress";
import { pickWithDirectoryPicker, sourcesFromFileList, supportsDirectoryPicker, type SourceFile } from "./import";
import { formatBytes } from "./manifest";
import { makePackages, PACK_TOOL, pickOutputFolder, planFromSources, supportsFolderOutput, type PackProgress, type PackResult, type PackTarget } from "./pack";

interface Picked {
  files: SourceFile[];
  byPath: Map<string, SourceFile>;
  plan: PackagePlan;
}

let picked: Picked | null = null;
let chosen = new Set<string>();
let scanning: string | null = null;
let running: AbortController | null = null;
let progress: PackProgress | null = null;
let results: (PackResult & { url?: string })[] = [];
let message: { text: string; warn: boolean } | null = null;
let mounted: { render(): void; progress(): void } | null = null;

export function packRunning(): boolean {
  return running !== null;
}

function allPackages(plan: PackagePlan): PlannedPackage[] {
  return [...(plan.base ? [plan.base] : []), ...plan.maps];
}

/** Every package, except a base package that would not start a game (bo1-pack refuses to write that one too). */
function defaultChoice(plan: PackagePlan, previous: Set<string> | null = null): Set<string> {
  const ids = allPackages(plan)
    .filter((p) => p.kind !== "base" || plan.coreMissing.length === 0)
    .map((p) => p.id);
  return new Set(previous ? ids.filter((id) => previous.has(id)) : ids);
}

function packageLabel(p: PlannedPackage): string {
  return p.kind === "base" ? "Base files (every map needs them)" : MAPS.find((m) => m.id === p.map)?.name ?? p.id;
}

function mapName(id: string): string {
  return MAPS.find((m) => m.id === id)?.name ?? id;
}

function sizeOf(p: PlannedPackage): number {
  return archiveBytes(p, manifestFor(p, PACK_TOOL));
}

function forgetResults(): void {
  for (const r of results) if (r.url) URL.revokeObjectURL(r.url);
  results = [];
}

async function adopt(files: SourceFile[], language: string | null = null): Promise<void> {
  const { plan } = await planFromSources(files, language);
  const previous = picked && language ? chosen : null;
  picked = { files, byPath: new Map(files.map((f) => [f.path, f])), plan };
  chosen = defaultChoice(plan, previous);
  if (!language) {
    forgetResults();
    message = null;
  }
}

async function start(): Promise<void> {
  if (!picked || running) return;
  const packages = allPackages(picked.plan).filter((p) => chosen.has(p.id));
  if (packages.length === 0) return;
  let target: PackTarget = { kind: "download" };
  if (supportsFolderOutput()) {
    let dir: FileSystemDirectoryHandle | null;
    try {
      dir = await pickOutputFolder();
    } catch (e) {
      toast(`Could not open that folder: ${e instanceof Error ? e.message : String(e)}`, "error");
      return;
    }
    if (!dir) return;
    target = { kind: "folder", dir };
  }
  forgetResults();
  message = null;
  running = new AbortController();
  progress = null;
  mounted?.render();
  const outcome = await makePackages(packages, picked.byPath, target, (p) => {
    progress = p;
    mounted?.progress();
  }, running.signal);
  running = null;
  progress = null;
  results = outcome.results.map((r) => (r.blob ? { ...r, url: URL.createObjectURL(r.blob) } : r));
  const n = (k: PackResult["status"]) => results.filter((r) => r.status === k).length;
  const plural = (c: number, word: string) => `${c} ${word}${c === 1 ? "" : "s"}`;
  if (outcome.error) {
    message = { text: `Stopped: ${outcome.error}`, warn: true };
    toast(message.text, "error", 8000);
  } else if (outcome.cancelled) {
    message = { text: "Cancelled. The packages listed below are complete; start again to make the rest.", warn: true };
    toast("Packaging cancelled.", "warn");
  } else if (target.kind === "folder") {
    const up = n("up-to-date");
    const written = n("written");
    const what = written === 0 ? `All ${plural(up, "package")} in the folder "${target.dir.name}" are up to date.` : `${plural(written, "package")} written to the folder "${target.dir.name}"${up ? `, ${up} already up to date` : ""}.`;
    message = { text: `${what} Import them with "Add packages" in the browser you want to play in.`, warn: false };
    toast(`${plural(results.length, "package")} in "${target.dir.name}".`, "success");
  } else {
    message = { text: `${plural(n("ready"), "package")} ready. Save each one with its button; the buttons work while this page stays open.`, warn: false };
    toast("Packages ready to save.", "success");
  }
  mounted?.render();
}

export function packCard(): { el: HTMLElement; unmount(): void } {
  const box = h("div", { class: "card", "data-testid": "packer" });
  const progressBox = h("div", { class: "progress-box", "data-testid": "pack-progress" });
  const input = h("input", { type: "file", multiple: true, hidden: true, "data-testid": "pack-folder-input" });
  input.setAttribute("webkitdirectory", "");
  input.setAttribute("directory", "");
  input.addEventListener("change", () => {
    if (!input.files || input.files.length === 0 || running) return;
    const files = sourcesFromFileList(input.files);
    input.value = "";
    void adopt(files).then(render, (e: unknown) => toast(`Could not read the folder: ${e instanceof Error ? e.message : String(e)}`, "error"));
  });

  const pick = async () => {
    if (running) return;
    if (!supportsDirectoryPicker()) {
      input.click();
      return;
    }
    try {
      scanning = "Scanning the folder…";
      render();
      const files = await pickWithDirectoryPicker((f, dirs) => {
        scanning = `Scanning… ${f} files in ${dirs} folders`;
        render();
      });
      if (files) await adopt(files);
    } catch (e) {
      toast(`Could not read the folder: ${e instanceof Error ? e.message : String(e)}`, "error");
    }
    scanning = null;
    render();
  };

  const renderProgress = () => {
    clear(progressBox);
    const p = progress;
    if (!p || p.phase === "checking") {
      put(progressBox, h("p", { class: "muted" }, p ? "Looking for packages that are already in the folder…" : "Starting…"));
      return;
    }
    const eta = p.bytesPerSec > 0 ? duration((p.bytesTotal - p.bytesDone) / p.bytesPerSec) : "";
    put(progressBox,
      h("p", null, `Package ${p.pkgIndex + 1} of ${p.pkgCount}: `, h("code", null, p.pkg ?? "")),
      h("p", { class: "small muted" }, p.phase === "hashing" ? ["Checking ", h("code", null, p.current ?? ""), " (sha256)"] : "Writing it into the folder"),
      h("p", null, `${formatBytes(p.bytesDone)} of ${formatBytes(p.bytesTotal)}`, p.bytesPerSec ? ` · ${formatBytes(p.bytesPerSec)}/s` : "", eta ? ` · about ${eta} left` : ""),
      bar(p.bytesTotal ? p.bytesDone / p.bytesTotal : 1, "pack-overall"),
    );
  };

  const planView = (pk: Picked): HTMLElement => {
    const plan = pk.plan;
    const packages = allPackages(plan);
    if (plan.root === null || packages.length === 0) {
      return h("p", { class: "warn", "data-testid": "pack-plan" }, "No Zombies files in that folder. Pick the folder that contains zone/ and main/.");
    }
    let languageControl: HTMLElement | string = plan.language?.name ?? "unknown";
    if (plan.languagesFound.length > 1) {
      const sel = h(
        "select",
        { "data-testid": "pack-language", "data-nav-id": "pack-language", "aria-label": "Language", class: "inline-select" },
        plan.languagesFound.map((l) => h("option", { value: l, selected: l === plan.language?.name }, l)),
      );
      sel.addEventListener("change", () => void adopt(pk.files, sel.value).then(render));
      languageControl = sel;
    }
    const options = packages.map((p) => {
      const disabled = p.kind === "base" && plan.coreMissing.length > 0;
      const id = `pack-${p.id}`;
      const cb = h("input", { type: "checkbox", id, checked: chosen.has(p.id), disabled, "data-testid": `pack-option-${p.id}` });
      cb.addEventListener("change", () => {
        if (cb.checked) chosen.add(p.id);
        else chosen.delete(p.id);
        render();
      });
      return h(
        "label",
        { class: ["toggle", disabled && "disabled"], for: id },
        cb,
        h("span", null, packageLabel(p), h("span", { class: "small muted" }, ` · ${formatBytes(sizeOf(p))} · ${p.files.length} files`)),
      );
    });
    const selected = packages.filter((p) => chosen.has(p.id));
    const total = selected.reduce((n, p) => n + sizeOf(p), 0);
    const folder = supportsFolderOutput();
    const count = `${selected.length} package${selected.length === 1 ? "" : "s"}`;
    return h(
      "div",
      { class: "scan-result", "data-testid": "pack-plan" },
      h("p", null, "Language: ", languageControl),
      plan.coreMissing.length > 0
        ? h("p", { class: "warn" }, `The base package would not start a game, so it is left out. Missing: ${plan.coreMissing.slice(0, 6).join(", ")}${plan.coreMissing.length > 6 ? `, …and ${plan.coreMissing.length - 6} more` : ""}`)
        : null,
      plan.incompleteMaps.length > 0
        ? h("p", { class: "muted small" }, `Left out, files missing: ${plan.incompleteMaps.map((m) => `${mapName(m.id)} (${m.missing.join(", ")})`).join("; ")}`)
        : null,
      options,
      h(
        "div",
        { class: "row" },
        h(
          "button",
          { class: "btn primary", "data-testid": "pack-start", "data-nav-id": "pack-start", disabled: selected.length === 0, onclick: () => void start() },
          folder ? `Save ${count} (${formatBytes(total)}) to a folder…` : `Prepare ${count} (${formatBytes(total)})`,
        ),
        h(
          "button",
          {
            class: "btn small",
            "data-nav-id": "pack-clear",
            onclick: () => {
              picked = null;
              forgetResults();
              message = null;
              render();
            },
          },
          "Clear",
        ),
      ),
      h(
        "p",
        { class: "muted small" },
        folder
          ? "The browser asks for a folder and for permission to save in it. Packages already there and up to date are skipped. Every file is checked (sha256) and then written."
          : "This browser cannot save into a folder, so each package becomes a download: the page checks every file (sha256), then shows a save button per package.",
      ),
    );
  };

  const resultsView = (): HTMLElement =>
    h(
      "ul",
      { class: "package-list", "data-testid": "pack-results" },
      results.map((r) =>
        h(
          "li",
          { "data-package": r.id },
          r.url
            ? h("a", { class: "btn small", href: r.url, download: r.fileName, "data-nav-id": `pack-save-${r.id}`, "data-testid": `pack-save-${r.id}` }, icons.download(), `Save ${r.fileName}`)
            : h("span", null, r.status === "written" ? icons.check() : null, h("code", null, r.fileName)),
          h("span", { class: "small muted" }, ` · ${formatBytes(r.bytes)}${r.status === "up-to-date" ? " · already up to date" : r.status === "written" ? " · written" : ""}`),
        ),
      ),
    );

  const render = () => {
    const focusKey = nav.focusKey();
    clear(box);
    put(box,
      h("h2", null, "Make packages"),
      input,
      h(
        "p",
        { class: "muted small" },
        "Turns your install into the packages ",
        h("code", null, "bo1-pack.mjs"),
        " makes: one with the files every map needs, one per map. Use them to import in another browser or on another computer. To play in this browser, import the folder instead; that needs no packages. Nothing is uploaded.",
      ),
    );
    if (running) {
      renderProgress();
      put(box,
        progressBox,
        h("button", { class: "btn danger", "data-testid": "pack-cancel", "data-nav-id": "pack-cancel", onclick: () => running?.abort() }, "Cancel"),
      );
      nav.restoreFocus(focusKey);
      return;
    }
    put(box,
      h(
        "div",
        { class: "row" },
        h(
          "button",
          { class: "btn", "data-testid": "pack-pick-folder", "data-nav-id": "pack-pick-folder", onclick: () => void pick() },
          icons.folder(),
          picked ? "Pick another folder" : "Choose your Black Ops folder",
        ),
        supportsDirectoryPicker() ? h("button", { class: "btn small", "data-nav-id": "pack-pick-fallback", onclick: () => input.click() }, "Use the browser's upload dialog") : null,
      ),
      scanning ? h("p", { class: "muted" }, scanning) : null,
      picked && !scanning ? planView(picked) : null,
      message ? h("p", { class: ["small", message.warn && "warn"], "data-testid": "pack-message" }, message.text) : null,
      results.length > 0 ? resultsView() : null,
    );
    nav.restoreFocus(focusKey);
  };

  mounted = { render, progress: renderProgress };
  render();
  return {
    el: box,
    unmount() {
      mounted = null; // a running job keeps going
    },
  };
}
