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

**Packaging.** `cmake --install <build> --prefix <absolute path>/stage` runs
`windeployqt` or `macdeployqt` (Qt's deploy step requires an absolute prefix).
On Windows everything lands in one folder: the exe, dwgread, the Qt DLLs and
plugins, and the Microsoft C++ runtime DLLs (`msvcp140.dll`,
`vcruntime140.dll`, …), so the unzipped folder runs on a PC that never installed
the Visual C++ Redistributable. The runtime comes from the compiler that built
the app, so that Visual Studio must be at least as new as the one that built Qt
(MSVC 14.39 for Qt 6.9.3); CI's is. Every package has a `licenses/` folder
(Windows: next to the exe; macOS: `Contents/Resources/licenses`) with the GPL and
LGPL texts and a NOTICES.txt that says where the source of dwgread and Qt is;
for a source-built dwgread it also holds its patches and build script
(packaging/licenses, DECISIONS D63). **Microsoft Store:** CI also builds
`DwgChecker.msix` (artifact DwgChecker-msix) with
`packaging/windows/make_msix.ps1`: the installed files plus the manifest that
CMake generates from `packaging/windows/AppxManifest.xml.in` and the Store
images (drawn by `packaging/icon/make_icon.ps1`). It is unsigned and uploaded in
Partner Center, which signs it. Each Store update needs a higher `project()`
VERSION (DECISIONS D64). On macOS, `packaging/macos/sign_and_notarize.sh` signs the nested
dwgread first, then the bundle, with hardened runtime, and notarizes it. CI does
all of this.

## Using it

**GUI.** A splash screen shows while the app starts (1.5 s, then the main window
takes its place; a click on it shows the main window at once). The steps and Settings sit in a column on the left, and the results
fill the right side (drag the divider between them to resize). The app opens
with nothing chosen and walks through four numbered steps: (1) click the first
card to choose the reference (solution) drawing; (2) check the **location
mode** (Absolute, Relative or Orthographic; with several drawings, one for each),
which is remembered for those reference solutions; (3) click the third card to
choose the submissions folder, or its **Choose a .zip…** button for a zip of
them (a Canvas bulk download as it comes); (4) click **Run**. The next step is outlined,
finished steps get a check, and Run is enabled once a reference and a
submissions folder (or zip) are chosen: the location mode always has a value (the
default, or the one remembered), so step 2 needs a change only when that value
is wrong. You can also drop the file or folder on a card, or pick one from its
**Recent** button. While the solution drawings are read (a few seconds each),
the reference card shows a progress bar ("reading Q10Problem2 (2 of 3)…" for
several) and the cursor shows the app is busy; the window stays usable. The
reference card then shows what was read
("inches · 42 objects, 2 dimensions; ignoring 51 objects on turned-off or frozen
layers", or a line per drawing), and the submissions card shows how many
drawings it found (Canvas bulk downloads work as-is, zipped or not). A zip is
unpacked by Run, in the background and after checking the drive has room for it,
into a temporary folder, which is deleted when the next run's
results replace these or the app closes; its drawings may be in any folder in the
zip, Mac `__MACOSX` clutter is skipped, a damaged file is listed in the log, and a
zip inside the zip is not opened (the card says how many). Settings are remembered for
each reference solution (or set of them), so choosing it again brings back its
settings. Check Settings and click **Run**. Settings shows the everyday
**Matching rules** (tolerance, what must match: linetypes, line colors with an
allowed color difference, linetype and color set by layer, layers, dimensions,
hatches); **Advanced settings** holds the rest, including template/starter DWGs
for cheating detection. Only the settings that apply are shown: "Wrong location
(in absolute mode)" and "Allow rotation / mirrored (in relative mode)" when a
drawing uses that mode, the Orthographic views group for Orthographic mode, and
the dimension and hatch checks only for solutions that have dimensions or
hatching. The tolerances default to 0.000001 (drawing units, and degrees); number
fields take a decimal point whatever the system's language (1e-6 works too), and a
tolerance must be above 0. Then:
- **Several drawings per assignment:** choose all the solution drawings on the
  first card (select several in the dialog, or drop several). Each student's
  files (grouped by the name and Canvas user id in the file name; accented names
  work too) are matched one
  to one to the solution drawings, choosing the assignment with the best total
  fit, wherever the files were drawn (an Absolute-mode drawing in the wrong place
  still finds its drawing, with a warning that gives the offset). A drawing with
  no file counts 0%, a file left over (more files than drawings) is listed as an
  extra and not graded, and the student's score is the mean over the drawings.
  Settings then shows a **Solution drawings** table with each drawing's location
  mode, **Dims.** and **Hatches** checks (only for a drawing that has them) and
  **Edit views…** (only for an Orthographic one); the other settings apply to all
  drawings. Click a drawing's name to see it, to tell the drawings apart: a window
  with the drawing in its own colors and linetypes, as in AutoCAD (ACI 7
  black/white contrasting with the background; a checkbox switches between
  AutoCAD's dark background and white), zoom and pan as in the detail view, and
  each item's layer when you hover over it. Several can be open at once; a drawing
  still being read opens when it is read. With a single solution drawing, the
  **Show drawing** button on the reference card opens the same window. A setting that only some drawings use names them underneath, e.g.
  "Only for Q10Problem 1_1-6 (Absolute mode)".
- Results tab, one row per student, sorted A to Z after each run: a **Possible
  cheating** icon (red = strong evidence, amber = weaker; hover for the details),
  the student, the correctness, a summary of the errors such as "2 shifted, 1
  missing, 1 extra", and an **Open in AutoCAD** link (opened as read-only by
  default). Two students' files with the same account name are red: a name in
  one file's LastSavedBy or Author that is also in the other's LastSavedBy or
  Author. One student's files saved under different account names
  are amber (names shared by much of the class, such as a lab account, never
  count). The same creation time counts only for two files of the same drawing,
  or with an account name in common. A file created more than 2 months before
  the class's files were last saved (the middle of them, so grading again later
  changes nothing) is red: perhaps a file from an earlier semester. A creation
  time that a template, a solution drawing or much of the class has does not
  count. The **Possible cheating** tab has a row per group of students linked
  by evidence, strongest first, across the whole tab: the students involved, the
  files involved, named as in the results ("smithjane: Drawing 2", "Solution:
  Drawing 1"; hover for the file names), six of each, then "…and N more", the
  level, the score and the signals, each with
  what it is about in parentheses: the shared account name, the creation time,
  "Drawing 2: 45 / 50" bit-identical drawing objects. In a group of three or
  more, a signal only some of them share names them. A weaker link to another
  student is a row of its own at that level, so it never makes that student look
  as involved as the others. A student linked to a solution drawing or template
  is a row of their own ("with the solution drawing"), and so is a student whose
  own files carry different account names. Click a row for a window with each
  link (file to file) and every signal's details. **Export possible cheating**
  writes the same rows with every student and file (by file name). A student with several files gets a lightly shaded row with their
  overall score (and "2 / 3 drawings" under the name when some are missing).
  Under it is one row per solution drawing, labeled **Drawing 1**, **Drawing 2**, …
  in the order of the first card, which sorts the drawings by name. Hover over
  the Student header or a row for the drawing's and the file's names. A student with several files has a subrow per
  solution drawing (and per extra file); click the student to fold them. Click a
  file's row for the detail view, or the link to open the file in AutoCAD.
- **Help > Show log** (Ctrl+L) lists the parser version, AutoCAD found, why
  any file could not be read, and how long choosing and reading the solution
  drawings took.
- Detail view: the drawing in three tabs: **Overlay**, **Student's file** and
  **Reference solution**. **Overlay** places the student's drawing
  on the solution (matched green, missing red, present but wrong, such as shifted
  or the wrong size, yellow, extra purple). Dimensions appear in the overlay only
  where they are graded, each reading as its drawing shows it (precision, Ø / R
  prefix). Orthographic views are outlined with a little room around their items,
  with their name and score above the box. **Student's file** shows the file as
  submitted: its own coordinates, scale and layout (Orthographic views not
  moved, a wrong scale not undone). **Reference solution** shows the solution as
  drawn. Both show everything in the file, dimensions (with their extension
  lines, dimension lines and arrowheads, as AutoCAD draws them; the Overlay
  shows a dimension as its value only) and Ignored views included, each item in
  its own color and linetype, stacked in AutoCAD's draw
  order (DRAWORDER), on AutoCAD's dark background or white. Clicking an error or a drawing object zooms all three tabs: in
  the student's file to the student's item, or to where a missing one should be,
  and in the solution to the same place as the overlay. Then the full discrepancy list (click an error to zoom to it), metadata side
  by side, and a **Drawing objects** tab. That tab lists everything the checker extracted from the
  student's or the reference's file, with layer, linetype, match status and
  source entity, which helps when a file grades unexpectedly.
- Orthographic mode: use **Edit views…** (step 2) to confirm how the reference
  splits into views. Name a view **Ignored** to leave it out of grading, such as
  a title block or notes: it is still found in each drawing, so a student's copy
  of it is not extra, but nothing in it counts. The graded views are matched
  first, and an Ignored view takes only what they leave. A stray item near a graded view
  still counts there, even inside an Ignored border. In the detail view, each view's
  box and title take its status color (grey for an Ignored view). The views are
  saved next to the reference as `ref.dwg.views.json`, and used by every run
  until you close the app; without them, each run uses that file, or finds the
  views again with the current settings. Moving the gap slider splits the
  drawing again when you let go of it, after asking if you have assigned or named
  views by hand.
- **Line colors must match:** each item's color, after ByLayer and ByBlock, must
  be the solution item's. "…allowing a color difference up to" accepts close
  colors: the RGB distance as a share of black to white, so 250,5,5 against
  255,0,0 is 2 %, red against orange 29 %, red against blue 82 %. An ACI color
  and the same true color are the same color, and ACI 7 (black/white) matches
  black as well as white. **Linetype and color must be set
  by layer** makes each item take both from its layer rather than from the
  object (inside a block, ByBlock counts when the block is by layer).
- **Layers must match:** each item must be on a layer named like the solution
  item's (letter case ignored).
- **Near-miss distance** (Advanced): how far a wrong item may be from the
  solution item and still be reported as one error about it ("is 4.010 long")
  instead of missing plus extra; 0 = 10% of the solution's size. Items of the
  right size are paired at any distance anyway ("is shifted by …").
- Objects on turned-off or frozen layers in the solution (a sketching grid, a
  construction layer) are not part of the answer by default, and the solution
  card says how many were left out. To require them, untick **Ignore objects on
  turned-off or frozen layers in the solution** (Advanced settings > Reading
  drawings). In student drawings they still count, with a warning, unless you
  tick the matching option for student drawings. As in AutoCAD, a block inserted
  on a turned-off layer still shows its objects that are on layers that are on;
  a frozen layer hides the whole block.
- Hatching is ignored unless you tick **Check hatches** in Settings (offered only
  when the solution has hatching). It then
  checks that every hatched region of the reference is hatched, however the
  student made the hatch. **…and with the same hatch pattern** (on by default)
  also requires the same pattern; untick it to accept any pattern. The hatch
  styles fill as in AutoCAD: Normal alternates at each island, Outer stops at the
  first island, Ignore fills the outer boundary whole.
- Dimensions are ignored unless you tick **Check dimensions** (offered only when
  the solution has dimensions). Each reference
  dimension then needs one of the same kind with the same measured value that
  measures the same thing; the text and dimension line may sit anywhere:
  - **Linear or aligned** (either one counts for the other): the same direction,
    and each point anywhere on the line the solution's point is on that runs
    parallel to the extension lines: either end or the middle of a side when the
    width is measured. For a hole's center, any point on its centerline counts
    instead (the distance is the same), however far the student drew the
    centerline, and even when the solution has none there. A point on no such
    line must be the same point.
    In Orthographic mode the points
    only need to be on the same lines across the measurement, so a width given in
    another view counts.
  - **Angle:** the same vertex and the same two sides, from any points along them.
  - **Radius or diameter:** the same center. **Arc length:** the same arc.
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
  ("Wrong location (in absolute mode) counts as an error" in Settings). **Once**
  (default) grades it after moving it back, with one error for the location; the
  detail view then draws the whole drawing yellow. **Every time** grades every
  item where it was drawn, with a warning that gives the offset. Only translation counts as location: a rotated drawing
  is still wrong in Absolute mode.
- File → Save/Load settings as JSON keeps a set of settings per assignment. A
  file that is not a settings file is refused, and the settings stay as they were.
  Pre-dumped `.json` files can stand in for `.dwg` files anywhere.
- In the exported CSV files, a text cell starting with `=`, `+`, `-` or `@` (a file
  or login name, say) starts with an apostrophe, so a spreadsheet shows it rather
  than running it as a formula.

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
