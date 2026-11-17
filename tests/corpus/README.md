# Test corpus

Generated with AutoCAD 2026's core console (`gen/make_corpus.ps1`, geometry in
`gen/corpus.lsp`) and saved in AutoCAD 2018 format unless noted. JSON dumps
made with dwgread 0.14 are in `../fixtures/` (`gen/dump_fixtures.ps1`).

Reference plate (`ref.dwg`, inches): 4 × 2 plate with an R0.5 top-right corner,
holes Ø0.75 at (1,1) and Ø0.5 at (2.75,1), a hidden line at x = 2, centerlines
on the large hole, a text label, and linear plus diameter dimensions.

| # (spec) | File | What it is |
|---|---|---|
| 1 | `ref.dwg` | reference |
| 2 | `ref_translated.dwg` | moved by (5.25, 3.125) |
| 3 | `ref_rotated37.dwg` | rotated 37° about the origin |
| 4 | `ref_mirrored.dwg` | MIRROR about the Y axis (source erased) |
| 4b | `ref_flipped3d.dwg` | ROTATE3D 180° about X: every entity gets extrusion (0,0,−1) |
| 5 | `ref_split.dwg` | bottom edge drawn as two lines |
| 6 | `rect_lines.dwg`, `rect_pline.dwg` | 3 × 2 rectangle as 4 lines vs a closed LWPOLYLINE |
| 6b | `ref_polyline.dwg` | plate outline as one closed LWPOLYLINE with a bulge |
| 7 | `ref_missing.dwg` | Ø0.5 hole missing |
| 8 | `ref_extra.dwg` | extra line on layer CONSTRUCTION |
| 9 | `ref_sizeoff.dwg` | Ø0.75 hole drawn Ø0.74 |
| 10 | `ref_mm.dwg` | scaled ×25.4, INSUNITS = mm |
| 11 | `ref_blocks.dwg` | built from nested blocks (rotated, scaled, ByBlock, layer-0 inheritance) |
| 12 | `ref_saveas.dwg` | Save As of ref (same fingerprint) |
| 13 | `ref_copypaste.dwg` | ref exploded into a new drawing (new fingerprint, bit-identical coordinates) |
| 14 | `ref_redraw.dwg` | independent redraw at another origin, different order and directions |
| 15 | `ref_bytecopy.dwg` | byte copy |
| — | `ref_r2013.dwg`, `ref_r2000.dwg` | ref saved down to 2013 / 2000 format |
| 16 | `ortho_ref.dwg` | stepped block with a through hole: front, top and right views (third angle) |
| 17 | `ortho_spacing.dwg` | view spacing changed |
| 18 | `ortho_misaligned.dwg` | top view shifted 0.25 sideways |
| 19 | `ortho_firstangle.dwg` | first-angle arrangement |
| 20 | `ortho_hidden_visible.dwg` | one hidden line drawn continuous |
| 21 | `ortho_projlines.dwg` | projection lines left in on the object layer |
| 22 | `ortho_similar.dwg`, `ortho_similar_spaced.dwg` | front and top views nearly identical; the second has different spacing |
| — | `kitchen.dwg`, `kitchen_r2000.dwg` | field-map file: one of every entity shape the loader handles |

Hatch set (for "check hatches"). Region A is a 4 × 2 plate minus a Ø0.75 hole,
hatched ANSI31. Region B is a 1 × 1 square at (5,0), hatched ANSI37.

| File | What it is |
|---|---|
| `hatch_ref.dwg` | reference: A and B hatched by picking a point |
| `hatch_missing_b.dwg` | B not hatched |
| `hatch_pattern.dwg` | A hatched ANSI37 |
| `hatch_scale_angle.dwg` | right patterns, different scale and angle |
| `hatch_partial.dwg` | only the right half of A hatched (drawn boundary) |
| `hatch_extra.dwg` | the hole hatched as well |
| `hatch_spill.dwg` | A hatched by a drawn rectangle with no island, so the hole is filled |
| `hatch_split.dwg` | A hatched as two separate hatches |
| `hatch_solid.dwg` | A solid-filled |
| `hatch_rotated.dwg` | everything rotated 37° |
| `hatch_block.dwg` | everything in a block inserted at (10,5), rotated 30° |
| `hatch_edges_pick.dwg`, `hatch_edges_select.dwg` | ref plate outline hatched by picking a point vs. by selecting objects (polyline vs. edge paths) |
| `hatch_edges_cw.dwg` | rectangle with a notch: a clockwise edge arc |

Dimension set (for "check dimensions" and its precision option). The reference
plate's outline and holes with seven dimensions: 4.000 overall, 1.500 right edge,
0.625 edge to hole, Ø0.750, R0.500, the 90° corner arc, and the corner's arc
length 0.785.

| File | What it is |
|---|---|
| `dim_ref.dwg` | reference: style DC3, three decimal places, whole degrees |
| `dim_prec2.dwg` | style with two decimal places |
| `dim_angprec.dwg` | angles with one decimal place |
| `dim_zin.dwg` | trailing zeros suppressed (`4`, `1.5`, `R0.5`) |
| `dim_override.dwg` | DC3, with DIMDEC 2 overridden on the 1.500 dimension only |
| `dim_text.dwg` | "4.00" typed over the 4.000 dimension; "<> TYP" on the 1.500 one |
| `dim_frac.dwg`, `dim_arch.dwg` | fractional and architectural units at 1/16 |
| `dim_family.dwg` | DC3 with a radius sub-style (DC3$4) at one decimal place |

