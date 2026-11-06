# DWG Checker: Implementation Spec

## Purpose

A Qt desktop app for grading CAD assignments. It loads one reference DWG (the solution) and a folder of student DWGs, reports how closely each student drawing matches the reference, and flags pairs of files that look shared or copied.

## Ground rules

- **Instrumentation first.** Before writing any comparison logic, build the dump tool (Milestone 0) and inspect real files. Do not assume JSON field names; verify them against the output of the pinned LibreDWG version.
- Keep a running `DECISIONS.md` recording design decisions and the evidence behind them.
- Put the core logic in a library that uses QtCore only (no widgets), with a CLI front end, so the logic is testable without the GUI.
- Prefer small, reviewable diffs.

## Stack

- C++20, Qt 6 Widgets, CMake.
- **Platforms:** Windows and macOS. Keep all platform-specific code (finding and launching AutoCAD, the location of `dwgread`) behind small interfaces in `platform/win` and `platform/mac`.
- **`dwgread` build:** build `dwgread` from source on both platforms, from the same pinned LibreDWG tag or commit, recorded in one place (for example, `third_party/libredwg.version`). This guarantees that both platforms parse files identically and produce the same JSON, and lets one patch fix both platforms. Use a single build script, `scripts/build_dwgread.sh`, with the same configure flags everywhere. Link it statically so there are no runtime libraries to ship. Cache the built binary in GitHub Actions, keyed on the version file plus a hash of the build script, so it rebuilds only when either one changes. Put any local patches in `third_party/patches/` and apply them in the build script.
  - **Windows:** run the script in an MSYS2/MinGW64 shell (`msys2/setup-msys2`). A toolchain mismatch with the MSVC-built app doesn't matter, because `dwgread` is a separate process.
  - **macOS:** produce a universal binary by building arm64 and x86_64, then joining them with `lipo`. Place the result in `Contents/Helpers/` and sign it with hardened runtime; notarization rejects any unsigned nested executable.
  - Log the `dwgread --version` output at startup and in every CSV export, so results can always be traced back to the exact parser version that produced them.
- **DWG reading:** LibreDWG's `dwgread` executable, run through `QProcess` with `-O JSON`, with the output parsed by `QJsonDocument`. Pin a specific LibreDWG release and bundle the binary next to the app. This approach was chosen for three reasons:
  - Running a separate process avoids GPL linking obligations.
  - It avoids building LibreDWG on Windows, since prebuilt release binaries exist.
  - It isolates crashes, so a malformed student file kills only the subprocess, not the app.
- Enforce a per-file timeout, and record any failure as a per-file error.
- Put the loader behind an `IDrawingLoader` interface so the ODA SDK, or a DXF route, can replace it later.
- **Documented fallback:** for files `dwgread` can't handle, rewrite them with the free ODA File Converter, then read the result.

## Data model

`DrawingMeta` holds the file-level metadata:

- path, SHA-256, size, DWG version
- `TDCREATE` / `TDUCREATE` / `TDUPDATE` / `TDUUPDATE`, `TDINDWG` (total editing time)
- `FINGERPRINTGUID`, `VERSIONGUID`, `HANDSEED`
- SummaryInfo: Author, LastSavedBy, RevisionNumber, custom properties
- AppInfo (the application that last saved the file)
- `INSUNITS`, entity count

`Primitive` is one canonical piece of geometry:

- kind: `Line`, `Arc`, `Circle`, `Text`, `Dimension`, or `Unsupported`
- geometry in 2D WCS (warn if any Z coordinate is nonzero)
- layer, source handle, source entity type
- linetype class: `Visible`, `Hidden`, `Center`, `Phantom`, or `Other` (see canonicalization rule 10)
- view ID (Orthographic mode only)

`Drawing` combines `DrawingMeta`, a `std::vector<Primitive>`, and a list of warnings.

## Canonicalization

Most of the correctness lives here. Students draw the same shape in many different ways, so canonicalization makes equivalent drawings compare equal.

1. Use model space only by default; paper space is an option. Skip the Defpoints layer.
2. Explode `INSERT` entities recursively, applying insertion point, scale, rotation, and extrusion, including nested blocks. If an arc gets non-uniform scaling, mark it `Unsupported` with a warning.
3. Convert `LWPOLYLINE` and `POLYLINE` into line and arc segments. Bulge values become arcs. The closed flag adds a closing segment.
4. Convert from OCS to WCS. Entities with extrusion (0,0,−1), which come from mirroring, have flipped arc angles. Cover this with a test.
5. Normalize arcs: counter-clockwise, angles in [0, 2π). An arc with a 360° sweep becomes a circle.
6. Merge collinear lines of the same linetype class that touch or overlap into maximal segments. This makes one line, a line split in two, and a polyline edge all compare equal. Drop zero-length segments. Remove exact duplicate (stacked) entities, and record a warning when you do.
7. Merge co-circular arcs that are contiguous, using the same approach.
8. Keep `TEXT`, `MTEXT`, and `DIMENSION` in separate categories, excluded from geometry matching by default.
   - A "check dimensions" option compares measured values.
   - Ignore hatches.
   - Report splines and ellipses as `Unsupported` in v1.
