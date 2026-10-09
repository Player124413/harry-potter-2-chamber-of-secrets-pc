# Importing a retail Harry Potter 2 installation

The native runtime does not bundle the copyrighted retail game data. Use this workflow with a retail installation you own. The importer reads data files only: it never launches the old installer, game executable, DLLs, Wine, Rosetta, or a virtual machine.

## Easiest: use the launcher

Open the app and choose **Import retail game…** for an installed folder or
**Import retail archive…** for an archive. The in-process C++ importer
validates and copies data to `~/Library/Application Support/Harry Potter 2/Data/Retail`;
re-running an import is safe. Player installs do not need Python or a separate
importer.

`Build/prepare_retail_data.py` remains in the checkout as a reference oracle for
the native implementation's parity tests; it is not shipped or used at runtime.

## Supported input

Provide either:

- an extracted retail installation directory with `System`, `Maps`, `Textures`, `Sounds`, `Music`, and `Help` below it; or
- a ZIP, TAR, or 7z archive containing exactly one such installation tree. Supported suffixes: `.zip`, `.tar`, `.tar.gz`, `.tgz`, `.tar.bz2`, `.tbz2`, `.tar.xz`, `.txz`, `.7z`.

The native importer uses its bundled libarchive build for ZIP, TAR-family, and
7z input. Archive paths and entry types are validated before extraction.
Windows executables, DLLs, installers, scripts, source-control metadata,
symbolic links, and disguised PE binaries are excluded or rejected.

A valid retail-only import contains the final 42 retail maps, the complete retail package and localization set, 214 cutscene localization files, retail audio/music, textures, and Help assets. It contains no prototype fallback assets.

## Developer CLI

The `hp2_import` CLI is a build-tree developer tool. It accepts the same
profiles and input formats as the in-process API, but is not installed in the
player bundle.

From an archive:

```sh
out/macos-arm64/hp2_import \
  --archive "/path/to/Harry Potter and the Chamber of Secrets.7z" \
  --output "$HOME/Library/Application Support/Harry Potter 2/Data/Retail" \
  --profile retail-only \
  --link-mode copy
```

From an extracted installation:

```sh
out/macos-arm64/hp2_import \
  --retail-root "/path/to/Harry Potter and the Chamber of Secrets" \
  --output "$HOME/Library/Application Support/Harry Potter 2/Data/Retail" \
  --profile retail-only \
  --link-mode copy
```

`copy` is the portable choice and leaves the source dump independent. `auto`
hard-links when source and destination are on the same filesystem, otherwise
copies.

Validate a completed import without changing it:

```sh
out/macos-arm64/hp2_import \
  --retail-root "/path/to/Harry Potter and the Chamber of Secrets" \
  --output "$HOME/Library/Application Support/Harry Potter 2/Data/Retail" \
  --profile retail-only \
  --link-mode copy \
  --check
```

For archive-based validation, use the same `--archive` argument instead of
`--retail-root`.

The importer writes `overlay-manifest.json` with per-file SHA-256 hashes, provenance, classifications, and profile metadata. A malformed or incomplete retail tree fails before launch rather than silently borrowing prototype files.

## Data identity

`overlay-manifest.json` is written and fully validated at import time: every
file's SHA-256, provenance, classification, and profile metadata are recorded,
and a malformed or incomplete tree fails the import instead of producing a
silent mix of retail and prototype files. Runtime enforcement of these
manifests is landing separately; until it ships, the runtime does not yet
re-validate the manifest at launch, so treat import-time validation as the
current integrity boundary.

## Run

After a successful import, open:

```text
dist/macos-arm64/HarryPotter2.app
```

In the launcher, the **Game Data** tab shows **Retail game data** and
**Prototype data**. Importing retail data writes it to the conventional app
destination. **Choose prototype folder…** assigns a separate validated
prototype tree. Choose **New Game** for a clean campaign or **Continue** for a
listed save.

For an alternate validated output directory, launch explicitly:

```sh
open -n dist/macos-arm64/HarryPotter2.app --args \
  -datadir="/absolute/path/to/retail-data"
```

Writable settings, logs, cache, persistent actors, and saves are separated by
edition under:

```text
~/Library/Application Support/Harry Potter 2/User/Profiles/Retail
~/Library/Application Support/Harry Potter 2/User/Profiles/Prototype
```

The common `User/Launcher.ini` stores launcher folder assignments. Re-importing
data does not overwrite profile state. The launcher reads saves without
modifying them and commits settings only after **New Game** or **Continue** is
chosen. `-datadir=` remains a transient command-line override: it uses an
isolated `Profiles/Explicit` state and does not change saved folders.
