# DECISIONS

Running log of design decisions and the evidence behind them. Field-level
evidence for the parser output is in [FIELDMAP.md](FIELDMAP.md).

## Parser and build

**D1. LibreDWG 0.14 is pinned** (`third_party/libredwg.version`, tarball SHA-256
recorded there). It was the latest non-prerelease on GitHub (2026-06-27); the
0.14.8xxx tags are nightly-style prereleases. The prebuilt `libredwg-0.14-win64.zip`
from the same tag was used only to produce the Milestone 0 field map and fixtures.
Shipped binaries are built from the source tarball by `scripts/build_dwgread.sh`.

**D2. Two local patches** in `third_party/patches/`, both verified with
`git apply --check` and `patch --dry-run -p1` against the tarball:
- `0001`: the JSON writer drops every `num_*` field. For MINSERT that loses the
  array size (verified: kitchen.json MINSERT has `col_spacing`/`row_spacing` but no
  `num_cols`/`num_rows`). Without the patch MINSERT is reported Unsupported.
- `0002`: doubles are written with `%.14f` (`out_json.c`), not round-trip
  precision (`π/2` → `1.5707963267949`). Bit-exact duplicate detection needs the
  exact bits, so the patch writes `%.17g`. The committed fixtures came from the
  unpatched prebuilt, so tests never depend on more than 14 decimals.

**D3. `-O minJSON` on stdout.** Same structure as `-O JSON` (verified on
kitchen.dwg: identical top-level keys and objects), about 35% smaller, and no temp
files. dwgread writes `SUCCESS` and errors to stderr.

**D4. The parser identity logged everywhere is `dwgread --version` plus the
binary's SHA-256**, because `--version` prints `dwgread 0.14` even after patching.

**D5. Strict JSON first, sanitized retry.** Non-release LibreDWG builds can print
bare `nan`, which `QJsonDocument` rejects. The loader replaces `nan`/`inf` tokens
and control characters and retries once.

**D6. Non-ASCII paths are staged.** dwgread opens files with narrow `fopen()`, so
paths with non-ASCII characters are copied to an ASCII temp name first.

**D7. The ODA File Converter fallback** is optional (Settings > Tools). When
dwgread fails, the file is rewritten to ACAD2018 DWG in a temp folder and read
again, with a per-file warning that metadata may then reflect the converter.
The converter is a separate download from the Open Design Alliance under its own
license, so it is not bundled.

**D7b. dwgread is not a setting (user decision, 2026-11-05).** The app always
runs the dwgread bundled with it (next to the executable, or in
`Contents/Helpers`), so results never depend on a path someone typed. Settings
files no longer store a dwgread path, and a path saved by an earlier version is
ignored. `$DWGCHECK_DWGREAD` and `PATH` are fallbacks only for a build made
without a dwgread to bundle.

## Metadata (DrawingMeta)

**D8. TDCREATE/TDUPDATE come from SummaryInfo.** In dwgread 0.14 output
`HEADER.TDCREATE` and `HEADER.TDUPDATE` are absent (AC1015 and AC1032). Only
`HEADER.TDUCREATE`/`TDUUPDATE` exist. `SummaryInfo.TDCREATE` equals
`HEADER.TDUCREATE` in every corpus file, so it is effectively UTC too. R2000
files have no SummaryInfo, so they have TDUCREATE only.

**D9. Creation times are whole seconds.** Every corpus value is a multiple of
1000 ms. "Identical TDCREATE to the millisecond" therefore means the same second.
It stays a strong signal but is suppressed when more than 30% of the class shares
the value (D27).

**D10. FINGERPRINTGUID behaves as the spec says** (corpus evidence): it survives
SAVEAS and down-saves to 2013/2000 (`ref`, `ref_saveas`, `ref_r2013`, `ref_r2000`).
Every new drawing from `acad.dwt` gets a new one, and so does an exploded
`-INSERT *ref.dwg` into a new drawing (`ref_copypaste`). VERSIONGUID changes on
every save and is equal only for the byte copy.

## Canonicalization

**D11. Model-space membership is `entmode == 2`** (1 = paper, 0 = owned via
`ownerhandle`). This works for AC1015, which has no `BLOCK_HEADER.entities`
array; block contents there come from an owner scan. Paper space means the
active layout's entities (`entmode 1`).

**D12. Layer "0" inheritance and ByBlock.** A block entity on layer 0 takes the
layer of the INSERT that places it (recursively); ByLayer then resolves against
that effective layer. ByBlock takes the INSERT's effective linetype. ByBlock in
model space means Continuous (AutoCAD behaviour). Verified on `ref_blocks`
(CMARK lines ByBlock under an INSERT with explicit CENTER; HIDLINE on layer 0
under an INSERT on HIDDEN).

**D13. Arcs and circles go through the full 3D transform**: block transform ×
`ocsToWcs(extrusion)`. The image of the OCS x/y axes must be a similarity in XY.
If it has a Z component the arc is tilted and becomes Unsupported; if its two
columns differ in length or are not orthogonal it is non-uniform and becomes
Unsupported (spec rule 2), each with a warning. A negative determinant
(extrusion −Z, mirrored INSERT) reverses orientation: CCW `a0→a1` becomes
`(φ−a1)→(φ−a0)`. Real-file evidence is `ref_flipped3d.json`: ROTATE3D stores the
arc as OCS center (−3.5,−1.5), 180°→270°, extrusion (0,0,−1), and the result is
WCS (3.5,−1.5), 270°→360°. MIRROR in plan keeps extrusion +Z and rewrites the
angles itself (`ref_mirrored`).

**D14. LINE points are WCS; ARC/CIRCLE/LWPOLYLINE/TEXT/INSERT points are OCS**
(FIELDMAP). After ROTATE3D every LINE carries extrusion −Z but its start/end are
already in WCS.

