# DWG Checker

A Qt desktop app for grading CAD assignments. Load one reference DWG (the
solution) and a folder of student DWGs. For each student it reports how closely
the drawing matches the reference, with an error list a student can act on
("Front view: circle Ø0.750 drawn as Ø0.740"). It also shows evidence of shared
or copied files: same fingerprint, same saved version, bit-identical geometry,
and so on. It presents evidence only and never labels anything as cheating.

Implements [the spec](docs/SPEC.md) milestones M0–M7, as a GUI-only app (the
spec's CLI was dropped, see DECISIONS D38). Design decisions and their
evidence are in [DECISIONS.md](DECISIONS.md); where every value lives in the
parser's JSON is in [FIELDMAP.md](FIELDMAP.md).

## Layout

| Path | What |
|---|---|
| `src/core/` | `dwgcore`, all comparison logic, **QtCore only** |
| `src/gui/` | `DwgChecker` Qt Widgets app |
| `src/platform/{win,mac}/` | AutoCAD discovery/launching and dwgread location |
| `tests/` | QtTest suites; `fixtures/` = dwgread JSON of the corpus |
| `tests/corpus/` | the test DWGs and the AutoCAD scripts that generate them |
| `third_party/` | pinned LibreDWG version and local patches |
| `scripts/build_dwgread.sh` | builds a static `dwgread` (MSYS2 on Windows, universal on macOS) |
| `.github/workflows/build.yml` | CI: dwgread (cached), tests on 3 OSes, packaging, parity check |

## Building

Requirements: CMake ≥ 3.22, Ninja, a C++20 compiler, Qt ≥ 6.4 (Core, Gui,
Widgets, Concurrent, Test).

**Windows (MSVC 2022)**, from a "x64 Native Tools" prompt:

```bash
cmake -S . -B ../DwgChecker-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/msvc2022_64
```

```bash
cmake --build ../DwgChecker-build
```

```bash
ctest --test-dir ../DwgChecker-build --output-on-failure
```

Or use the presets: set `QTDIR` (and optionally `DWGCHECK_DWGREAD`), then run
`cmake --preset release`. Build trees go to `../DwgChecker-build/`, outside the
source folder, which matters when the source lives in a synced folder.

**macOS (universal)**: `cmake --preset macos-universal` and
`cmake --build --preset macos-universal`.

**Self-contained build folder (Windows).** Every build runs `windeployqt` over
`DwgChecker.exe`. It copies the Qt DLLs, the plugin folders
(`platforms/`, `styles/`, …) and `vc_redist.x64.exe` into the build folder,
along with dwgread, so the folder runs on its own without Qt on `PATH`.

**dwgread.** Build it once with `scripts/build_dwgread.sh` (in an MSYS2 MINGW64
shell on Windows). CMake bundles the first dwgread it finds:
1. `-DDWGCHECK_DWGREAD=<path>`
2. `$DWGCHECK_DWGREAD`
3. `build-dwgread/`, where the script writes it
4. `~/tools/libredwg-<pinned version>/`

If you use the official prebuilt Windows release, its DLLs are copied along with
it. On macOS, dwgread goes into `Contents/Helpers`. The app always uses the
bundled copy; there is no setting for it. Only a build made without one falls
back to `$DWGCHECK_DWGREAD`, then `PATH`. CMake warns when a GUI build has no
dwgread to bundle.

**Packaging.** `cmake --install <build> --prefix stage` runs `windeployqt` or
`macdeployqt`. On macOS, `packaging/macos/sign_and_notarize.sh` signs the nested
dwgread first, then the bundle, with hardened runtime, and notarizes it. CI does
all of this.

## Using it

**GUI.** The app opens with nothing chosen and walks through three numbered
steps: (1) click the first card to choose the reference (solution) drawing, (2)
click the second card to choose the submissions folder, (3) click **Run**. The
next step is outlined, finished steps get a check, and Run is enabled once both
are chosen. You can also drop the file or folder on a card, or pick one from its
**Recent** button. The reference card shows what was read
("inches · 4 objects · 5 dimensions"), and the submissions card shows how many
drawings it found (Canvas bulk downloads work as-is). Settings are remembered for
each reference, so choosing a quiz brings back its settings. Check Settings and
click **Run**. Settings shows the everyday choices (mode, tolerance, what to
check); **Advanced settings** holds the rest, including template/starter DWGs for
copy detection. Then:
- **Several drawings per assignment:** choose all the solution drawings on the
  first card (select several in the dialog, or drop several). Each student's
  files (grouped by the name and Canvas user id in the file name) are matched one
  to one to the solution drawings, choosing the assignment with the best total
  fit, wherever the files were drawn (an Absolute-mode drawing in the wrong place
  still finds its drawing, with a warning that gives the offset). A drawing with
  no file counts 0%, a file left over (more files than drawings) is listed as an
  extra and not graded, and the student's score is the mean over the drawings. Settings then shows a **Solution drawings** table with
  each drawing's mode, dimension and hatch checks (and **Edit views…** for an
  Orthographic one); the other settings apply to all drawings. A setting that
  only some drawings use, because of their mode or checks, names them underneath,
  e.g. "Only for Q10Problem 1_1-6 (Absolute mode)". These are rotation and
  mirroring, wrong location, the Orthographic views group, and dimension
  precision and hatch pattern.