9. Provide an optional layer-ignore regex, for example for construction layers.
10. Resolve each entity's effective linetype, following ByLayer and ByBlock, and map it to a class using configurable name patterns:
    - `HIDDEN*` / `DASHED*` → Hidden
    - `CENTER*` → Center
    - `PHANTOM*` → Phantom
    - `CONTINUOUS` → Visible

    Linetype class is part of every match, so a hidden edge drawn as a visible line is an error. Ignore linetype scale (`LTSCALE`, `CELTSCALE`).

## Comparison

### Settings

| Setting | Default |
|---|---|
| Mode | `Absolute`, `Relative`, or `Orthographic` |
| Match linetype class | on |
| Allow rotation (Relative mode) | on |
| Allow mirror | off |
| Linear tolerance | 1e-4 drawing units |
| Angular tolerance | 1e-6 rad |
| Require layer match | off |
| Extras policy | penalize (alternative: report only) |
| Units check | an `INSUNITS` mismatch produces a flag; never auto-scale |

### Matching under a transform T

1. Transform the student's primitives into the reference frame.
2. For each reference primitive, find an unused student primitive of the same kind whose parameters are all within tolerance:
   - Line: endpoints, in either order.
   - Circle: center and radius.
   - Arc: center, radius, and start/end angles.
3. Use a spatial hash grid for lookups. Matching is one-to-one. Greedy matching is fine because the tolerance is tiny; warn if a match is ambiguous.

### Absolute mode

Use T = identity.

### Relative mode

Find the best rigid transform T:

1. **Signatures.** Compute a signature for every primitive: (kind, length or radius, arc sweep), bucketed by tolerance.
2. **Anchor.** Choose an anchor in the reference: the primitive whose signature is rarest in the student drawing (but present at least once). Prefer lines and arcs, because they fix rotation. Break ties by size.
3. **Candidate transforms.** For each student primitive with the same signature, compute the transform(s) that align it with the anchor:
   - A line gives two candidates (one per endpoint ordering). With rotation disallowed, keep only candidates with matching direction.
   - An arc gives one candidate.
   - A circle gives translation only. If rotation is allowed, a second anchor fixes the rotation.
4. **Scoring.** Score each candidate T by its match count. Keep the best T and report it as (dx, dy, θ).

Always compute the Absolute result as well, because duplicate detection uses it.

### Orthographic mode

The drawing contains several views, typically front, top, and right. The rules are:

- Each view must be correct internally.
- The spacing between views is free.
- Views must stay in projection alignment.

#### Segmenting the reference into views

Only the reference is segmented. Student drawings are never segmented, because projection lines and stray construction lines bridge the gaps between views and break clustering.

- **Auto-segment.** Cluster the reference by single-linkage on primitive bounding boxes. The default gap threshold is 2% of the drawing's diagonal, adjustable with a live preview.
- **Attach centerlines afterward.** Centerlines extend past outlines, so cluster on Visible and Hidden primitives first. Then attach each Center and Phantom primitive, and each dimension, to its nearest view.
- **Instructor confirmation.** The instructor confirms or fixes the segmentation in a view-assignment editor:
  - each view shown in its own color
  - lasso to reassign primitives
  - name each view Front, Top, Right, Left, Bottom, Rear, or Other
- **Reuse.** Save the segmentation as a sidecar file, `ref.dwg.views.json`, keyed by the reference's SHA-256 so it is reused on later runs.

#### Matching each view against the whole student drawing

1. **Per-view candidates.** For each reference view, align that view alone against *all* student primitives using the Relative-mode alignment, translation only. Keep the top K candidate translations (default K = 5) along with their match counts.
2. **Joint resolution.** By construction, views share sizes; for example, the front and top views have the same width. That means one view's anchor can land in the wrong student view. Resolve this jointly:
   - Evaluate every combination of candidates across views. There are K^n combinations, which is small.
   - Enforce one-to-one primitive claims across views.
   - Pick the combination with the most total matches. Break ties with the alignment and arrangement checks below.
3. **Extras.** Student primitives not claimed by any view are extras.

#### Centerlines

Match centerlines on position and direction only. Check their length only when the "strict centerlines" setting is on, because the extension past the outline is often drawn by eye.