**D15. Frozen and off layers are kept by default**, with a warning ("N entities
are on frozen/off layers (included)"). The spec does not say to drop them, and
dropping silently could hide work. A setting skips them. Layer flags:
`flag0 & 1` frozen, `& 2` off (verified with dedicated layers in kitchen.dwg).
This rule now covers student drawings only; D51 drops them from the solution.

**D16. Spline-fit POLYLINE_2D/3D, SPLINE, ELLIPSE, XLINE/RAY, REGION and solids
are Unsupported.** SOLID, POINT, VIEWPORT, LEADER/MLEADER and TOLERANCE are
ignored without a warning. HATCH is read as a filled region and compared only
with "check hatches" (D35). Unsupported entities are never counted, in
either the reference or the student file.

**D17. Merging keeps exact coordinates.** Merged lines use the original extreme
endpoints, not projections, so `ref_split` merges back to exactly (0,0)–(4,0),
and unmerged primitives keep their bits for duplicate detection. Stacked
duplicates are removed before merging (partial overlaps merge without a warning,
exact copies warn). An arc lying on a circle with the same center and radius is
a stacked duplicate of that circle.

**D18. Merge groups are linetype class, plus layer when layers must match**, so
"require layer match" can still tell layers apart after merging.

## Comparison

**D19. Arc angle test: `|Δangle| ≤ angTol` OR endpoint distance ≤ linTol**, per
end. With angTol = 1e-6 rad alone, a small arc whose endpoints sit within 1e-4
could be rejected for an angle difference that is far below the drawing
tolerance.

**D20. Centerlines match on position and direction in every mode, not only
Orthographic.** A student centerline matches if both its endpoints lie within
tolerance of the reference centerline's infinite line and the two overlap.
"Strict centerlines" restores endpoint matching. The extension past the outline
is drawn by eye in every kind of assignment.

**D21. Pass order: exact → geometry-only (wrong linetype/layer) → near-miss →
missing/extra.** The spec lists near-misses first in the error list, and they
are printed first. Wrong-linetype pairs have to be found before near-miss
pairing, though, or an exact-geometry pair could be split by a closer near-miss.

**D22. Counting.** A pair (near-miss or wrong linetype) is one error: the
reference primitive is unmatched, and the student primitive is not an extra.
Correctness = matched ÷ (comparable reference primitives + unpaired student
extras). "Comparable" means lines, arcs and circles, plus dimensions when "check
dimensions" is on. Text is never counted. An empty reference with an empty
student drawing scores 100%.

**D23. Relative alignment pools up to 4 anchors**, ranked by the spec rule
(rarest, lines and arcs before circles, larger first), plus the identity
candidate. One anchor whose student counterpart was drawn wrong would otherwise
lose the whole alignment. Scoring is geometry-only, so linetype mistakes cannot
pull the drawing out of place. Ties prefer no mirror, then the smallest rotation,
then the smallest translation. When nothing aligns, the drawing is compared in
place with a warning.

**D24. Near-miss search radius defaults to 10% of the reference diagonal**
(configurable). A student line that is collinear with and contains reference
lines is paired with all of them ("is part of a longer line", typical for
projection lines left in on the object layer). This is many-to-one; every other
pairing is one-to-one and greedy by distance.

**D42. Displaced copies are paired at any distance (user report, 2026-11-05).**
After the radius-limited pass, a leftover reference line, circle or arc is
paired with a leftover student primitive of the same size (same length, same
radius, same radius and sweep), however far away, nearest first. The copy may be
turned by up to 5°, because a chain drawn from the wrong point is often also
drawn at a slightly different angle (one submission had the 11.400 line 0.17°
off). It is reported as "line 11.400 long at (…) is shifted by (5.49, 4.594) and
rotated by 0.17°": one error instead of missing plus extra. A line of the right
length turned more than 5° stays missing plus extra. The case that prompted this was a
chain of lines whose first line was drawn wrong, so the next two lines were
right but started from the wrong point, 7.2 units from the reference with a
2.4-unit search radius. Pairs found within the radius are unchanged.

**D39. Wrong scale is one error, not every item wrong (user decision, 2026-11-05).**
This overrides the spec's "never auto-scale". If fewer than half the reference
items match as submitted, and "detect wrong scale" is on (the default), the
checker tries uniform scale factors about the origin. Candidates come from three
sources:
- the INSUNITS ratio of the two files;
- size ratios measured in the drawings (bounding-box diagonal, the three
  longest lines, the largest circles and arcs), snapped to a nice factor when
  within 0.2%;
- common conversions (25.4, 2.54, 10, 12, 100, 1000, 304.8, 36, 2–20, and
  their reciprocals).

Each candidate is ranked by the matches the mode's own alignment finds: rigid
in Relative, none in Absolute, translation over the whole drawing in
Orthographic. The best one is kept only if it reaches at least half the
reference and beats the unscaled result by more than 2 matches. A drawing that
is merely wrong in other ways therefore never gets "rescued", and a wrong
placement in Absolute mode is not a scale error.

The kept result is the full comparison of the rescaled drawing, so per-item
errors are still reported, measured at the reference scale. A first error states
the factor, and the units when the INSUNITS ratio explains it: "drawing is at
25.4× the reference scale (drawn in millimeters, the reference is in inches)".
The scale error counts as `scalePenaltyErrors` errors (default 1) in the
correctness denominator, and the drawing can never pass. `ref_mm` scores
10/11 = 90.9% instead of 0%. Duplicate detection still uses the file as
submitted.

**D40. Dimension precision is checked as the dimension reads (user decision, 2026-11-05).**
Under "check dimensions", the sub-option "…and the precision they show" is on by
default. A dimension with the right kind and value must also show the same
precision as the reference. The checker reads the precision from the dimension's
style (sub-styles included) and its per-dimension overrides (Properties palette,
`DIMOVERRIDE`). It then formats the value the way AutoCAD does:
- decimal, scientific, engineering, architectural and fractional units;
- angles in decimal degrees, deg/min/sec, gradians or radians;
- zero suppression (`DIMZIN` / `DIMAZIN`), `DIMRND`, `DIMLFAC` and the decimal
  separator; halves round up.

What is compared is the precision visible in that text: the digits after the
separator, the fraction denominators and the unit marks. Whole-number digits,
leading zeros and the separator character don't count. So "4" (trailing zeros
suppressed) differs from "4.000", while 0.625 still reads "0.625" with trailing
zeros suppressed and passes. Typed-over text is judged by its number closest to
the measured value. When no style information is available the precision is not
checked.

A dimension that differs only in precision is one wrong-attribute error, like a
wrong linetype, for example "linear dimension at (2.0, -0.6) reads 4.00, expected
4.000 (2 decimal places instead of 3)". Precision is not checked after the
wrong-scale fallback (D39), since numbers in other units normally carry other
decimal places. The formatter is used instead of the text in the dimension's `*D`
block because the same settings and value always give the same text on both
sides, and no block parsing is needed. `tst_dims` checks that the formatter
reproduces AutoCAD's text for all 103 dimensions in the fixtures.

**D41. Arc length dimensions are lengths.** `ARC_DIMENSION` was treated as an
angle (angular tolerance, not rescaled). `dim_ref` shows that `act_measurement`
is the arc length, 0.785 for the R0.5 quarter arc, and AutoCAD formats it with
the linear precision. It now uses the length tolerance and scales with the
drawing.

## Orthographic mode

**D25. Segmentation adds a cluster-box merge.** Single-linkage on primitive
boxes alone splits a hole in the middle of a view off into its own "view": the
circle's box is 0.625 from every outline primitive in `ortho_ref`'s top view,
above the 0.153 gap threshold. After linking primitives, clusters whose
bounding boxes come within the gap are merged until stable (verified in Python:
3 views for `ortho_ref` and `ortho_similar`). Centerlines, phantom lines,
dimensions, text and unsupported entities are attached afterwards to the nearest
view.

**D26. View names default to third-angle placement**: the view with neighbours
above and to its right is Front; above is Top, below Bottom, right Right (a
second right is Rear), left Left. The instructor confirms in the editor and the
result is saved to `<ref>.views.json` with the reference SHA-256. A sidecar for
a different SHA is ignored with a log message. Primitives are keyed by source id
(handle path plus segment index); ids that no longer exist after a settings
change fall back to the nearest saved view box. Views confirmed in the editor
are rebound to the current primitive list the same way (`ViewSet::rebindTo`),
and any primitive left without a view is attached to the nearest one. Otherwise
it would be counted in the correctness denominator without ever being reported.
Empty views are saved with a `null` box, never as the 1e300 sentinel.

Joint resolution enumerates K candidates **plus "not found"** per view. Score is
the total one-to-one matches; ties go to the combination with fewer failed
alignment/arrangement checks. Above 200,000 combinations it falls back to
greedy, largest view first. A view with no candidate reports "view not found"
as a failing error. Unclaimed student primitives are attributed to the nearest
located view (in student space) for messages and per-view extras.

## Duplicate detection

**D27. The 30% rule covers every class-wide value.** It applies to fingerprint,
TDUCREATE, LastSavedBy, Author and original file name. Evidence: every corpus
file has LastSavedBy `jhertz`; in a lab every file may share one login. Template
files whitelist their fingerprint and creation time. A student-vs-template
match is shown as informational.

**D28. Bit-exact geometry, weighted by coordinate roundness.** Keys are the raw
bits of each primitive's defining doubles with T = identity (−0 folded into +0,
line endpoints ordered). Matches whose coordinates are all multiples of 0.001
can come from independent typing, for example two students who both start at
the origin. So:
- Strong: at least half the primitives match and at least 3 have non-round
  coordinates.