Color set (for "line colors must match" and "set by layer"). Eight horizontal
lines 4 long at y = 0…7: by layer (white), ACI 1 on the object, true color
255,0,0 on the object, on layer RED, on layer TC (true color 200,10,10), with
an explicit HIDDEN linetype, and a ByBlock line in a block inserted in ACI 3
and inserted by layer.

| File | What it is |
|---|---|
| `color_ref.dwg` | reference, as above |
| `color_wrong.dwg` | the ACI 1 line drawn in ACI 5 (blue) |
| `color_near.dwg` | the 255,0,0 true color drawn as 250,5,5 (2 % different) |
| `color_bylayer.dwg` | the same colors and linetypes, every one set by layer (layers RED, HID7, GREEN) |

Ellipse set (D98). A 6 × 3 plate with a whole ellipse 2 × 1 at (1.5,1.5), its
major axis along (0.8,0.6) (36.87°), and the left half of an ellipse 2 × 1.2 at
(4.5,1.5) with an upright major axis, from (4.5,2.5) through (3.9,1.5) to
(4.5,0.5).

| File | What it is |
|---|---|
| `ell_ref.dwg` | reference |
| `ell_translated.dwg`, `ell_rotated37.dwg` | moved by (5.25, 3.125); rotated 37° about the origin |
| `ell_mirrored.dwg`, `ell_flipped3d.dwg` | MIRROR about the Y axis; ROTATE3D 180° about X (both give the ellipses extrusion −Z) |
| `ell_redraw.dwg` | lines in another order and direction, each ellipse from the other end of its major axis |
| `ell_block.dwg` | both drawn as a circle and an arc in blocks inserted at uneven scales |
| `ell_sizeoff.dwg` | whole ellipse 2 × 0.9 |
| `ell_shifted.dwg` | whole ellipse at (1.6, 1.5) |
| `ell_turned.dwg` | whole ellipse turned 10° about its center |
| `ell_arcangle.dwg` | half ellipse ending at parameter 0.9π |
| `ell_circle.dwg` | whole ellipse drawn as a circle Ø1.5 |

Spline set (D99). A 6 × 3 plate whose top edge is a spline from (6,3) through
(4.5,3.4) (3,2.8) (1.5,3.3) to (0,3), and a closed spline through (2,1) (3,1.6)
(4,1.2) (3.2,0.6) inside it. The core console's SPLINE is the older command (fit
points, chord knots, no Knots or Method options).

| File | What it is |
|---|---|
| `spl_ref.dwg` | reference |
| `spl_translated.dwg`, `spl_rotated37.dwg`, `spl_mirrored.dwg` | moved by (5.25, 3.125); rotated 37°; MIRROR about the Y axis |
| `spl_reversed.dwg` | the top edge drawn from (0,3) to (6,3) |
| `spl_block.dwg` | the part at half size as a block, inserted at scale 2 |
| `spl_cv.dwg` | both splines remade from AutoCAD's own control points (entget), without fit points |
| `spl_pointoff.dwg` | the top edge's third point at (3, 2.7) |
| `spl_fewer.dwg` | the top edge through 4 points (no (3, 2.8)) |
| `spl_tangent.dwg` | the top edge with end tangents given |
| `spl_tol.dwg` | the top edge with a fit tolerance of 0.1 (not graded) |
| `spl_pline.dwg` | the top edge as a spline-fit polyline of the same points (PEDIT Spline) |
| `spl_shifted.dwg` | the closed spline moved by (0.1, 0) |
| `spl_fullacad.dwg` | **made by hand in full AutoCAD 2026** (not rebuilt by the script): splines through (0,0) (1,1) (2,0.5) (3,1.5) (4,0) with chord, square-root and uniform knots, and one by control vertices, each 3 higher |

Lineweight set (D100). Four lines 4 long at y = 0…3.

| File | What it is |
|---|---|
| `lw_ref.dwg` | on layer THICK (0.50 mm by layer), by layer on OBJECT (Default), 0.35 mm set on the line, and a ByBlock line in a block inserted at 0.70 mm |
| `lw_wrong.dwg` | the first two lines 0.25 mm set on the line (the first wrong, the second the same as Default) |
| `lw_bylayer.dwg` | the same lineweights, every one by layer (layers L35, L70) |

Readability set (D101). `rd_ref.dwg`: a 6 × 3 plate with a long hidden line, a
hidden line 0.25 long (its dash pattern is 0.375), a center line at linetype scale
0.05, squares hatched ANSI31 at scale 0.1 (dense), 2 (a 0.3 square: sparse) and 1,
and two dimensions, text 0.18 and 0.04, both with arrows 0.18. Layout1 has one
viewport showing it all at 1:2. `rd_big.dwg` (D102): sizes too large, on the same
plate and layout: a hidden line at linetype scale 3 (gaps 0.375), a 2 × 2 square
hatched ANSI31 at scale 4 (lines 0.5 apart), and a dimension with text and arrows 0.7.

Regenerate everything (a few minutes; it needs a licensed AutoCAD):

```bash
powershell -File tests/corpus/gen/make_corpus.ps1 -DwgRead C:/path/to/dwgread.exe
```