#### Alignment and arrangement checks

These are computed from the per-view translations (dx, dy), using the view names:

- **Projection alignment.**
  - Top and Bottom must align with Front horizontally: dx_view = dx_front, within tolerance.
  - Right, Left, and Rear must align with Front vertically: dy_view = dy_front.
  - The gaps between views are unconstrained.
- **Arrangement.** Each view must sit on the same side of Front as it does in the reference. This catches first-angle vs. third-angle swaps.
- **Severity.** Each check has its own setting: Fail, Warn, or Off.

#### Per-view results

For each view, report pass/fail, % matched, and the missing and extra primitives. Also report the alignment and arrangement results.

### Per-student result

Each student gets a correctness value, an error list, and some internal data.

#### Correctness value

Correctness % = matched ÷ (reference primitives + penalized extras) × 100. In Orthographic mode, counts are pooled across views, so larger views weigh more.

The student passes only if the correctness value is 100% and no check set to Fail has failed.

#### Error list

The error list is a list of human-readable strings, each naming its view in Orthographic mode. Build it in this order:

1. **Pair up near-misses.** Before reporting anything as missing or extra, pair each unmatched reference primitive with the closest unmatched student primitive of the same kind (within a configurable search radius). Report each pair as one error, not as one missing plus one extra. For example:
   - "Front view: circle Ø0.750 drawn as Ø0.740"
   - "Top view: line 2.500 long is 2.375 long"
   - "Right view: hole center off by (0.125, 0)"
2. **Report wrong linetypes.** When the geometry matches but the linetype class differs, report a linetype error. For example: "Top view: hidden edge drawn as visible line".
3. **Report what remains** as missing or extra, giving size and location. For example: "Front view: missing hidden line 1.250 long at (3.0, 2.5)".
4. **Then report the rest:** alignment and arrangement failures, then warnings (unsupported entities, units mismatch, stacked duplicates).

Each error keeps a reference to its primitives so the GUI can zoom to it.

#### Internal data

Keep the transforms used, for the detail view and for duplicate detection.

## Duplicate / collaboration detection

Compare files pairwise across all students. Also compare each student file against the reference and any template files, because a student submitting the instructor's file, or a file derived from it, is a key case.

### Signals, strongest first

1. **Identical SHA-256:** byte-identical file.
2. **Identical `VERSIONGUID`:** same saved version. This GUID changes on every save.
3. **Identical `FINGERPRINTGUID`:** derived from the same original file. This GUID is set at creation and survives Save As and editing.
   - **Exception:** if a starter or template DWG was distributed, every file derived from it shares its fingerprint. A "Template files" setting whitelists their fingerprints.
   - Also, if more than ~30% of the class shares a fingerprint, display it as a probable template instead of flagging every pair.
4. **Identical `TDCREATE`** (to the millisecond).
5. **Shared identity fields:** the same LastSavedBy or Author on two different students' files, or a LastSavedBy that matches a different student's name.
6. **Bit-exact absolute geometry.** Count primitives that match between two students at full double precision, with T = identity and no tolerance. Copy/paste preserves the exact bits, but independent redraws almost never share placement. This signal is strong in Relative-mode assignments and weak when placement is specified. In Orthographic mode, compute it per view: view spacing is free, so two students with bit-identical positions for every view is a strong signal.
7. **Informational only:**
   - identical entity creation order or handle patterns
   - identical non-required layer names, colors, or linetypes
   - identical original filenames (from the Canvas suffix)
   - a very small `TDINDWG` for a complete drawing

### Presentation

- Give each signal a weight and show which signals fired for each pair, along with a combined score.
- Show clusters: groups of files connected by strong signals.
- Present the evidence only. The app never labels anything as cheating.

## Student identity

Parse student identity from filenames using a configurable regex. The default matches Canvas bulk downloads:

`^(?<name>[a-z]+)_(?:LATE_)?(?<uid>\d+)_(?<sid>\d+)_(?<orig>.+)$`

If the regex doesn't match, fall back to the bare filename.

## GUI

### Top bar

- reference file picker
- student folder picker
- template files list
- collapsible settings panel
- Run button

### Results tab

This tab is the main output: a sortable table with one row per student DWG file.

#### Columns

