// filesview.ts - the "Game files" screen: import the player's own files into the browser, see what is there,
// delete everything. Nothing is uploaded: files are copied into this site's private browser storage (OPFS).

import { looksLikePackageName, PACKER_PATH } from "../../../shared/gamepack";
import { MAPS } from "../../../shared/roster";
import type { App } from "../app";
import { nav } from "../input/nav";
import { clear, confirmDialog, h, put, toast } from "../ui/dom";
import { icons } from "../ui/icons";
import { bar, duration } from "../ui/progress";
import { Disposer, type ScreenInstance } from "../ui/screen";
import {
  deleteAllImported,
  importedMapBytes,
  ImportSession,
  pickWithDirectoryPicker,
  rebuildIndex,
  removeImportedMap,
  sourcesFromFileList,
  supportsDirectoryPicker,
  type ImportProgress,
  type OpenedPackage,
} from "./import";
import { formatBytes, type Availability } from "./manifest";
import { opfsAvailable, requestPersist, storageInfo, type StorageInfo } from "./opfs";
import { packCard } from "./packview";

export interface AvailabilityOptions {
  title?: string;
  mapBytes?: Record<string, number>; // imported bytes per map: with onRemove, each imported map gets a Remove button
  onRemove?: (mapId: string) => void;
  busy?: boolean;
}

export function availabilityView(av: Availability | null, opts: AvailabilityOptions = {}): HTMLElement {
  const box = h("div", { class: "availability" });
  if (opts.title) put(box, h("h3", null, opts.title));
  if (!av || av.files === 0) {
    put(box, h("p", { class: "muted" }, "Nothing imported yet."));
    return box;
  }
  put(box,
    h(
      "p",
      { class: av.core.complete ? "ok" : "warn", "data-testid": "core-status" },
      av.core.complete ? icons.check() : icons.cross(),
      av.core.complete ? "Core files complete" : `Core files incomplete: ${av.core.missing.length} missing`,
      av.language ? h("span", { class: "muted" }, ` · language: ${av.language.name}`) : null,
    ),
  );
  if (!av.core.complete) {
    put(box, h("ul", { class: "missing-list" }, av.core.missing.slice(0, 12).map((m) => h("li", null, h("code", null, m)))));
    if (av.core.missing.length > 12) put(box, h("p", { class: "muted small" }, `…and ${av.core.missing.length - 12} more`));
  }
  put(box,
    h(
      "ul",
      { class: "map-list", "data-testid": "map-availability" },
      MAPS.map((m) => {
        const a = av.maps[m.id];
        const playable = av.core.complete && !!a?.available;
        const bytes = opts.mapBytes?.[m.id] ?? 0;
        const remove = opts.onRemove;
        return h(
          "li",
          { class: playable ? "ok" : a?.available ? "warn" : "muted", "data-map": m.id },
          playable ? icons.check() : icons.cross(),
          h("span", null, m.name),
          !a?.available ? h("span", { class: "small muted" }, " · map files missing") : !playable ? h("span", { class: "small" }, " · needs the core files") : null,
          remove && bytes > 0
            ? [
                h("span", { class: "small muted" }, ` · ${formatBytes(bytes)}`),
                h(
                  "button",
                  { class: "btn small map-remove", "data-testid": `remove-map-${m.id}`, "data-nav-id": `remove-map-${m.id}`, disabled: opts.busy, onclick: () => remove(m.id) },
                  "Remove",
                ),
              ]
            : null,
        );
      }),
    ),
  );
  return box;
}

// The import outlives the screen: leaving "Game files" while copying keeps the copy going, and coming back shows it.
const session = new ImportSession();
let running: AbortController | null = null;
let progress: ImportProgress | null = null;
let lastResult: string | null = null;
let mounted: { progress(): void; all(): void } | null = null;

export function importRunning(): boolean {
  return running !== null;
}