- Results tab, one row per student, sorted A to Z after each run: a **Poss.
  dup.** icon (possible duplicate: red = strong evidence, amber = weaker; hover
  for the details), the student, the correctness, a summary of the errors such
  as "2 shifted, 1 missing, 1 extra", and an **Open in AutoCAD** link (a
  read-only copy by default). A student with several files has a subrow per
  solution drawing (and per extra file); click the student to fold them. Click a
  file's row for the detail view, or the link to open the file in AutoCAD.
- **Help > Show log** (Ctrl+L) lists the parser version, AutoCAD found, and why
  any file could not be read.
- Detail view: the overlay (matched green, missing red, present but wrong, such
  as shifted or the wrong size, yellow, extra purple), the full discrepancy list
  (click an error to zoom to it), metadata side by side, and a **Primitives** tab. That tab lists everything the checker extracted from the
  student's or the reference's file, with layer, linetype, match status and
  source entity, which helps when a file grades unexpectedly.
- Orthographic mode: use **Edit views…** in Settings, under Mode, to confirm how
  the reference splits into views. In the detail view, each view's box and title
  take its status color. The views are saved next to it as `ref.dwg.views.json`.
- Objects on turned-off or frozen layers in the solution (a sketching grid, a
  construction layer) are not part of the answer by default, and the solution
  card says how many were left out. To require them, untick **Ignore objects on
  turned-off or frozen layers in the solution** (Advanced settings > Reading
  drawings). In student drawings they still count, with a warning, unless you
  tick the matching option for student drawings.
- Hatching is ignored unless you tick **Check hatches** in Settings. It then
  checks that every hatched region of the reference is hatched, however the
  student made the hatch. **…and with the same hatch pattern** (on by default)
  also requires the same pattern; untick it to accept any pattern.
- Dimensions are ignored unless you tick **Check dimensions**. Each reference
  dimension then needs one of the same kind with the same measured value.
  **…and the precision they show** (on by default) also requires the same decimal
  places or fraction precision as the reference shows. The style, per-dimension
  overrides, trailing-zero suppression and typed-over text all count. A
  dimension that differs only in precision is one error.
- A line, circle or arc with the right size but in the wrong place is one error
  ("is shifted by …", plus "and rotated by …" when it is also turned by up to 5°),
  however far it was moved, not missing plus extra.
- A drawing at the wrong scale (millimeters instead of inches, or simply 2×) is
  detected when most of it would otherwise fail. "Wrong scale counts as an error"
  (Settings) is **Once** by default: graded after undoing the scale, with one
  error for the scale. **Never** undoes the scale without that error; **Every
  time** does not rescale, so every object is wrong. A rescaled drawing never
  passes.
- In Absolute mode, a drawing that is right but displaced as a whole (most of it
  matches once moved) is graded by "Wrong location counts as an error"
  (Settings, under the scale setting). **Once** (default) grades it after moving
  it back, with one error for the location; the detail view then draws the whole
  drawing yellow. **Never** moves it back without that
  error. **Every time** grades every item where it was drawn, with a warning
  that gives the offset. Only translation counts as location: a rotated drawing
  is still wrong in Absolute mode.
- File → Save/Load settings as JSON keeps a set of settings per assignment.
  Pre-dumped `.json` files can stand in for `.dwg` files anywhere.

## Test corpus

`tests/corpus/dwg/` holds the 22 drawings from the spec plus a few extras (see
DECISIONS D33). They were generated headlessly with AutoCAD's core console:

```bash
powershell -File tests/corpus/gen/make_corpus.ps1 -DwgRead C:/path/to/dwgread.exe
```

The script rebuilds the DWGs from `corpus.lsp` and refreshes `tests/fixtures/`.
The unit tests use only the fixtures, so they need neither AutoCAD nor dwgread.
The exception is `tst_dwgread`: it runs the bundled dwgread on the corpus DWGs,
checks the output against the fixtures, and runs a real batch. It skips itself
when no dwgread is configured.

## Status and known limits

- **Not run on the development machine.** Its endpoint protection blocks
  executing freshly built binaries, so the code was compiled there (no warnings
  at `/W4`) and its corpus expectations were cross-checked in Python, but the
  test suites first run in CI or on your machine. Run `ctest` before relying on
  results.
- COM attach to a running AutoCAD has not been verified against AutoCAD 2018
  (spec M6). LT versions always start a new session (LT has no COM automation).
- MINSERT needs patch `0001`, because dwgread 0.14's JSON omits the array size.
- Splines, ellipses and other curves are reported as unsupported, not compared (v1).