1. **File.** The student name parsed from the filename, plus the filename itself, shown as a clickable link that opens the file in AutoCAD.
   - The link always opens the file in AutoCAD, never with the OS file association.
   - **Locating AutoCAD.** On first run, auto-detect installed versions (full AutoCAD and LT) and default to the newest. The user can choose a different version in Settings. If nothing is found, prompt the user to locate it. Store the choice in `QSettings`.
     - Windows: enumerate `HKLM\SOFTWARE\Autodesk\AutoCAD\R*\ACAD-*`, reading the `AcadLocation` value. R22.0 is 2018. The executable is `acad.exe`, or `acadlt.exe` for LT.
     - macOS: scan `/Applications/Autodesk/` for `AutoCAD*.app` bundles.
   - **Launching on Windows.** Running `acad.exe <file>` starts a new AutoCAD session on every click, which is slow. So:
     1. First try attaching to a running AutoCAD through COM (`GetActiveObject` on the `AutoCAD.Application` ProgID, wrapped with ActiveQt `QAxObject`).
     2. If attached, call `Documents.Open(path, ReadOnly = true)`.
     3. If no AutoCAD is running, launch `acad.exe` with the path via `QProcess`.

     Verify the COM attach behavior against AutoCAD 2018 in M6 before relying on it.
   - **Launching on macOS.** Run `open -a "<AutoCAD app path>" <file>` via `QProcess`. This reuses a running instance automatically.
   - **Protect the evidence:** by default, open a *copy* of the file in a temp folder, never the submission itself. Saving the original in AutoCAD would rewrite `VERSIONGUID`, `TDUPDATE`, and LastSavedBy. A setting can switch this off.
   - Record each file's SHA-256 at scan time. If a file has changed since the scan, show a warning and offer to re-scan.
2. **Copy flag.** Shows none, weak, or strong, plus the partner file(s) of the strongest signal. For example: "⚠ same fingerprint as smithjane". A tooltip lists every signal that fired. Clicking the flag opens that pair in the Duplicates tab.
3. **Correctness.** The correctness % with a pass/fail color.
4. **Errors.** The error list as a multi-line cell, using word wrap and an auto-sized row height.

#### Behavior

- Double-clicking a row opens the detail view. Clicking a single error opens the detail view zoomed to that error.
- CSV export uses the same four columns, with the errors joined by "; ".

### Detail view

- **Overlay:** a `QGraphicsView` showing the reference (gray) and the student drawing transformed into the reference frame. Matched primitives are green, missing ones red, extras orange. Zoom and pan are supported.
- **Discrepancy list:** clicking an item zooms to it.
- **Metadata panel:** the student file's metadata shown side by side with the reference's.
- **Orthographic mode:** outline each matched view and show per-view results and alignment checks. The reference view-assignment editor opens from the top bar.

### Duplicates tab

- a table of flagged pairs, with the per-pair breakdown of signals
- a cluster list

### Other behavior

- Export CSV for both results and duplicates.
- Run the batch on a worker thread (`QtConcurrent`) with a progress bar and a Cancel button. An error in one file never stops the batch.
- Persist the last-used folders and settings with `QSettings`.

## CLI

```
dwgcheck --dump file.dwg                  # metadata + canonical primitives
dwgcheck --ref ref.dwg --dir subs/ --mode relative --tol 1e-4 --csv out.csv
```

## Milestones

- **M0, Field map.** Run `dwgread -O JSON` on several real files. Write `FIELDMAP.md` recording where each needed value lives in the JSON, and commit the sample JSON to `tests/fixtures/`.
- **M1, Loading.** Loader, data model, metadata, and `--dump`.
- **M2, Canonicalization.** Implement it with unit tests for each rule above.
- **M3, Absolute comparison.** Absolute compare plus CSV output from the CLI.
- **M4, Relative alignment.**
- **M4b, Orthographic mode.** Linetype classes, reference segmentation with the sidecar file, joint per-view matching, and alignment checks.
- **M5, Duplicate detection.**
- **M6, GUI.**
- **M7, Packaging.** Bundle `dwgread`, then run `windeployqt` / `macdeployqt`. Build both platforms in GitHub Actions. On macOS, produce a universal build and sign and notarize the app, including the nested `dwgread`.

## Test corpus

Create these files in AutoCAD 2018:

1. `ref.dwg`
2. ref translated
3. ref rotated 37°
4. ref mirrored
5. ref with one line split into two
6. a rectangle drawn as a polyline vs. as 4 lines
7. ref with one entity missing
8. ref plus one extra construction line
9. ref with one size off by 0.01
10. ref drawn in mm vs. inches
11. a version built from blocks
12. Save As copy of ref (same fingerprint)
13. ref copy/pasted into a new drawing (new fingerprint, bit-identical coordinates)
14. an independent redraw of ref
15. a byte copy of ref
16. `ortho_ref.dwg` (front, top, and right views)
17. ortho ref with the view spacing changed
18. ortho ref with the top view shifted sideways (misaligned)
19. ortho ref in first-angle arrangement
20. ortho ref with one hidden line drawn as continuous
21. ortho ref with projection lines left in, on the object layer
22. a simple block whose front and top views are nearly identical (exercises joint resolution)