export function mountFiles(host: HTMLElement, app: App): ScreenInstance {
  const d = new Disposer();
  let storage: StorageInfo = { usage: null, quota: null, persisted: null };
  let scanning: string | null = null;

  const manageBox = h("div", { class: "card", "data-testid": "imported-files" });
  const importBox = h("div", { class: "card", "data-testid": "importer" });
  const storageBox = h("div", { class: "card", "data-testid": "browser-storage" });
  const pack = packCard();

  const fileInput = h("input", { type: "file", multiple: true, hidden: true, "data-testid": "folder-input" });
  fileInput.setAttribute("webkitdirectory", "");
  fileInput.setAttribute("directory", "");
  fileInput.addEventListener("change", () => {
    if (!fileInput.files || fileInput.files.length === 0) return;
    const files = sourcesFromFileList(fileInput.files);
    fileInput.value = "";
    lastResult = null;
    void session.addPicked(files).then(() => {
      packageErrorToast();
      void renderImport();
    });
  });

  // game file packages (.tar from bo1-pack): the "Add packages" dialog or a drop onto the importer card
  const packageInput = h("input", { type: "file", multiple: true, accept: ".tar", hidden: true, "data-testid": "package-input" });
  packageInput.addEventListener("change", () => {
    if (!packageInput.files || packageInput.files.length === 0) return;
    const files = Array.from(packageInput.files);
    packageInput.value = "";
    void addPackageFiles(files);
  });

  const packageErrorToast = () => {
    const bad = session.packageErrors;
    if (bad.length > 0) toast(`Not a usable package: ${bad.map((e) => e.name).join(", ")}`, "warn", 6000);
  };

  const addPackageFiles = async (files: File[]) => {
    if (running) return;
    await session.addPackages(files.map((f) => ({ path: f.name, size: f.size, file: async () => f })));
    lastResult = null;
    packageErrorToast();
    void renderImport();
  };

  importBox.addEventListener("dragover", (e) => {
    if (!e.dataTransfer?.types.includes("Files")) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = running ? "none" : "copy";
    importBox.classList.add("drop-target");
  });
  importBox.addEventListener("dragleave", (e) => {
    if (e.relatedTarget instanceof Node && importBox.contains(e.relatedTarget)) return;
    importBox.classList.remove("drop-target");
  });
  importBox.addEventListener("drop", (e) => {
    importBox.classList.remove("drop-target");
    if (!e.dataTransfer?.types.includes("Files")) return;
    e.preventDefault();
    if (running) return;
    const files = Array.from(e.dataTransfer.files);
    const tars = files.filter((f) => looksLikePackageName(f.name));
    if (tars.length < files.length) toast("Only .tar packages can be dropped here. Use the folder button for an install folder.", "warn");
    if (tars.length > 0) void addPackageFiles(tars);
  });

  const pick = async () => {
    if (!supportsDirectoryPicker()) {
      fileInput.click();
      return;
    }
    try {
      scanning = "Scanning the folder…";
      void renderImport();
      const files = await pickWithDirectoryPicker((f, dirs) => {
        scanning = `Scanning… ${f} files in ${dirs} folders`;
        void renderImport();
      });
      scanning = null;
      if (files) {
        await session.addPicked(files);
        lastResult = null;
        packageErrorToast();
      }
    } catch (e) {
      scanning = null;
      toast(`Could not read the folder: ${e instanceof Error ? e.message : String(e)}`, "error");
    }
    void renderImport();
  };

  const startImport = async () => {
    if (running) return;
    running = new AbortController();
    progress = null;
    void renderImport();
    const outcome = await session.run((p) => {
      progress = p;
      mounted?.progress();
    }, running.signal);
    running = null;
    progress = null;
    app.setFiles(outcome.state);
    if (outcome.error) {
      lastResult = `Import stopped: ${outcome.error}`;
      toast(lastResult, "error", 8000);
    } else if (outcome.cancelled) {
      lastResult = `Import cancelled after ${outcome.copied} file(s). Start it again, or pick the same folder or packages again, to resume.`;
      toast("Import cancelled.", "warn");
    } else {
      lastResult = `Imported ${outcome.copied} file(s)${outcome.skipped ? `, ${outcome.skipped} already there` : ""}.`;
      toast(lastResult, "success");
      session.clear();
    }
    mounted?.all();
  };

  // ----- imported files -----
  const renderManage = () => {
    const focusKey = nav.focusKey();
    clear(manageBox);
    const av = app.files?.availability ?? null;
    put(manageBox,
      h("h2", null, "In this browser"),
      availabilityView(av, {
        mapBytes: app.files ? importedMapBytes(app.files.index) : undefined,
        busy: !!running,
        onRemove: async (mapId) => {
          const name = MAPS.find((m) => m.id === mapId)?.name ?? mapId;
          if (!(await confirmDialog(`Remove ${name}?`, "This deletes the map's files from this browser to free space. The files every map needs and the other maps stay. You can import it again later.", "Remove", "Keep"))) return;
          if (running) return;
          app.setFiles(await removeImportedMap(mapId));
          await session.init();
          storage = await storageInfo();
          renderStorage();
          renderManage();
          void renderImport();
          toast(`${name} removed.`, "info");
        },
      }),
      av && av.files > 0 ? h("p", { class: "muted small" }, `${av.files} files, ${formatBytes(av.bytes)}`) : null,
      h(
        "div",
        { class: "row" },
        h(
          "button",
          {
            class: "btn small",
            "data-nav-id": "rescan",
            "data-testid": "rescan-files",
            disabled: !!running || !opfsAvailable(),
            onclick: async () => {
              app.setFiles(await rebuildIndex());
              await session.init();
              toast("Storage rescanned.", "info", 2000);
              renderManage();
            },
          },
          "Rescan",
        ),
        h(
          "button",
          {
            class: "btn small danger",
            "data-nav-id": "delete-all",
            "data-testid": "delete-files",
            disabled: !!running || !av || av.files === 0,
            onclick: async () => {
              if (!(await confirmDialog("Delete all game files?", "This removes every imported file and the mods/data copies from this browser. You can import them again later.", "Delete", "Keep"))) return;
              await deleteAllImported();
              await session.init();
              await app.refreshFiles();
              storage = await storageInfo();
              renderStorage();
              renderManage();
              toast("Game files deleted.", "info");
            },
          },
          "Delete all",
        ),
      ),
    );
    nav.restoreFocus(focusKey);
  };

  // ----- importer -----
  const progressBox = h("div", { class: "progress-box", "data-testid": "import-progress" });
  const renderProgress = () => {
    clear(progressBox);
    const p = progress;
    if (!p) {
      put(progressBox, h("p", { class: "muted" }, "Starting…"));
      return;
    }
    const done = p.bytesDone + p.currentBytes;
    const remaining = p.bytesTotal - done;
    const eta = p.bytesPerSec > 0 ? duration(remaining / p.bytesPerSec) : "";
    put(progressBox,
      h("p", null, `${formatBytes(done)} of ${formatBytes(p.bytesTotal)} · file ${Math.min(p.filesDone + 1, p.filesTotal)} of ${p.filesTotal}`, p.bytesPerSec ? ` · ${formatBytes(p.bytesPerSec)}/s` : "", eta ? ` · about ${eta} left` : ""),
      bar(p.bytesTotal ? done / p.bytesTotal : 1, "import-overall"),
      p.current
        ? h("p", { class: "small muted" }, h("code", null, p.current), ` ${formatBytes(p.currentBytes)} / ${formatBytes(p.currentSize)}`)
        : null,
      p.current ? bar(p.currentSize ? p.currentBytes / p.currentSize : 1, "import-file") : null,
    );
  };

  let renderSeq = 0;
  const renderImport = async () => {
    // the async part first, so overlapping renders cannot interleave their DOM writes
    const seq = ++renderSeq;
    const pickedLanguage = !session.empty && !running ? await session.pickedLanguage() : null;
    if (seq !== renderSeq) return;
    const focusKey = nav.focusKey();
    clear(importBox);
    put(importBox, h("h2", null, "Import from your Black Ops install"), fileInput, packageInput);
    if (!opfsAvailable()) {
      put(importBox, h("p", { class: "warn" }, "This browser has no private file storage (OPFS), so game files cannot be imported. Private windows often disable it."));
      return;
    }
    if (running) {
      renderProgress();
      put(importBox,
        progressBox,
        h(
          "button",
          {
            class: "btn danger",
            "data-testid": "cancel-import",
            "data-nav-id": "cancel-import",
            "data-autofocus": true,
            onclick: () => running?.abort(),
          },
          "Cancel",
        ),
        h("p", { class: "muted small" }, "You can cancel at any time; picking the same folder or packages again resumes where it stopped."),
      );
      nav.restoreFocus(focusKey);
      if (nav.mode !== "mouse" && !nav.current()) nav.focusFirst(importBox);
      return;
    }
    const pickLabel = session.empty ? "Choose your Black Ops folder" : "Add another folder";
    put(importBox,
      h(
        "ol",
        { class: "steps" },
        h("li", null, "Pick the folder Black Ops is installed in (for Steam: ", h("code", null, "steamapps/common/Call of Duty Black Ops"), "). Its parent, or the ", h("code", null, "zone"), " and ", h("code", null, "main"), " folders one after the other, work too."),
        h("li", null, "Check what was found, then start the import. Large files take a while; you can cancel and resume."),
      ),
      h(
        "div",
        { class: "row" },
        h("button", { class: ["btn", session.empty && "primary"], "data-testid": "pick-folder", "data-nav-id": "pick-folder", onclick: () => void pick() }, icons.folder(), pickLabel),
        supportsDirectoryPicker()
          ? h("button", { class: "btn small", "data-nav-id": "pick-fallback", onclick: () => fileInput.click() }, "Use the browser's upload dialog")
          : null,
      ),
      supportsDirectoryPicker()
        ? null
        : h("p", { class: "muted small" }, "Your browser will ask to \"upload\" the folder: that is its name for reading files. Nothing is sent anywhere; the files are copied into this browser only."),
    );
    // the language the import uses: a picked localization.txt (base package or folder), else the imported one
    const language = pickedLanguage ?? app.files?.language ?? null;
    const conflicts = new Set(session.languageConflicts(language));
    const packageName = (p: OpenedPackage) =>
      p.manifest.kind === "base" ? "Base files" : MAPS.find((m) => m.id === p.manifest.map)?.name ?? p.manifest.id;
    put(importBox,
      h(
        "div",
        { class: "packages", "data-testid": "packages" },
        h("h3", null, "Or add game file packages"),
        h(
          "p",
          { class: "muted small" },
          "A package is a .tar file made from your own install: ",
          h("code", null, "bo1-zombies-base-<language>.tar"),
          " with the files every map needs, and ",
          h("code", null, "bo1-zombies-<map>-<language>.tar"),
          " per map. Make them with \"Make packages\" on this screen, or on your computer with ",
          h("a", { href: PACKER_PATH, download: "bo1-pack.mjs", "data-nav-id": "packer-download", "data-testid": "packer-download" }, "bo1-pack.mjs"),
          " (",
          h("code", null, "node bo1-pack.mjs \"<your Black Ops folder>\""),
          "; ",
          h("code", null, "bun"),
          " works too). Add the base package and the maps you want, or drop them onto this card. A folder that holds packages can be picked as well.",
        ),
        h(
          "div",
          { class: "row" },
          h("button", { class: "btn", "data-testid": "pick-packages", "data-nav-id": "pick-packages", onclick: () => packageInput.click() }, icons.folder(), "Add packages"),
        ),
        session.packages.length > 0
          ? h(
              "ul",
              { class: "package-list", "data-testid": "package-list" },
              session.packages.map((p) =>
                h(
                  "li",
                  { "data-package": p.manifest.id },
                  h("span", null, packageName(p)),
                  h("span", { class: "small muted" }, ` · ${p.manifest.language} · ${formatBytes(p.bytes)} · `, h("code", null, p.name)),
                  conflicts.has(p)
                    ? h("span", { class: "small warn" }, ` · made from a ${p.manifest.language} install, but the import uses ${language}: its localized files will not count`)
                    : null,
                ),
              ),
            )
          : null,
        session.packageErrors.length > 0
          ? h(
              "ul",
              { class: "missing-list", "data-testid": "package-errors" },
              session.packageErrors.map((e) => h("li", { class: "warn" }, h("code", null, e.name), `: ${e.message}`)),
            )
          : null,
      ),
    );
    if (scanning) put(importBox, h("p", { class: "muted" }, scanning));
    if (lastResult) put(importBox, h("p", { class: "small" }, lastResult));
    if (!session.empty) {
      const plan = session.plan();
      const projected = session.projected(language, plan.selected);
      const free = storage.quota !== null && storage.usage !== null ? storage.quota - storage.usage : null;
      const videoCb = h("input", { type: "checkbox", id: "include-video", checked: session.includeVideo });
      videoCb.addEventListener("change", () => {
        session.includeVideo = videoCb.checked;
        void renderImport();
      });
      const roots = session.roots.map((r) => (r === null ? "(no zone/ or main/ folder found)" : r === "" ? "(the folder you picked)" : r));
      // maps can be left out to fit the browser's storage limit; the files every map needs always come along
      const choices = session.mapChoices(language);
      const baseCopy = plan.copy.filter((f) => f.info.kind !== "map-zone" && f.info.kind !== "video").reduce((n, f) => n + f.size, 0);
      const mapChoices =
        choices.length > 0
          ? h(
              "div",
              { class: "map-choices", "data-testid": "import-maps" },
              h("h3", null, "Maps to import"),
              baseCopy > 0 ? h("p", { class: "small muted" }, `The files every map needs come along: ${formatBytes(baseCopy)}.`) : null,
              choices.map((c) => {
                const id = `import-map-${c.id}`;
                const cb = h("input", { type: "checkbox", id, checked: c.selected, "data-testid": id });
                cb.addEventListener("change", () => {
                  session.setMapSelected(c.id, cb.checked);
                  void renderImport();
                });
                return h(
                  "label",
                  { class: "toggle", for: id },
                  cb,
                  h(
                    "span",
                    null,
                    MAPS.find((m) => m.id === c.id)?.name ?? c.id,
                    h("span", { class: "small muted" }, c.copyBytes === 0 ? " · in this browser already" : ` · ${formatBytes(c.copyBytes)}`),
                    c.missing.length > 0 ? h("span", { class: "small warn" }, ` · not playable, missing ${c.missing.join(", ")}`) : null,
                  ),
                );
              }),
              h("p", { class: "muted small" }, "Unchecked maps are not copied. Import them later by picking the folder or package again; remove maps you are done with under \"In this browser\"."),
            )
          : null;
      put(importBox,
        h(
          "div",
          { class: "scan-result", "data-testid": "scan-result" },
          h("p", null, `Found ${plan.selected.length} game file${plan.selected.length === 1 ? "" : "s"}: `, h("strong", null, `${plan.copy.length} to copy (${formatBytes(plan.copyBytes)})`), plan.skip.length ? `, ${plan.skip.length} already imported (${formatBytes(plan.skipBytes)})` : "", "."),
          h(
            "p",
            { class: "muted small" },
            roots.length > 0 ? `Game folder: ${roots.join("; ")} · ` : "",
            session.packages.length > 0 ? `${session.packages.length} package${session.packages.length === 1 ? "" : "s"} · ` : "",
            `${session.ignored} other files ignored (multiplayer, campaign, executables).`,
          ),
          session.ignoredPaths.length > 0
            ? h(
                "details",
                { class: "ignored-files", "data-testid": "ignored-files" },
                h("summary", { class: "small muted" }, `Files not imported (${session.ignoredPaths.length})`),
                h(
                  "ul",
                  { class: "missing-list" },
                  session.ignoredPaths.slice(0, 300).map((p) => h("li", null, h("code", null, p))),
                  session.ignoredPaths.length > 300 ? h("li", { class: "muted" }, `…and ${session.ignoredPaths.length - 300} more`) : null,
                ),
              )
            : null,
          plan.selected.length === 0
            ? h("p", { class: "warn" }, "No Zombies files in that folder. Pick the folder that contains zone/ and main/.")
            : null,
          free !== null && plan.copyBytes > free
            ? h(
                "p",
                { class: "warn", "data-testid": "import-too-big" },
                `This needs ${formatBytes(plan.copyBytes)}; the browser reports ${formatBytes(free)} more for this site. Chrome and Edge always report 10 GB more than the site uses and often allow more. If storage runs out, the import stops with a message and keeps what it copied. To import less, uncheck maps below and import them later.`,
              )
            : null,
          mapChoices,
          availabilityView(projected, { title: "After the import" }),
          h("label", { class: "toggle", for: "include-video" }, videoCb, h("span", null, "Also import the cinematics (main/video, large, not used yet)")),
          h(
            "div",
            { class: "row" },
            h(
              "button",
              {
                class: "btn primary",
                "data-testid": "start-import",
                "data-nav-id": "start-import",
                disabled: plan.copy.length === 0,
                onclick: () => void startImport(),
              },
              plan.copy.length ? `Import ${formatBytes(plan.copyBytes)}` : "Nothing new to import",
            ),
            h(
              "button",
              {
                class: "btn small",
                "data-nav-id": "clear-selection",
                onclick: () => {
                  session.clear();
                  void renderImport();
                },
              },
              "Clear selection",
            ),
          ),
        ),
      );
    }
    nav.restoreFocus(focusKey);
  };

  const renderStorage = () => {
    clear(storageBox);
    const used = storage.usage;
    const quota = storage.quota;
    put(storageBox,
      h("h2", null, "Browser storage"),
      used !== null && quota !== null
        ? [h("p", null, `${formatBytes(used)} used; the browser reports ${formatBytes(Math.max(0, quota - used))} more for this site.`), bar(quota ? used / quota : 0)]
        : h("p", { class: "muted" }, "This browser does not report its storage quota."),
      h(
        "p",
        { class: "small muted" },
        storage.persisted
          ? "Storage is persistent: the browser will not clear it on its own."
          : "Storage is not marked persistent yet; the import asks the browser to keep it. Clearing site data removes the files.",
      ),
      h(
        "p",
        { class: "small muted" },
        "The browser sets the limit; the site cannot raise it. Chrome and Edge report the space used plus 10 GB whatever the real limit, which in a normal window is a share of the disk and usually larger. Private windows keep the files in memory and run out after about 2 GB while reporting 5 GB: import in a normal window. Firefox allows 10 GB or 10% of the disk, whichever is smaller, until you allow persistent storage, then up to half the disk.",
      ),
      storage.persisted === false
        ? h(
            "button",
            {
              class: "btn small",
              "data-testid": "persist-storage",
              "data-nav-id": "persist-storage",
              onclick: async () => {
                const ok = await requestPersist();
                storage = await storageInfo();
                renderStorage();
                void renderImport();
                toast(ok ? "Storage is persistent now." : "The browser did not make storage persistent.", ok ? "success" : "warn");
              },
            },
            "Ask for persistent storage",
          )
        : null,
    );
  };

  put(host,
    h(
      "section",
      { class: "screen files", "data-testid": "files-screen" },
      h("h1", null, "Game files"),
      h(
        "div",
        { class: "banner banner-privacy" },
        icons.shield(),
        h(
          "div",
          null,
          h("strong", null, "Your files stay on this computer. "),
          "They are copied into this site's private browser storage (the Origin Private File System) and read from there by the game. Nothing you import is uploaded, and the server never sees it.",
        ),
      ),
      h("div", { class: "grid two" }, h("div", { class: "col" }, importBox, pack.el), h("div", { class: "col" }, manageBox, storageBox)),
    ),
  );

  const refreshAll = async () => {
    if (opfsAvailable() && !running) await session.init();
    storage = await storageInfo();
    renderStorage();
    renderManage();
    void renderImport();
  };
  mounted = { progress: renderProgress, all: () => void refreshAll() };
  renderManage();
  renderStorage();
  void renderImport();
  void refreshAll();
  d.add(app.on((what) => {
    if (what === "files") renderManage();
  }));

  nav.setScreen(
    [{ button: "y", label: "Pick folder", key: "p", run: () => void pick() }],
    () => {
      if (running) return;
      app.go(app.lobby.room ? "lobby" : "home");
    },
  );
  if (nav.mode !== "mouse") nav.focusFirst(host);

  return {
    unmount() {
      d.dispose();
      pack.unmount();
      mounted = null; // an import keeps running in the background
    },
  };
}