- Medium: at least 80% match and at least 5, with round coordinates.
- Info: 3 or more.

In Absolute mode the signal is not used at all (user decision, 2026-11-06): the
placement is given, so every correct drawing has the reference's coordinates and
so does every other correct submission. Orthographic mode compares per view and
reports "k of n views bit-identical". In Relative mode `ref_copypaste` vs `ref`
is Medium (round coordinates); a synthetic copy at 13.48291034 is Strong.

**D29. Weights**:

| Signal | Strength | Weight |
|---|---|---|
| SHA-256 | strong | 100 |
| VERSIONGUID | strong | 90 |
| FINGERPRINTGUID | strong | 70 |
| TDUCREATE | strong | 60 |
| bit-exact | strong/medium | 60/35 |
| saved by another student | strong | 50 |
| same LastSavedBy / Author | medium | 30 |
| handles, extra layers, original name | info | 5 |

The combined score is the sum, capped at 100. A pair's level is its strongest
signal. Clusters join pairs with strong signals.

## Hatch check (optional)

**D35. Hatches are read always and compared only on request.** The spec's
default is "ignore hatches", so `checkHatches` is off. When it is on, each
hatched reference region counts as one comparable item, like a primitive. "Check
hatch pattern" is a sub-setting that also requires the same pattern name; it is
on by default (user decision, 2026-11-05). Settings saved before that change
(settings JSON without `"version": 2`) take the new default, because the old
version saved it as off whenever hatches were off. Pattern scale and angle are
never compared.

**D36. Regions are compared by area, not by boundary entities.** A region can be
hatched by picking a point (polyline paths), selecting objects (edge paths),
drawing a boundary (open edge path), or in several pieces. The corpus has all
four, and they must compare equal. Each hatch becomes flattened loops (arcs every
π/64) with the stored fill rule: parity over all loops for normal/outer style,
external loops only for "ignore islands". Coverage is measured on a grid of
about 2,500 interior points per region, kept away from the boundary by
max(10 × linear tolerance, 0.5% of the region's smaller side), so tolerance and
flattening never decide the result.

**D37. Thresholds**:

| Measurement | Result |
|---|---|
| coverage ≥ 99.5% | filled |
| coverage < 2% | "region at (x, y) is not hatched" |
| anything between | "region at (x, y) is only N% hatched" |
| student hatch outside every reference region | "extra hatching" |
| student hatch with more than 0.5% outside | "also fills N% outside the expected region" |

The pattern check compares every student hatch covering at least 5% of the
region. A partly-filled or wrong-pattern region counts as unmatched. An extra or
spilling student hatch is one penalized extra. A student hatch that lies inside
the expected regions is never an extra, even if it only covers part of them.
Expected values (53.8% for `hatch_partial`, 5.5% spill for `hatch_spill`) were
cross-checked with an independent Python port on the fixtures.

## GUI and platform

**D30. COM through raw IDispatch.** The installed Qt kit (6.9.3 msvc2022_64) has
no ActiveQt module, so `GetActiveObject` → `Documents.Open(path, ReadOnly=True)`
uses IDispatch directly, with retries on `RPC_E_CALL_REJECTED`. ProgIDs tried:
`AutoCAD.Application.<release>` (from the registry key, e.g. `25.1`), then
`.<major>`, then version-independent. The test machine registers only
`AutoCAD.Application.25.1` and `.25`. The spec says to verify the attach
behaviour against AutoCAD 2018 in M6; that has not been done (no 2018 install
here, and built executables cannot run here, see D33).

**D31. Registry layout** (AutoCAD 2026): `AcadLocation` and `ProductName` sit
under `HKLM\SOFTWARE\Autodesk\AutoCAD\R25.1\ACAD-9101:409`, the subkey with the
locale suffix, not `ACAD-9101` itself. The `AutoCAD LT` root is also scanned.
LT has no COM automation, so LT always launches `acadlt.exe`.

**D32. QtCore-only core.** The batch runs in parallel on a `QThreadPool` (QtCore),
so the unit tests exercise the same code without any GUI. The GUI launches the
whole batch on a worker thread with `QtConcurrent::run`, as the spec asks.

**D43. Results table (user decisions, 2026-11-05).** Correctness shows the
percentage alone (also in the detail view and the CSV), on a background that runs
smoothly from red at 0% through yellow at 50% to green at 100%. Pass/fail is still
computed but not shown. The status beside Run counts the files that were read and
compared ("31 of 33 files processed"). The first click on "Copy flag" sorts
flagged rows to the top, strongest first, with empty cells last. Colored cells
always use dark text, so they stay readable in Windows dark mode.

**D44. Main window layout (user decisions, 2026-11-05).** The student's name and
the "Open in AutoCAD" link are separate columns. A click anywhere in a row opens
the detail view, except in the AutoCAD column, which opens the file (a read-only
copy by default). The copy flag no longer jumps to the Duplicates tab. The Errors
column shows a summary: every error carries a short label ("shifted", "rotated",
"wrong length", "wrong size", "missing", "extra", "wrong precision", "wrong
scale", "partly hatched"…). The labels are counted in the order the errors are
listed, and warnings are counted at the end ("2 shifted, 1 missing; 1
warning"). The full list is in the detail view and in the cell's tooltip.
Settings shows the basic choices, and everything else is under a collapsed
"Advanced settings", including the template files, which used to take a row of
the main window. The angle tolerance is shown in degrees. "Edit views…" appears
only in Orthographic mode, and rotation and mirroring can only be set in
Relative mode, the only mode that uses them.

**D45. More main-window changes (user decisions, 2026-11-06).** Columns run Copy
flag, Student, Correctness, Errors, AutoCAD. The copy flag is a warning icon,
red for strong evidence and amber for weaker, and its tooltip carries the text and
every signal. Each finished batch is sorted A to Z by student. Rows alternate
subtly: the base color is moved 4.5% toward the text color, which works in light
and dark themes. The log is a separate window (Help > Show log, Ctrl+L); the
status line points to it when files could not be read. The app icon is drawn by
`packaging/icon/make_icon.ps1` and packed into `src/gui/resources/app.ico` by
`make_ico.py`. The Windows .exe and the window icon use it; a macOS `.icns` is
not made yet.

**D46. Reference and submissions as cards, settings per quiz (user decision,
2026-11-06).** The two full-path text boxes are now cards. Each shows the name in
bold and a gray detail line, with the full path as its tooltip. The reference
card's line comes from reading the drawing in the background ("inches · 4
objects · 5 dimensions", or the reason it could not be read). The submissions
card counts the drawings at once, not after Run. **Change…** browses; its arrow
lists the 8 most recent entries, and files or folders can be dropped on a card.
Run is a large button, and Cancel shows only while a batch runs. Settings are
remembered per reference in the app's own settings (keyed by the reference's
full path, not as files next to it). Every settings change and every run stores
them, and choosing that reference again loads them. A reference without saved
settings keeps the current ones and says "new quiz: check the settings".

**D47. A guided three-step start (user decision, 2026-11-09).** The app opens with
no reference and no folder chosen; only the recent lists are remembered. The
cards are step 1 and step 2, each with a number badge that turns into a check when
done, and the next step is outlined in the highlight color. A click anywhere on a
card opens its dialog; a small "Recent" button (shown when there are entries)
lists recent choices. Run is step 3: its badge turns blue and it is enabled only
when the reference was chosen (and could be read) and the folder holds at least
one drawing. Its tooltip names what is missing. Until the first run, the Results
area shows the three steps with the next one in bold. The cards are locked while
a batch runs.

**D48. Several solution drawings per assignment (user decision, 2026-11-09).** The
first card takes one or more solution drawings. Student files are grouped by
student (name and Canvas user id from the file name, else the file name), and
every file is compared with every solution drawing. Each student's files are then
matched to the drawings: score, plus 5 points when the file name matches the
solution's, each drawing and each file used once. (First greedily, best score
first, with a 10%-of-best rule; D53 replaced that with one assignment for the best
total.) A drawing without a file counts 0%; a file left over is an extra, shown
but not graded. The student's score is the mean over the solution drawings. With
one solution drawing the same rule picks a student's best file and lists the
rest as extras.

Copy detection gives each file its part and its student. A student's own files
are never compared with each other. Geometry is compared only within one part
(templates with every part), while metadata (fingerprints, saved-by, …) is
compared across parts too. Class-wide frequencies count students, not files.

The results are a tree, one row per student. A student with one file keeps a
single row. Otherwise there is a subrow per solution drawing (the matched file,
or "no file submitted") and one per extra file, and clicking the student row
folds them. The CSV keeps one row per file for a single solution drawing. With
several, it has a row per student and drawing, a row for each extra file, and an
"(all drawings)" row with the mean. Views (Orthographic) are per solution
drawing; "Edit views…" asks which one. Settings are remembered per set of
solution drawings. `tst_batch` covers grouping, matching, extras, missing
drawings, copy detection per part and the CSV.

**D49. Feedback round (user decisions, 2026-11-09).**
- The copy-flag column is "Poss. dup." (possible duplicate).
- "Edit views…" is in Settings under Mode, enabled only in Orthographic mode.
- Overlay colors: matched green, missing red, present but wrong (shifted, wrong
  size, …) yellow, extra purple. A shifted item thus shows red where it belongs
  and yellow where it was drawn.
- In Orthographic mode each view's box and title take its status color: not
  found or something missing red, something wrong (or a failed alignment check)
  yellow, only extras purple, else green.
- Drop-downs and number boxes in Settings ignore the mouse wheel until clicked
  into, so scrolling the panel never changes a value.
- "Wrong scale counts as an error" is Never / Once / Every time. These map to
  detection with penalty 0, detection with penalty 1, and no detection. The
  old free penalty count (D39) is no longer offered in the GUI.

**D50. Per-drawing mode and checks (user decision, 2026-11-09).** With several
solution drawings, each has its own mode, "check dimensions" and "check hatches"
(`AppSettings::perDrawing`, keyed by file name in lower case, saved with the quiz).
A drawing without dimensions would otherwise count every student dimension as an
extra. Every other setting is shared. A drawing seen for the first time starts
from the current choices. In Settings the "Solution drawings" table replaces the
single Mode and the two main checks; the precision and pattern sub-options apply
wherever those checks are on. Views are resolved only for the Orthographic
drawings, and copy detection uses each part's own mode. Also in this round:
- The Views tab follows Discrepancies, and only for a drawing graded in
  Orthographic mode.
- The Settings area takes the panel's height every time it changes (Advanced or
  the drawings table shown or hidden), so collapsing leaves no blank space.
