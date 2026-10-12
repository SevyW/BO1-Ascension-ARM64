# bo1-pack: game file packages

`bo1-pack` turns a player's own Black Ops install into packages for the web build. It writes one package with the
files every Zombies map needs and one package per map. The player imports them on the site's "Game files" screen
with "Add packages". It runs on the player's machine. Nothing is uploaded, and the site never hosts game files.

Packages are smaller than the install: they leave out multiplayer and campaign zones, other languages and the
cinematics. They are also portable: a player can copy them to another computer or browser and import them there
instead of the whole install folder.

## Start it

The site serves the packer as one file, `<BASE_PATH>tools/bo1-pack.mjs` (https://spacemandev.games/bo1/tools/bo1-pack.mjs).
It needs Node.js 18 or later, or Bun:

```sh
node bo1-pack.mjs "C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Black Ops"
```

In this repository, run it from `web/` with `bun run packer -- <folder> [options]` (or `bun packer/main.ts`).

| option | default | effect |
| --- | --- | --- |
| `<folder>` | required | the install that holds `zone/` and `main/`, or a folder above it (searched 8 levels deep) |
| `-o, --out <dir>` | `./bo1-packages` | where the packages go; created when missing |
| `--only <ids>` | all found | comma-separated: `base` and/or map ids (`zombie_theater`, `zombie_pentagon`, ...) |
| `-l, --language <name>` | first line of `localization.txt` | the language to package (`english`, `french`, ...) |
| `--list` | off | print the packages and sizes, write nothing |
| `-f, --force` | off | rewrite packages that are already up to date |
| `--verify <file>...` | - | check packages: structure, sizes, sha256 of every file |

Output for an install with two maps:

```
bo1-packages/bo1-zombies-base-english.tar
bo1-packages/bo1-zombies-zombie_theater-english.tar
bo1-packages/bo1-zombies-zombie_pentagon-english.tar
```

Exit codes: `0` done, `1` bad arguments or an I/O error, `2` nothing to package or the base files are incomplete,
`3` a verified package failed.

### In the browser

The site's "Game files" screen has a "Make packages" card that writes the same packages without Node
(`web/client/src/assets/pack.ts`, `web/client/README.md` "Making packages"). The player picks the install and ticks
the packages. Chrome and Edge write them into a folder the player picks and skip packages that are already up to date
there. Firefox and Safari offer each package as a download. The manifest's `tool` is `bo1-pack 1 (web)`, and
`--verify` accepts these packages. The page hashes in JavaScript (286 MiB/s in Chrome 151 on an Apple M4) and reads
every file twice.

## What goes where

The rules are the importer's (`web/client/src/assets/manifest.ts`); `plan.ts` applies them:

| package | files |
| --- | --- |
| `base` | `localization.txt`; `zone/Common/{code_pre_gfx_mp, code_post_gfx_mp, code_post_gfx, patch_mp, patch, ui_mp, frontend, common_zombie, common_zombie_patch}.ff`; `patch_ui`, `patch_ui_mp`, `dev_mp` when present; the localized copies of these in the chosen language (`zone/English/en_*.ff`); `main/iw_*.iwd`; `main/localized_<language>_iwd*.iwd` |
| `<map>` | `zone/Common/<map>.ff`, `zone/Common/<map>_patch.ff` (when present), `zone/<Language>/<prefix><map>.ff` |

A map is packaged only when its zone and its localized zone are both present. Otherwise the packer prints
`skipping <map>: missing <file>`. If any required base file is missing, it refuses to write the base package and
lists the missing files. Map packages can still be written with `--only`. The maps are the ones in
`web/shared/roster.ts` `MAPS`.

## How it works

- **Format**: a package is a plain uncompressed POSIX ustar archive (`web/shared/gamepack.ts`). Fastfiles and IWDs
  are compressed already. The first entry is `bo1-package.json`. It holds the format (`bo1-zombies-package`, version
  1), the id (`base` or the map id), the kind, the map, the language, the creation time, the tool, and every game
  file's path and size. Then come the game files, under the same paths the importer stores them at
  (`zone/Common/x.ff`). The last entry is `bo1-package.sha256` in `sha256sum -c` format.
- **Extracting**: any tar tool can extract a package. The result is a partial install that the folder importer also
  accepts.
- **Writing**:
  - The packer scans the folder the way the browser's folder picker does, then plans the packages.
  - It writes each package to `<name>.tar.part`, hashing every file as it copies it, and renames the file when done.
  - A package whose manifest lists the same files with the same sizes and language counts as up to date, and the
    packer skips it.
  - Before writing, it checks the free disk space when the OS reports it.
- **Importing**: the page reads the header chain with `Blob.slice` (one 512-byte read per entry) and checks the
  manifest against the entries. It then streams each file's byte range into OPFS like any picked file. A truncated
  copy is reported as incomplete before anything is written. The page checks sizes, not checksums; `--verify` does
  that.

## Not done

- No standalone executables: players need Node.js or Bun. `bun build --compile` could produce them; they would be
  unsigned, and Windows SmartScreen warns about unsigned downloads.
- Cinematics (`main/video/*.bik`) are not packaged, because the web build does not play them yet.
- One language per run. Run it again with `--language` for another.
- Tested only against a fake install, a folder of random files with the retail names; never against real game files.
  The page's packer was tested in headless Chrome only (folder output through OPFS folders standing in for the picker,
  downloads with the picker removed); not in Firefox or Safari, and not with multi-GB files.

## Limits

- ustar stores sizes in 11 octal digits, so one file can be at most 8 GiB. Retail files are far smaller.
- Paths are ASCII, at most 255 bytes, with the retail directory names.
- The page reads package format version 1 or lower. A newer package asks the player to reload the page.
