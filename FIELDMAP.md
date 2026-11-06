# FIELDMAP: where each value lives in `dwgread -O JSON` output

Pinned parser: **LibreDWG 0.14** (`dwgread 0.14`). Every entry below was checked
against real output of that binary on the corpus in `tests/corpus/dwg/`, which was
authored in AutoCAD 2026 (`accoreconsole`) and saved as AutoCAD 2018 (AC1032),
2013 (AC1027) and 2000 (AC1015). The JSON dumps are committed in `tests/fixtures/`.
Entries marked *(source only)* were read from the 0.14 source but no corpus
file exercises them yet.

Regenerate the evidence with:

```
tests/corpus/gen/make_corpus.ps1                 # AutoCAD 2026 core console
tests/corpus/gen/dump_fixtures.ps1 -DwgRead <path to dwgread>
```

## Conventions

| Thing | JSON shape | Notes |
|---|---|---|
| Own handle | `"handle": [code, size, value]` | use the **last** element |
| Handle reference | `[code, size, value, absolute_ref]` | use the **last** element (absolute ref). The `value` part can be relative (e.g. `ownerhandle: [12, 1, 2, 630]` → owner is 630) |
| Null reference | `[5, 0, 0, 0]` or `[4, 0, 0, 0]` | absolute ref 0 |
| Dates (`TIMEBLL`) | `[julian_day, milliseconds]` | e.g. `TDUCREATE: [2461350, 49490000]` = 2026‑11‑05 13:44:50 UTC. Every corpus value is a multiple of 1000 ms, i.e. AutoCAD stores **whole seconds** |
| Durations | `[days, milliseconds]` | `TDINDWG: [0, 1000]` = 1 s |
| Doubles | printed with `%.14f`, trailing zeros trimmed | **14 digits after the decimal point, not round-trip precision.** `π/2` → `1.5707963267949`. See patch `0002` |
| Count fields | any field whose name starts with `num_` is **omitted** by the JSON writer (`out_json.c` `FIELD` macro); arrays carry their own length | breaks MINSERT, see below |
| Entities vs objects | entities have `"entity": "<TYPE>"`, non-entities have `"object": "<TYPE>"` | all live in the flat `OBJECTS` array |

## Top level

| Key | Present | Content |
|---|---|---|
| `created_by` | all | `"LibreDWG 0.14"` |
| `FILEHEADER.version` | all | `AC1015` (R2000), `AC1018` (R2004), `AC1021` (R2007), `AC1024` (R2010), `AC1027` (R2013), `AC1032` (R2018) |
| `HEADER` | all | header variables (object, ~300 keys) |
| `OBJECTS` | all | every object/entity, file order |
| `SummaryInfo` | R2004+ only | absent for AC1015 |
| `AppInfo` | R2004+ only (seen in AC1027, AC1032) | absent for AC1015 |

## DrawingMeta

| Field | Location | Evidence / caveat |
|---|---|---|
| DWG version | `FILEHEADER.version` | |
| `TDCREATE` | **not in `HEADER`** (key absent in 0.14 output for AC1015 and AC1032). Use `SummaryInfo.TDCREATE` | in every R2004+ corpus file `SummaryInfo.TDCREATE == HEADER.TDUCREATE`, i.e. it is UTC |
| `TDUCREATE` | `HEADER.TDUCREATE` | all versions |
| `TDUPDATE` | `SummaryInfo.TDUPDATE` (not in `HEADER`) | |
| `TDUUPDATE` | `HEADER.TDUUPDATE` | |
| `TDINDWG` | `HEADER.TDINDWG` (also `SummaryInfo.TDINDWG`) | `ref_redraw` has `[0, 0]` |
| `FINGERPRINTGUID` | `HEADER.FINGERPRINTGUID` (string `"{…}"`) | survives SAVEAS and down-saves (`ref`, `ref_saveas`, `ref_r2013`, `ref_r2000` share it); every new drawing from `acad.dwt` gets a new one; `ref_copypaste` (exploded insert of ref into a new drawing) has a new one |
| `VERSIONGUID` | `HEADER.VERSIONGUID` | differs on every save, identical only for `ref_bytecopy` |
| `HANDSEED` | `HEADER.HANDSEED` → element `[2]` | `[0, 2, 669, 669]` |
| `INSUNITS` | `HEADER.INSUNITS` | 1 = inches, 4 = mm (`ref_mm`) |
| Author | `SummaryInfo.AUTHOR` | empty in corpus |
| LastSavedBy | `SummaryInfo.LASTSAVEDBY` | Windows login (`"jhertz"`) |
| RevisionNumber | `SummaryInfo.REVISIONNUMBER` | |
| Title/Subject/Keywords/Comments/HyperlinkBase | `SummaryInfo.TITLE` … `HYPERLINKBASE` | |
| Custom properties | `SummaryInfo.props: [{"tag": …, "value": …}]` *(source only: `summaryinfo.spec`)* | key absent when there are none |
| AppInfo | `AppInfo.comment` is XML-ish: `<ProductInformation name ="AutoCAD" build_version="W.179.0.0(x64)" registry_version="25.1" install_id_string="ACAD-9101:409" …`; `AppInfo.version` is the "Trusted DWG" sentence | parse `name`, `build_version`, `registry_version` with a regex |
| Entity count | number of top-level model-space entities (`entmode == 2`) | |
| `LTSCALE`, `CELTSCALE` | `HEADER.LTSCALE`, `HEADER.CELTSCALE` | ignored by design |

