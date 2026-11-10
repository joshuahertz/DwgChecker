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

Regenerate everything (a few minutes; it needs a licensed AutoCAD):

```bash
powershell -File tests/corpus/gen/make_corpus.ps1 -DwgRead C:/path/to/dwgread.exe
```