- "Centerlines must have the same length as in the reference" (strict
  centerlines; off, a centerline only has to lie on the reference centerline and
  overlap it).

**D51. Turned-off and frozen layers in the solution are ignored by default (user
decision, 2026-11-09).** Two Q10 solution drawings kept a 0.5 sketching grid (49
and 45 lines) on a turned-off layer, and every student was marked as missing it.
What the instructor turned off is not part of the answer. The setting
`CanonSettings::ignoreHiddenLayersInReference` (default on, Advanced > Reading
drawings, "Ignore objects on turned-off or frozen layers in the solution")
loads every solution drawing with `AppSettings::referenceCanon()`, which skips
those layers. That covers grading, the solution card and the view editor. The
solution card and the log say what was left out, e.g. "49 objects on
turned-off or frozen layers (Layer2) were ignored". Student drawings keep D15:
a layer a student turned off still counts, with a warning, because turning a
layer off changes only the display. Saved quiz settings without the key get
the new default, so no format version bump was needed.

**D52. Files find their drawing wherever they were drawn (2026-11-09).** Q10
Problem 1 is graded in Absolute mode on purpose. Two students drew that wrench
perfectly but displaced, so it scored 0% and 2.4% there. One file was then left
over as an extra, because its 5.6% coincidental fit to Problem 2 made 0% look
like "not this drawing" (D48's 10%-of-best rule). The other was rightly graded
in place, but nothing said why it failed. Matching a file to a drawing is a question about
shape, not placement. In Absolute mode, `compareDrawings` therefore also
compares the drawing in Relative mode when not everything matched. If moving
the whole drawing back matches most of the reference, the result keeps that
correctness as `placementFreeCorrectness` and gets one warning, e.g. "the whole
drawing is shifted by (12.0, 9.5) from the reference; moved back, 42 of 42
reference items would match". "Most" uses the same bar as the wrong-scale check
(D39): at least half and at least 3 items, and more than 2 beyond the in-place
match. `assignFiles` weighs each file and drawing by `fit()`, the larger of the
two (D53). The grade was unchanged here, still where the drawing was drawn; D54
then made the location error a setting, with that behavior as "Every time". A lower bar was tried and rejected: after free placement a few
items always line up by chance, which in the test fixtures gave a second copy of
part A a 12.5% "fit" to part B.

**D53. Files are matched to drawings one to one for the best total (user
decision, 2026-11-09).** The instructor's rule: a student's files and the
solution drawings are matched one to one, choosing the assignment with the best
total fit over all of them, not each drawing's best file regardless of the
others. `assignOneToOne` (the Hungarian method on fit plus the name bonus)
replaces the greedy pass and its 10%-of-best rule. As many files as possible get
a drawing: with as many files as drawings every file is graded, so the last file
takes the last drawing however poorly it fits (in the Q10 case the displaced
wrench takes Problem 1 by elimination even without D52). Only with more files
than drawings are some left over as extras, those the total misses least; with
fewer files, the drawings left free count 0%. A consequence the old rule
avoided: a student who submits part A twice and no part B has the second copy
graded as part B, where it scores next to nothing.

**D54. Wrong location counts as an error: Never / Once / Every time (user
decision, 2026-11-09).** This is the Absolute-mode counterpart of "Wrong scale
counts as an error" (D39, D49), so a constant displacement can count as one
error. When the whole drawing is right but displaced, it is graded after moving
it back. "Displaced" uses D52's bar: by translation only, most of the reference
matches. **Once** (the default) adds one "wrong location" error ("the whole
drawing is shifted by (12.0, 9.5) from the reference; after moving it back, 42 of
42 reference items match, and the errors below are measured there"), which
counts in the correctness and fails the drawing. **Never** moves it back with
only a warning, so it can pass. **Every time** grades it where it was drawn,
with D52's warning. `CompareSettings::detectLocation` and
`locationPenaltyErrors` mirror `detectScale` and `scalePenaltyErrors`. The
drop-down sits under the scale one and is enabled only when a drawing is graded
in Absolute mode. Location means translation: rotation and mirroring stay
errors in Absolute mode, so the check never rotates (it also feeds D52's fit,
which therefore ignores rotation too). A result graded after moving keeps
`moved` and the offset in dx/dy, shown in the detail window's header. With
**Once**, the detail window draws everything the student drew in yellow
("present but wrong"), even what matches after moving it back, so the overlay
shows that the whole drawing is misplaced while the text lists one error
(`CompareResult::wrongLocation()`). With **Never** matched items stay green. Copy
detection still sees the file as submitted (`absoluteMatched`). The default
changes Absolute-mode grades for displaced drawings: before, every item counted
as shifted.

**D55. Shared settings say which drawings they apply to (user decision,
2026-11-09).** With several solution drawings, most settings apply to every
drawing, but some only to drawings with a given mode or check:
- rotation and mirroring: Relative mode;
- wrong location: Absolute mode;
- the Orthographic views group (positions per view, gap between views,
  alignment tolerance, the two view checks): Orthographic mode;
- dimension precision and hatch pattern: drawings with dimensions or hatches
  checked.

When only some drawings qualify, a grey note under the setting names them, e.g.
"Only for Q10Problem 1_1-6 (Absolute mode)". When none qualify, the setting is
disabled with "Not used: no drawing is graded in Absolute mode". When all qualify, there
is no note. `SettingsPanel::updateScopes` does this on every change to a mode or
check. With one drawing the notes stay hidden and the single Mode and checks
enable these settings; the Orthographic group, always enabled before, is now
disabled outside Orthographic mode. The other settings apply to all drawings:
tolerances, extras, linetypes, layers, wrong scale, near-miss distance,
centerlines, reading and copy-detection options.

**D56. Offsets and angles in messages show their size (2026-11-09).** A Q9
"boxes" file had both upper slots and the upper square drawn 0.00003 too low.
With the quiz's tolerance of 0.000001 that is a real error, but the messages
printed every offset with 3 decimals: "is shifted by (0.0, -0.0)". Offsets
("shifted by", "center off by", the whole-drawing location) now use
`fmtOffset`, which adds digits (up to 8) until a part that is not 0 does not
print as 0: "(0.0, -0.00003)". Rounding noise below that still prints as 0.0.
Turns ("rotated by") get the same treatment against 0 (up to 6 decimals of a
degree). An arc's wrong sweep and an angular dimension's wrong value get extra
digits until they differ from the reference, as lengths already did with
`fmtLen`.

**D57. Setup column beside the results (user decision, 2026-11-09).** The main
window was a stack: cards and Run in a row, Settings under them, results below.
It is now split left and right by a draggable divider:
- **Left:** a column with the reference card, the submissions card and a
  full-width Run, then progress, then Settings. Settings is open by default,
  remembers whether it was folded, and fills the rest of the column, scrolling
  when it is longer.
- **Right:** the Results and Duplicates tabs at the full window height.

Wide screens have width to spare and are short on height, so the table now
shows more students, and the steps read top to bottom before the results. The
divider's position is saved with the window.

The settings panel is laid out in one column (Grading, then What to check; the
Advanced groups stacked), and form labels too long for the column wrap above
their fields. The auto-height of the old settings strip (D50,
`fitSettingsArea`) is gone. The cards' detail lines now wrap instead of being
cut short with "…", and the reference card lists one solution drawing per line. Long
drawing names in the Solution drawings table are shortened in the middle, with
the full path as a tooltip.

**D58. Settings show only what applies; names for the users (user decisions,
2026-11-10).**
- **Matching rules.** "Grading" and "What to check" are one box, since all of
  it says what must match the solution and what may differ. The Advanced
  "Matching" box is now "More matching rules".
- **Location mode** replaces "Mode", with an (i) icon whose tooltip explains
  Absolute, Relative and Orthographic. It is used in the Solution drawings table
  too, where "Dimensions" is shortened to "Dims.".
- **Hidden, not greyed out.** A setting that does not apply is hidden, not
  greyed out with a note (this replaces D55's "Not used: …"). That covers
  rotation and mirroring (Relative), wrong location (Absolute), the
  Orthographic views box, Edit views…, and the dimension and hatch checks with
  their precision and pattern options. Dimension and hatch checks need the
  solution to have some: the solution card's reading reports what each drawing
  contains (`SettingsPanel::setDrawingContents`). In the table, a drawing's
  Dims. or Hatches box, or its Edit views… button, is shown only when it
  applies, and a column with nothing left goes away. "Only for …" notes stay for
  settings that some drawings use. Until the drawings are read, everything is
  shown.
- **The core agrees.** `compareDrawings` checks dimensions or hatches only when
  the solution has some, so a hidden check left ticked cannot count a student's
  dimensions or hatches as extras.
- **Tolerances default to 1e-6:** the tolerance in drawing units, and the angle
  tolerance in degrees (1.745e-8 rad), as the instructor grades.
- **Cheating, not duplicates.** "Copy detection" is "Cheating detection"; the
  "Poss. dup." column, the Duplicates tab, its export and the CSV column say
  "Possible cheating". The internal names (Duplicates.cpp, DupEntry) are
  unchanged.
- **Open as read-only (protect the evidence)** replaces "Open a read-only copy".
- **No "quiz".** Assignments are quizzes or homework, so the card says
  "settings saved for this reference solution" (or "these reference solutions").
  The registry key keeps its old name, `quizSettings`, so saved settings still
  load.
- **The solution card** gives one line per drawing: "Q10Problem 1_1-6 – 42
  objects; ignoring 51 objects on turned-off or frozen layers". The layer names
  are in the log. `Drawing::hiddenLayerObjects` carries the count.

**D59. Cleaner results for several files per student (user decisions,
2026-11-10).**
- **Student row.** It names the student in bold and is shaded with a faint tint
  of the highlight color (the correctness cell keeps its red-to-green color).
  Its grey second line appears only when drawings are missing ("2 / 3
  drawings"). Its Errors cell is empty, with no tooltip, since every file row
  lists its own errors.
- **File rows are one plain line:** "Drawing 1", "Drawing 2", … with several
  solution drawings (the user chose this over the drawing's name, the
  student's file name, or a separate Drawing column). It is "Graded file" with
  one drawing, and "Extra file (not graded)" or "Unreadable file" otherwise. A
  missing drawing's row says "no file".
- **Names on hover.** The Student header's tooltip lists "Drawing 1:
  Q10Problem 1_1-6.dwg", …, and each row's tooltip gives its drawing's and the
  student's file names. "Closest" for an extra file uses the same numbers.
- **Predictable numbers.** The first card sorts several solution drawings by
  file name, with numbers compared by value (QCollator numeric mode), so
  Drawing 2 is "Problem 2" and comes before "Problem 10". Settings are keyed by
  file name and saved settings by the sorted set of paths, so nothing saved
  changes. The CSV still names each drawing in full.

**D60. Colors, by layer, the mode as step 2, Ignored views (user decisions,
2026-11-10).**
- **Line colors must match** (`CompareSettings::matchColor`), with an allowed
  difference (`colorTolerance`, 0-100, default 0). The difference is the RGB
  distance as a share of black to white (`colorDifference` in Color.cpp). It is
  simple to read, an ACI color and the same true color come out equal, and a
  near red is about 2 % while red against blue is 82 %. Colors are resolved like
  linetypes: ByLayer takes the layer's color, ByBlock the INSERT's (white at the
  top level). dwgread's encodings were confirmed on new corpus drawings
  (color_ref / color_wrong / color_near / color_bylayer, made with
  accoreconsole):
  - **Entities:** `{index 256}` ByLayer, `{index n}` ACI, no index ByBlock, and
    `{rgb "c2RRGGBB", flag 128}` a true color.
  - **Layers:** `c3`+ACI, or `c2`+RGB.
  - **Older files:** the index alone.

  The layer table used to keep only the index, which is 256 for every layer in
  R2004+ files; it now has the real ACI and RGB (this also changes copy
  detection's layer-table signature, consistently for all files). The ACI
  palette is computed: 24 hues, five shades, full and half saturation, plus the
  greys. Lines merge only with lines of the same color when colors must match.
- **Linetype and color must be set by layer** (`requireByLayer`): a student item
  whose color or linetype is set on the object is one "not by layer" error.
  Inside a block, ByBlock counts as by layer when the INSERT is. These, like
  wrong colors, are attribute errors: matched geometry with the wrong
  attributes, one error per item, labeled by the first thing wrong (linetype,
  color, layer, by layer).
- **Labels instead of notes.** "Wrong location (in absolute mode) counts as an
  error" comes first, then "Allow rotation (in relative mode)" and "Allow
  mirrored (in relative mode)". The labels say the mode, so these have no
  "Only for …" notes. They are still hidden when no drawing uses that mode.
- **Wrong location: Never is gone** (only Once and Every time); the core still
  supports a penalty of 0.
- **The location mode is step 2 of 4.** It must be chosen for each assignment.
  A step box between the reference card and the submissions card holds the
  Location mode drop-down (with its (i) tooltip, the mode's description and
  Edit views…), or, with several solution drawings, the Solution drawings table.
  The panel still builds and owns these controls (`modeStepWidget`), so saving
  and loading are unchanged. The step is done when saved settings for these
  reference solutions were loaded, or when a mode was picked from the drop-down
  (each drawing's, with several). Picking one saves the settings, so the next
  time the step is done already. Run needs steps 1-3.
- **Ignored view.** A view named Ignored in Edit views is resolved like any
  other, so the student's copy is claimed, but its reference items, the student
  items matched to it and the leftovers nearest to it are set aside
  (NotCompared): not counted, no errors, no alignment checks, never "not found".
  It is meant for title blocks, notes and construction.
- **Advanced boxes.** "AutoCAD and files" is now "Viewing submissions in
  AutoCAD". The reading time limit and the ODA File Converter moved to "Reading
  drawings".
- **Answers given.** Release builds use the compilers' optimization (MSVC
  /O2, clang/gcc -O3, CMake's Release defaults); the Debug configuration Qt
  Creator builds by default has none. The GitHub workflow (build.yml) already
  builds dwgread statically from the pinned LibreDWG on Windows (MSYS2) and
  macOS (universal), builds the app in Release, runs the tests, and uploads
  DwgChecker-windows (windeployqt folder with dwgread.exe) and
  DwgChecker-macos.zip (the .app with Qt and Helpers/dwgread; signed and
  notarized when the secrets exist).

**D61. Fixes from a code review (2026-11-10).**
- **Ignored views keep real errors.** An Ignored view (often a border whose box
  encloses every view) takes a leftover student item only when no graded view
  is within the near-miss distance and it is nearer. Before, an extra or a near
  miss just outside a view's box went to the border and was silently not
  graded.
- **No reference items into Ignored views.** Rebuilding views never assigns an
  unassigned reference item to an Ignored view unless there is no other view.
- **Cheating detection skips Ignored views** in its per-view bit-identical
  check: a title block from a template is the same for everyone.
- **Step 2 is saved explicitly.** `AppSettings::modeConfirmed` is saved with
  each set of solution drawings. Before, saved settings counted as a chosen
  mode, and any settings change saved them, so the step could be skipped.
  Settings saved before this have no flag and count as chosen. Opening a
  reference no longer re-saves its settings; only picking a mode (or changing a
  setting) does.
- **ACI 7 matches black or white**: it shows black on a light background, so a
  student's true-color black is not 100% different. It is named "black/white
  (ACI 7)".
- **Cheating detection's layer-table signature uses the RGB**, so different true
  colors no longer look alike.
- **Several solution drawings** sort stably, by file name and then folder.
- **The color tolerance keeps one decimal.**
- **Two help texts** point to Settings for rotation and wrong location.
- **Cleanups:**
  - the unused `sizeChanged` signal is gone;
  - each entity's color and linetype source is resolved once, not once per
    polyline segment or MINSERT cell;
  - one `blendColors` helper does the table shading;
  - `.qtcreator/` (Qt Creator's machine-specific CMake caches) is ignored and
    no longer tracked.

**D62. CI fixes (2026-11-10).**
- **Linux** installs Qt 6.9.3 with install-qt-action, the same as Windows and
  macOS. It used Ubuntu's Qt 6.4, whose `qt_generate_deploy_app_script` has no
  OUTPUT_SCRIPT (Qt 6.5 renamed FILENAME_VARIABLE). src/gui/CMakeLists.txt now
  uses either name by Qt version, so the declared minimum of 6.4 still
  configures.
- **macOS** runs on `macos-15`; GitHub no longer runs the `macos-14` image. The
  deployment target is 12.0, the minimum for Qt 6.8 and later.
- **A failed test step is followed by a step that runs each failed test
  directly**, with its QtTest log written to a file and printed, plus the exit
  code. `ctest -V` showed no output at all from tst_dwgread.
- **An explicitly given DWGCHECK_DWGREAD is converted to forward slashes**, like
  a found one. CI passes `D:\a\...`, and the path becomes a C++ string literal in
  tst_dwgread, where `\a` is an escape.
- **build_dwgread.sh reads third_party/libredwg.version line by line.** It used
  `source <(grep ...)`, which macOS's /bin/bash 3.2 runs as an empty file, so the
  macOS dwgread job stopped at "LIBREDWG_VERSION: missing". The script must stay
  bash 3.2 compatible.
- **The tst_dwgread failure (Windows and macOS) was a fragile arc split.**
  Hatch boundary arcs are flattened into one segment per pi/64 of sweep with
  `ceil()`. hatch_edges_select's quarter-round corner sweeps exactly pi/2 (32
  segments). The fixture's 14-decimal angle 1.5707963267949 is a hair over pi/2,
  which gave 33 segments; CI's dwgread prints full precision and gave 32, hence
  "loop size". `arcSteps` now subtracts 1e-6 steps before rounding up. An edge
  whose start and end angles agree to 1e-9 is a full turn, not a sliver. Test:
  tst_canon `hatchArcStepsIgnoreLastDigit`. The backslash fix above is kept as a
  real but separate issue.
- **`cmake --install` gets an absolute prefix** (`$GITHUB_WORKSPACE/stage`):
  Qt's deploy step stops with "qt.conf path is not an absolute path" otherwise.
- **On Windows the Qt runtime is deployed next to DwgChecker.exe.** Qt deploys
  into QT_DEPLOY_BIN_DIR, which it takes from GNUInstallDirs ("bin") when Qt is
  found, while the app and dwgread install to the prefix itself; the package
  would have had its DLLs and qt.conf in a `bin/` the exe never looks in.
  Setting CMAKE_INSTALL_BINDIR afterwards changes nothing, so src/gui sets
  QT_DEPLOY_BIN_DIR to "." in an install(CODE) just before the deploy script,
  which keeps a value already set. Checked with a local install: one folder with
  the exe, dwgread and its DLLs, the Qt DLLs, plugins/ and a qt.conf of
  `Prefix = .`.
- **The Windows package carries the MSVC runtime DLLs (user decision,
  2026-11-10).** windeployqt by default adds only `vc_redist.x64.exe`, which a
  PC without the runtime must run before the app starts. Now
  InstallRequiredSystemLibraries installs msvcp140*.dll, vcruntime140*.dll and
  concrt140.dll next to the exe, from the redist folder of the compiler in use
  (`VCToolsRedistDir`, set by vcvars and msvc-dev-cmd), and the deploy script
  passes NO_COMPILER_RUNTIME, so there's no installer. The trade-off is that
  app-local DLLs don't get Windows Update fixes. The UCRT is not bundled; it is
  part of Windows 10 and later, which Qt 6 requires anyway. The bundled runtime
  must be at least as new as the toolset that built Qt (Qt6Core.dll of 6.9.3:
  MSVC 14.39), because the app directory comes first in the DLL search order.
  GitHub's runner has a newer VS. This laptop's Build Tools are 14.34, so a
  local `cmake --install` here is for checking the layout only, not for handing
  out. A runner VS newer than its CMake (the redist folder named after the
  toolset, e.g. VC145) would make the install stop with "file not found", not
  ship without the runtime. The first CI package (2026-11-10) was built by the
  runner's VS 2026 and carries runtime 14.51, the same toolset that linked the
  exe; every DLL that DwgChecker.exe, the Qt DLLs and qwindows.dll import is in
  the package or part of Windows.

**D63. License and source notices in the packages (user decision,
2026-11-10).** Handing the zip to colleagues already distributes dwgread (GNU
LibreDWG, GPL-3.0-or-later, copyright FSF) and Qt (LGPL-3.0), so the packages
carry a `licenses/` folder: next to DwgChecker.exe on Windows, in
`Contents/Resources/licenses` inside the macOS app (installed before signing, so
the signature seals it).
- **NOTICES.txt** is generated at configure time from packaging/licenses: what
  is in the package, under which license, and where each source is. It names
  the pinned LibreDWG tarball with its URL and SHA-256, now read from
  third_party/libredwg.version along with the version. It also names Qt's
  source archive for the exact version, and the MSVC runtime.
- **GPL-3.0.txt** is the repo's LICENSE; **LGPL-3.0.txt** is a verbatim copy of
  the LGPL v3 text from Qt's own license file (`C:\Qt\Licenses\LICENSE`).
- **For a dwgread built by build_dwgread.sh** (CI), `libredwg-source/` holds
  libredwg.version, the patches and the build script, laid out like the source
  tree so `bash scripts/build_dwgread.sh` runs from there. Together with the
  tarball that is the complete Corresponding Source under GPLv3 section 6. The
  patch descriptions in dwgread-source.txt.in must be updated when the patches
  change. The script is installed without its execute bit, so the signed bundle
  has no executable in Resources.
- **For the official prebuilt dwgread** (local builds, found by its DLLs, now
  DWGCHECK_DWGREAD_PREBUILT), the notice says so and names the release. It does
  not include licenses for the release's libiconv and PCRE2 DLLs, since such
  packages aren't handed out.
- **Qt's own third-party components** (pcre2, harfbuzz, libpng and others) are
  covered only by a link to Qt's list of their licenses, not with their texts.
- **CI's dwgread has no libiconv or PCRE2 in it:** it imports only
  KERNEL32/msvcrt, and none of libiconv's charset names are in the file. What it
  carries besides LibreDWG is MinGW-w64 startup code and the GCC runtime, which
  the GCC Runtime Library Exception covers without a notice.
- **No copyright line for DWG Checker itself (user decision).** Its code was
  written by Claude, and US copyright needs a human author, so instead of a
  claim NOTICES.txt and the About box carry the credit "DWG Checker was designed
  by Joshua Hertz [j.hertz@northeastern.edu]; its code was written with Claude
  (Anthropic)."
- **Help > About** says the app is free software under GPLv3 with no warranty,
  names Qt and LibreDWG, and links to the licenses folder when it exists. Help >
  **About Qt** is Qt's standard box.
- **The Windows package no longer has the D3D and DXC shader compilers or the
  software OpenGL** (`--no-system-d3d-compiler --no-system-dxc-compiler
  --no-opengl-sw`, about 40 MB): the app draws with QPainter on a raster
  QGraphicsView, and each would have needed its own notice (DXC: NCSA; Mesa:
  MIT). The option is passed on Qt 6.9 and later, where it was checked.
- **App stores:** the Mac App Store is ruled out while dwgread is bundled,
  because Apple's terms add restrictions the GPL forbids and the FSF holds
  LibreDWG's copyright; it had GNU Go removed from that store in 2010. The
  Microsoft Store allows GPL apps with the publisher's own license terms, if it
  is wanted later.

**D64. A Microsoft Store package (user decision, 2026-11-10).** CI builds
`DwgChecker.msix` (artifact DwgChecker-msix) from the same installed files as
the zip, with packaging/windows/make_msix.ps1. It is unsigned; Partner Center
signs it after upload.
- **Identity:** `13710gruepr.DWGChecker`, publisher
  `CN=06F1A065-BAE3-4E3B-878B-6C4471EB60C4`, display name "gruepr", from Partner
  Center's Product identity page, in packaging/windows/AppxManifest.xml.in.
  They aren't secret: every installed package shows them. The DisplayName "DWG
  Checker" must be the name reserved in Partner Center.
- **Version:** CMake writes `major.minor.patch.0` from `project(VERSION)` into
  build/msix/AppxManifest.xml; the Store owns the fourth part. Every Store
  update needs a higher version, so bump `project()` VERSION before uploading.
- **A full-trust desktop app** (`runFullTrust`), x64, Windows 10 1903 or later.
- **Write virtualization is off** (`desktop6:FileSystemWriteVirtualization` and
  `RegistryWriteVirtualization` disabled, restricted capability
  `unvirtualizedResources`, hence 1903). By default a packaged app's new files
  in AppData, including %TEMP%, and its HKCU writes go to a per-package store
  that other programs don't see. "Open in AutoCAD" hands a running AutoCAD (via
  COM) a copy in %TEMP%\DwgChecker-evidence, which it couldn't open. An AutoCAD
  that the app starts runs inside the package, and its own new settings and
  files would have vanished into DWG Checker's store. With virtualization off,
  writes go where they go for the zip version, so the two share settings, and
  nothing is removed on uninstall. Both restricted capabilities need a short
  justification in the Store submission.
- **Not handled:** an AutoCAD started by the app still runs with DWG Checker's
  package identity (no breakaway from the package). That's untested; it is the
  first thing to try in a Store-installed copy.
- **Store images:** make_icon.ps1 now draws the icon at any size and offset (its
  app icons are byte-identical to before) and also writes packaging/windows/Assets:
  StoreLogo and Square44x44Logo at scale-100/200 and the Start tile at
  scale-100/200, with the icon at two thirds of the tile's width. The taskbar
  sizes (targetsize-16/24/32/48/256, plated and unplated) are the app icon's own
  drawings, with the heavier strokes of the small sizes.
- **resources.pri** maps the manifest's logo names to those files. makepri
  indexes a folder holding only the manifest and Assets, so DLLs and translations
  aren't resources. The default config's `<packaging>` section, which splits
  scales into resources.scale-200.pri for bundles, is removed: a single package
  reads only resources.pri.
- **Checked locally (Windows SDK 10.0.26100):** makeappx packs the installed
  files and validates the manifest; a dump of resources.pri lists every logo
  candidate. The package has not been installed or put through the Windows App
  Certification Kit. Sideloading it would need a self-signed certificate trusted
  on this managed laptop, so the first install should come from a
  private-audience Store submission.

**D65. Progress while the solution drawings are read (user request,
2026-11-10).** Reading them was already in the background, but the only sign was
"reading…" in the card's gray detail line, so the pause after choosing them
looked like nothing was happening. Now the reference card shows a thin progress
bar under that line: a moving bar for one drawing, and for several a bar that
counts them, with "reading Q10Problem2 (2 of 3)…". The window shows Qt's busy
cursor (an arrow with a spinner, not the hourglass, since the window stays
usable; the card keeps its pointing hand). Every read gets a number, and only
the latest one updates the card or ends the busy state, so a slow earlier read
that finishes late changes nothing. Clearing the choice or a missing file ends
it at once. Edit views… still reads its drawing on the GUI thread with the wait
cursor, as before.

**D66. A preview of each solution drawing (user request, 2026-11-10).** With
several solution drawings, the names in the step 2 table are links. Each opens a
window (PreviewDialog) that shows that drawing alone, so the drawings can be
told apart.
- **The same drawing code as the detail view** (DrawingView): lines, arcs,
  circles, hatches, text and dimension values, linetypes as dash patterns,
  wheel zoom, drag pan, and a tooltip with each item's kind, layer and linetype.
- **The drawing's own colors** instead of status colors: each primitive's color
  after ByLayer and ByBlock (its ACI or true color, already resolved by the
  canonicalizer). ACI 7 shows white on the dark background and black on the
  white one, as in AutoCAD. The background starts as AutoCAD's default model
  space (33, 40, 48), so yellow and white lines show. A checkbox switches it to
  white, remembered in "preview/darkBackground".
- **What is shown is what is graded:** the drawing as read with the reference
  reading rules, so objects on turned-off or frozen layers are left out when
  that setting is on, and the header says so, as the card does.
- **Fast:** the drawings read for the reference card are kept (m_refDrawings, by
  path, together with each one's summary line), so the preview opens at once,
  and Edit views… uses them too instead of reading the file again. They are
  dropped when the reading rules change and the card reads them again. A name
  clicked while reading opens when the read ends; a drawing that could not be
  read says so.
- **Several previews can be open,** one per drawing; clicking the same name
  again brings its window forward. Their titles use the results' "Drawing N"
  numbers. They close when the choice of solution drawings changes, since
  "Drawing 2" may then be another file.
- **A single solution drawing** has no table, so the reference card gets a
  **Show drawing** button (next to Recent, shown only with exactly one drawing)
  that opens the same window (user decision).
- **Not done:** the view boxes of an Orthographic drawing aren't drawn in the
  preview.

**D67. A drawing's mode, once picked, counts as chosen in any set (bug fix,
2026-11-10; superseded by D68 the same day, and its code removed).** With several solution drawings, Run stayed disabled until every
drawing's Location mode drop-down was clicked, even when the modes shown were
already right. Step 2 counted as done only for the exact set of paths whose
settings were saved with modeConfirmed, but settings are keyed by the full
paths of the set (quizKey). The same files in another folder, or the set with a
file added or removed, are a new set: each drawing's mode was filled in (modes
are remembered by file name), yet none counted as chosen. The saved settings
showed this: three separate Q10 sets and two Q4 sets.
- **DrawingSettings has `modeChosen`,** saved with the drawing's mode (JSON
  "modeChosen"; settings saved before it read as false). Picking a drawing's
  mode sets it; loading a set saved as confirmed sets it for that set's
  drawings. With several drawings, step 2 is done when all of the set's drawings
  have it, so a drawing is asked about once, not once per set or folder. A
  drawing never seen before still has to be chosen (D60).
- **setSettings merges perDrawing instead of replacing it:** what the loaded
  settings say about a drawing wins, drawings they don't list stay known, and
  modeChosen, once true, stays true. An older saved set holds an older snapshot
  of the drawings and would otherwise forget picks made since.
- **One drawing is unchanged:** its mode is the main Location mode, saved with
  the set.
- **Test:** tst_batch `perDrawingSettingsRoundTrip` (the flag round-trips;
  without it, false).

**D68. The location mode no longer holds up Run (user decision, 2026-11-10).**
The default mode, or the one remembered for the drawings, is often right, so
there's no reason to make the instructor touch every drop-down. This reverses
D60's "the mode must be chosen every time" gate, and with it D67.
- **Run** is enabled once a readable reference and a submissions folder with
  drawings are chosen. Step 2 gets its check as soon as step 1 is done, and the
  guide says "Check the location mode".
- **Removed:** AppSettings::modeConfirmed, DrawingSettings::modeChosen and
  their JSON keys (old saved settings that have them still load; the keys are
  ignored), SettingsPanel's modeChosen()/setModeChosen(), the bold "Choose the
  location mode…" note in step 2, and the modeChoiceChanged/modeChosenByUser
  signals. A mode change is still saved for the reference solutions at once,
  through the panel's changed() signal, as before.
- **Kept from D67:** loading saved settings merges perDrawing instead of
  replacing it, so an older saved set doesn't bring back older modes for
  drawings it doesn't list.

**D69. Graded views come before Ignored ones (bug fix, 2026-11-10).** In a Q3
set of five Orthographic solution drawings, every student's Problem 2 and
Extended Work files were matched to each other's solution. Both solutions have
the part twice: a clean copy, graded as Front, and the same geometry again with
its dimensions, named Ignored, and the Ignored view was first in the view list.
- **Cause:** resolveViews placed the views by the most student items claimed in
  total, ties to the first combination tried, which varies view 0 fastest. On a
  student's single copy, Front and Ignored claimed the same items, so the
  Ignored view took it. Front was "not found", its items were set aside, and the
  file scored about 0 against its own solution. With both right pairings at
  about 0, the one-to-one assignment (D52) had nothing to prefer, and the swap
  was as good as any. Problem 1 also has Ignored views, but listed after its
  graded ones, and Problems 3 and 4 have none, so they were unaffected.
  Reproduced in Python with a simulation of the view placement on the real
  files (kept in the scratchpad, not in the repo): Problem 2's file scored 0 %
  against Problem 2 before the fix and 40.5 % after. The dumps and drawings
  showed the duplicated geometry.
- **Fix:** an Ignored view only keeps a student's copy of it from counting as
  extra, so it never competes with a graded view.
  - resolveViews scores a combination by the items claimed by graded views,
    then the items the Ignored views add, then the fewest failed alignment
    checks. The greedy path for many views also places graded views first.
  - compareCore's exact matching (Pass A and B) runs graded views before Ignored
    ones, then larger before smaller, so an Ignored view placed over a graded
    one can't take its items either.
- **Test:** tst_ortho `ignoredCopyOfAGradedView`: a solution with an L-shape
  twice (Ignored first, Front second). A student's single copy scores 100 %
  with Front found. A student who drew both copies gets one view each and
  nothing extra.
- **Results graded before this fix** for drawings with such an Ignored copy
  were wrong (about 0 % for those files) and should be run again.

**D38. No CLI (user decision, 2026-11-05).** The spec's `dwgcheck` front end was
removed because only the GUI is used. The core library still uses QtCore only,
so the logic stays testable without the GUI. The spec's reason for the CLI is
covered by the unit tests, which link `dwgcore` directly. What the CLI uniquely
provided moved elsewhere:
- `--dump` became the detail dialog's **Primitives** tab: every extracted
  primitive with its layer, linetype class, match status and source entity.
- The CI step that ran the real dwgread through the app's code became
  `tst_dwgread` under ctest. It checks the bundled parser against the pinned
  version and the committed fixtures, then runs a batch that includes an
  unreadable file.

Scripted grading (no clicking) is no longer available.

## Test corpus and verification

**D33. Corpus made with AutoCAD 2026 `accoreconsole`, saved as 2018 format**
(AC1032, the format AutoCAD 2018 writes), plus 2013 and 2000 copies.
SECURELOAD refuses `(load …)` from the corpus folder; that security setting is
left alone and the LISP is inlined into each script. Additions beyond the spec's
22: `ref_flipped3d` (extrusion −Z), `ref_r2013`, `ref_r2000`, `kitchen` and
`kitchen_r2000` (every entity shape the loader handles), and
`ortho_similar_spaced` (the student side of corpus item 22), and the `hatch_*` and
`dim_*` sets for the optional hatch and dimension checks. Copy/paste (item
13) is an exploded `-INSERT *ref.dwg`, because the core console has no
clipboard.

**D34. Verification status.** On the development laptop a Defender for Endpoint
ASR rule (`01443614-…`, block low-prevalence executables) stops every freshly
built executable, so the unit tests were compiled but not run there. The corpus
expectations in the tests were cross-checked with an independent Python port of
the canonicalization rules on the same fixtures. CI runs the full test suite on
Linux, Windows and macOS. The instructor's own Qt Creator build does run them:
every suite passed there on 2026-11-05, before `tst_dims` was added.
