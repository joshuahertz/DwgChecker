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

In Absolute mode, where placement is specified, at most Medium, and only with
non-round coordinates. Orthographic mode compares per view and reports
"k of n views bit-identical". `ref_copypaste` vs `ref` is therefore Medium
(round coordinates); a synthetic copy at 13.48291034 is Strong.

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
