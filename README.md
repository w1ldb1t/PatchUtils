# PatchUtils

PatchUtils is a small collection of CLI tools for working with Git patches.

## Utilities
- `create-patch` - Lets you interactively create a patch file from modified or untracked files, and writes a unified diff patch.
- `update-patch` - Loads an existing multi-file patch and lets you interactively update specific files.
- `split-patch` - Splits a multi-file patch into one patch per file.

## Prerequisites
- `clang`
- `meson` & `ninja`
- Dev packages for `libgit2` & `ncursesw`

## Build
```sh
meson setup build
meson compile -C build
# Optionally install:
# meson install -C build
```

If `clang` is not your default compiler, configure Meson with `CC=clang meson setup build`.

## Usage
Run the tools inside a Git working tree unless you are only splitting a patch:
```sh
create-patch                 # interactively choose files and write changes.patch
update-patch path/to.patch   # refresh or curate an existing patch file
split-patch path/to.patch    # explode a patch into <file>.patch pieces
```

## License
PatchUtils is released under the MIT License. See `LICENSE` for details.