## Ownership and spaces

| Question | Answer |
|---|---|
| Is an entity in model space? | `entmode == 2` (1 = paper space, 0 = owned by a block via `ownerhandle`) |
| Block contents, R2004+ | `BLOCK_HEADER.entities: [href…]` in drawing order |
| Block contents, R2000 | no `entities` array; `BLOCK_HEADER.first_entity` / `last_entity` only. Collect entities whose `ownerhandle` → that block header, in file order |
| Block name / base point | `BLOCK_HEADER.name`, `BLOCK_HEADER.base_pt` |
| Anonymous dimension blocks | `BLOCK_HEADER.name == "*D"`, `anonymous: 1`; contents are LINE/SOLID/MTEXT/POINT with `entmode 0` (never collected for model space) |
| Skip in block contents | `BLOCK`, `ENDBLK`, `SEQEND`, `ATTDEF` |

## Tables

| Table | Fields |
|---|---|
| `LAYER` | `name`, `ltype` (href), `flag0` bits: **1 = frozen, 2 = off, 8 = locked, 16 = plot** (verified: FROZEN 1009, OFFLAYER 1010, LOCKED 1016, Defpoints 992 = no plot); bits 5‑9 hold linewt |
| `LTYPE` | `name` (`ByBlock`, `ByLayer`, `Continuous`, `HIDDEN`, `CENTER`, …), `numdashes` |
| `BLOCK_HEADER` | see above |
| `DIMSTYLE` | `name`, `DIMDEC`, `DIMADEC` (−1: angles use `DIMDEC`), `DIMZIN`, `DIMAZIN`, `DIMLUNIT` (1 scientific, 2 decimal, 3 engineering, 4 architectural, 5 fractional, 6 Windows desktop), `DIMAUNIT` (0 decimal degrees, 1 deg/min/sec, 2 gradians, 3 radians, 4 surveyor's), `DIMDSEP` (character code, 46 = `.`), `DIMLFAC`, `DIMRND`, … A sub-style made for one kind of dimension is named `<style>$<n>` (`DC3$4` for radius) and **the dimension references it directly** (`dim_family`). See "DIMENSION text and precision" |

## Common entity fields

| Field | Meaning |
|---|---|
| `layer` | href to LAYER |
| `ltype_flags` | **0 = ByLayer, 1 = ByBlock, 2 = Continuous, 3 = explicit** (then `ltype` href is present). Verified on kitchen (explicit HIDDEN → 3, BYBLOCK → 1) and ref_blocks (explicit Continuous → 2, explicit CENTER on an INSERT → 3) |
| `ltype` | href to LTYPE, only when `ltype_flags == 3` |
| `extrusion` | `[x, y, z]`; `(0,0,-1)` after ROTATE3D/flip (`ref_flipped3d`). MIRROR in plan keeps `(0,0,1)` and rewrites the angles (`ref_mirrored`) |
| `ownerhandle` | only when `entmode == 0` |

## Geometry entities

| Entity | Fields | Coordinate system |
|---|---|---|
| `LINE` | `start`, `end` (3D) | **WCS** (even with extrusion −Z) |
| `CIRCLE` | `center` (3D), `radius` | center in **OCS** |
| `ARC` | `center`, `radius`, `start_angle`, `end_angle` (radians, CCW in OCS) | **OCS**. `ref_flipped3d`: WCS arc 270°→360° about (3.5,−1.5) is stored as center (−3.5,−1.5), 180°→270°, extrusion −Z |
| `LWPOLYLINE` | `points: [[x,y]…]` (2D), `bulges: [...]` (one per vertex, may be absent), `flag`, `const_width`, `elevation` (when flag & 8), `extrusion` (when flag & 1) | **OCS**. `flag & 512` = **closed** (529 = closed+bulges+extrusion; 20 = bulges+const width) |
| `POLYLINE_2D` | `flag` (& 1 = closed, & 2 = curve-fit, & 4 = spline-fit), `elevation`, `extrusion`, `vertex: [href…]`, `seqend` | |
| `VERTEX_2D` | `point` (3D, OCS), `bulge`, `flag` (& 8 curve-fit vertex, & 16 spline frame) | OCS |
| `POLYLINE_3D` / `VERTEX_3D` | `vertex` hrefs / `point` | WCS *(source only)* |
| `INSERT` | `ins_pt` (OCS), `scale` `[x,y,z]`, `rotation` (rad), `extrusion`, `block_header` (href), `has_attribs` | mirrored insert has `scale: [-1, 1, 1]` |
| `MINSERT` | as INSERT plus `col_spacing`, `row_spacing` — **`num_cols` / `num_rows` are missing** (dropped by the `num_` rule) | treated as Unsupported unless patch `0001` is applied |
| `TEXT` | `ins_pt` (2D OCS), `height`, `text_value`, `rotation` | |
| `MTEXT` | `ins_pt` (3D WCS), `text`, `text_height`, `x_axis_dir` | |
| `DIMENSION_LINEAR` / `_ALIGNED` | `xline1_pt`, `xline2_pt`, `def_pt` (WCS), `text_midpt` (2D OCS), `act_measurement`, `user_text`, `block` (href to `*D` block), `dimstyle` (href), `dim_rotation` (linear), `eed` (overrides) | |
| `DIMENSION_DIAMETER` / `_RADIUS` | `def_pt`, `first_arc_pt`, `act_measurement`, `leader_len` | |
| `DIMENSION_ANG3PT` | `act_measurement` **in radians** (1.5708 for the 90° corner arc, `dim_ref`) | |
| `ARC_DIMENSION` | `act_measurement` is the **arc length** (0.785 for the R0.5 quarter arc, `dim_ref`), `xline1_pt`, `xline2_pt`, `center_pt`, `arc_start_param`, `arc_end_param`, `is_partial` | a length, not an angle |
| `DIMENSION_ANG2LN`, `_ORDINATE` | `act_measurement` *(source only)* | |
| `HATCH` | see the HATCH section below | read as a filled region; compared only with "check hatches" |
| `SPLINE` | `degree`, `fit_pts` / `ctrl_pts`, `knots` | Unsupported in v1 |
| `ELLIPSE` | `center`, `sm_axis`, `axis_ratio`, `start_angle`, `end_angle` | Unsupported in v1 |
| `XLINE` / `RAY` | `point`, `vector` | Unsupported (infinite construction line) |
| `POINT` | `x`, `y`, `z` | ignored |
| `SOLID`, `VIEWPORT`, `LEADER`, `MLEADER`, `TOLERANCE`, `WIPEOUT` | — | ignored (annotation / paper space) |

## HATCH

Evidence: `hatch_*.dwg` in the corpus (AutoCAD 2026, saved as 2018), plus the hatch in `kitchen.dwg`.

| Field | Meaning / evidence |
|---|---|
| `name` | pattern name (`ANSI31`, `ANSI37`). A solid fill made with the `_Solid` option is stored as **`_SOLID`** (`hatch_solid`), so names are upper-cased and leading `_` stripped |
| `is_solid_fill`, `is_gradient_fill`, `gradient_name` | solid / gradient flags; `gradient_name` is present (`LINEAR`) even for pattern hatches |
| `style` | 0 normal, 1 outer, 2 ignore islands. Every corpus hatch has 1 |
| `angle`, `scale_spacing`, `pattern_type`, `deflines` | pattern angle and scale (`hatch_scale_angle`: scale 2, 30°); not compared |
| `is_associative`, `seeds`, `pixel_size` | informational |
| `elevation`, `extrusion` | the paths are 2D in this OCS |
| `paths[]` | one per boundary loop. `flag`: 1 external, 2 polyline, 4 derived, 16 outermost (island loops have 22 = polyline + derived + outermost); 32 appears on drawn boundaries (`hatch_partial`, flag 33), meaning unknown |
| polyline path (`flag & 2`) | `polyline_paths: [{point: [x,y], bulge?}]`, `bulges_present`, `closed`. **Picking an internal point produces polyline paths even around circles**: the hole in `hatch_ref` is two points with `bulge: -1` (two clockwise semicircles) |
| edge path (no `flag & 2`) | `segs: [{curve_type, ...}]`. **Selecting objects produces edge paths** (`hatch_edges_select`, `kitchen`). `curve_type` 1 line: `first_endpoint`, `second_endpoint`; 2 arc: `center`, `radius`, `start_angle`, `end_angle`, `is_ccw`; 3 ellipse: `center`, `endpoint` (major axis), `minor_major_ratio`, angles, `is_ccw`; 4 spline: `control_points[{point, weight?}]`, `fitpts` *(3 and 4 source only)* |
| clockwise edge arc | **angles are stored negated.** `hatch_edges_cw` (notch arc running clockwise from (2.5,2) through (2,1.5) to (1.5,2)) is stored `start_angle 6.283`, `end_angle 9.425`, `is_ccw 0`: the real arc runs clockwise from −2π to −3π |
| drawn boundary | the `draW` option stores an **open** edge path: 3 line segments for a 4-corner polygon (`hatch_partial`, `hatch_spill`); the closing edge is implied |
| hatch in a block | `entmode 0`, owned by the block like any other entity (`hatch_block`) |

## DIMENSION text and precision

Evidence: `dim_*.dwg` (AutoCAD 2026, saved as 2018). The precision check formats
`act_measurement` from these settings; `tst_dims` compares the result with the text
AutoCAD generated for every dimension in the fixtures.

| What | Where |
|---|---|
| style | `dimstyle` href → `DIMSTYLE` (Tables) |
| per-dimension override (Properties palette, `DIMOVERRIDE`) | `eed` of the dimension: `{code 0, "DSTYLE"}` (the item carrying the ACAD appid `handle`), `{code 2, 0}` = "{", then pairs `{code 70, value: DXF group}` + `{code 70/40/1, value}`, then `{code 2, 1}` = "}". `dim_override`: `271, 2` = DIMDEC 2. The next item with its own `handle` starts another application's data (radial dimensions carry one with groups 387/388/390) |
| DXF groups used | 271 DIMDEC, 179 DIMADEC, 78 DIMZIN, 79 DIMAZIN, 277 DIMLUNIT, 275 DIMAUNIT, 278 DIMDSEP, 144 DIMLFAC, 45 DIMRND |
| typed-over text | `user_text`: `"4.00"` replaces the measurement, `"<> TYP"` keeps it (`dim_text`) |
| generated text | the `MTEXT` owned by the `block` (`*D`): `\A1;4.000`, `\A1;∅0.750` (U+2205), `\A1;R0.500`, `\A1;90°`; trailing zeros suppressed (`DIMZIN 8`): `4`, `1.5`; fractional: `1{\H1x;\S1/2;}`, `{\H1x;\S5/8;}`; architectural adds `"`. Halves round up: 0.625 at two places reads `0.63` (`dim_prec2`) |

## Process behaviour

- `dwgread -O JSON <file>` writes JSON to stdout and `SUCCESS` (or errors) to **stderr**, exit code 0 on success.
- `-O minJSON` produces the same structure without whitespace (verified identical objects on kitchen); ~35 % smaller. The loader uses `minJSON`.
- `dwgread --version` prints `dwgread 0.14`. It does not reveal local patches, so the app also logs the SHA-256 of the binary.
- Release builds (`IS_RELEASE`) print NaN as `0.0`; non-release builds would print bare `nan`, which is invalid JSON. The loader sanitizes `nan`/`inf` tokens and retries if strict parsing fails.
