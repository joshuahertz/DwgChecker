;;; corpus.lsp -- builds the DWG Checker test corpus inside AutoCAD (accoreconsole).
;;; Loaded by the per-file .scr scripts that make_corpus.ps1 generates.
;;; All geometry is created with entmake so coordinates are exact and repeatable.

(defun dc-init ()
  (setvar "CMDECHO" 1)
  (setvar "FILEDIA" 0)
  (setvar "OSMODE" 0)
  (setvar "MIRRTEXT" 0)
  (setvar "EXPERT" 5)
  (setvar "INSUNITS" 1)
  (dc-load-ltypes)
  (dc-layer "OBJECT" 7 "Continuous")
  (dc-layer "HIDDEN" 1 "HIDDEN")
  (dc-layer "CENTER" 3 "CENTER")
  (dc-layer "PHANTOM" 6 "PHANTOM")
  (dc-layer "DIM" 2 "Continuous")
  (dc-layer "CONSTRUCTION" 8 "Continuous")
  (dc-layer "HATCH" 4 "Continuous")
  (setvar "CLAYER" "OBJECT")
  (princ))

(defun dc-load-ltypes ()
  (foreach lt '("HIDDEN" "CENTER" "PHANTOM" "DASHED")
    (if (not (tblsearch "LTYPE" lt))
      (command "_.-LINETYPE" "_Load" lt "acad.lin" "")))
  (princ))

(defun dc-layer (name color ltype)
  (if (not (tblsearch "LAYER" name))
    (entmake (list '(0 . "LAYER") '(100 . "AcDbSymbolTableRecord") '(100 . "AcDbLayerTableRecord")
                   (cons 2 name) '(70 . 0) (cons 62 color) (cons 6 ltype)))))

(defun dc-p (x y) (list (float x) (float y) 0.0))
(defun dc-deg (d) (* pi (/ (float d) 180.0)))

(defun dc-line (lay x1 y1 x2 y2)
  (entmake (list '(0 . "LINE") (cons 8 lay) (cons 10 (dc-p x1 y1)) (cons 11 (dc-p x2 y2))))
  (entlast))
(defun dc-line-lt (lay lt x1 y1 x2 y2)
  (entmake (list '(0 . "LINE") (cons 8 lay) (cons 6 lt) (cons 10 (dc-p x1 y1)) (cons 11 (dc-p x2 y2))))
  (entlast))
(defun dc-circle (lay x y r)
  (entmake (list '(0 . "CIRCLE") (cons 8 lay) (cons 10 (dc-p x y)) (cons 40 (float r))))
  (entlast))
(defun dc-arc (lay x y r a0 a1)          ; angles in degrees, CCW from a0 to a1
  (entmake (list '(0 . "ARC") (cons 8 lay) (cons 10 (dc-p x y)) (cons 40 (float r))
                 (cons 50 (dc-deg a0)) (cons 51 (dc-deg a1))))
  (entlast))
(defun dc-text (lay x y h s)
  (entmake (list '(0 . "TEXT") (cons 8 lay) (cons 10 (dc-p x y)) (cons 40 (float h)) (cons 1 s)))
  (entlast))
(defun dc-mtext (lay x y h s)
  (entmake (list '(0 . "MTEXT") '(100 . "AcDbEntity") (cons 8 lay) '(100 . "AcDbMText")
                 (cons 10 (dc-p x y)) (cons 40 (float h)) (cons 1 s)))
  (entlast))
(defun dc-bulge (sweepdeg) (/ (sin (/ (dc-deg sweepdeg) 4.0)) (cos (/ (dc-deg sweepdeg) 4.0))))

(defun dc-all () (ssget "_X" '((410 . "Model"))))
(defun dc-save (path) (command "_.SAVEAS" "2018" path) (princ))
(defun dc-save-fmt (fmt path) (command "_.SAVEAS" fmt path) (princ))

;;; ------------------------------------------------------------------
;;; Reference plate (ref.dwg): 4 x 2 plate, top-right corner rounded R0.5,
;;; holes D0.75 at (1,1) and D0.5 at (2.75,1), one hidden line, centerlines
;;; on the large hole, a text label and a linear + diameter dimension.
;;; ------------------------------------------------------------------
(defun dc-ref-outline ()
  (setq *L1* (dc-line "OBJECT" 0 0 4 0))
  (setq *L2* (dc-line "OBJECT" 4 0 4 1.5))
  (setq *A1* (dc-arc  "OBJECT" 3.5 1.5 0.5 0 90))
  (setq *L3* (dc-line "OBJECT" 3.5 2 0 2))
  (setq *L4* (dc-line "OBJECT" 0 2 0 0)))

(defun dc-ref-features ()
  (setq *C1* (dc-circle "OBJECT" 1 1 0.375))
  (setq *C2* (dc-circle "OBJECT" 2.75 1 0.25))
  (setq *H1* (dc-line "HIDDEN" 2 0 2 2))
  (setq *CL1* (dc-line "CENTER" 0.5 1 1.5 1))
  (setq *CL2* (dc-line "CENTER" 1 0.5 1 1.5)))

(defun dc-ref-annot ()
  (dc-text "DIM" 0.2 2.3 0.2 "PLATE")
  (setvar "CLAYER" "DIM")
  (command "_.DIMLINEAR" (dc-p 0 0) (dc-p 4 0) (dc-p 2 -0.6))
  (command "_.DIMDIAMETER" (list *C1* (dc-p 1.375 1.0)) (dc-p 1.9 2.6))
  (setvar "CLAYER" "OBJECT")
  (princ))

(defun dc-build-ref () (dc-ref-outline) (dc-ref-features) (dc-ref-annot) (princ))

;;; Outline as one closed LWPOLYLINE; the bulge makes the R0.5 corner.
(defun dc-ref-outline-pline ()
  (entmake (list '(0 . "LWPOLYLINE") '(100 . "AcDbEntity") '(8 . "OBJECT") '(100 . "AcDbPolyline")
                 '(90 . 5) '(70 . 1)
                 '(10 0.0 0.0) '(10 4.0 0.0) '(10 4.0 1.5) (cons 42 (dc-bulge 90.0))
                 '(10 3.5 2.0) '(10 0.0 2.0)))
  (entlast))

;;; Independent redraw: different origin, different drawing order and directions.
(defun dc-build-redraw (ox oy)
  (dc-circle "OBJECT" (+ ox 2.75) (+ oy 1) 0.25)
  (dc-line "OBJECT" (+ ox 0) (+ oy 0) (+ ox 0) (+ oy 2))
  (dc-line "OBJECT" (+ ox 0) (+ oy 2) (+ ox 3.5) (+ oy 2))
  (dc-arc  "OBJECT" (+ ox 3.5) (+ oy 1.5) 0.5 0 90)
  (dc-line "OBJECT" (+ ox 4) (+ oy 1.5) (+ ox 4) (+ oy 0))
  (dc-line "OBJECT" (+ ox 4) (+ oy 0) (+ ox 0) (+ oy 0))
  (dc-circle "OBJECT" (+ ox 1) (+ oy 1) 0.375)
  (dc-line "HIDDEN" (+ ox 2) (+ oy 2) (+ ox 2) (+ oy 0))
  (dc-line "CENTER" (+ ox 1) (+ oy 0.5) (+ ox 1) (+ oy 1.5))
  (dc-line "CENTER" (+ ox 1.5) (+ oy 1) (+ ox 0.5) (+ oy 1))
  (dc-text "DIM" (+ ox 0.2) (+ oy 2.3) 0.2 "PLATE")
  (princ))

;;; Reference built from blocks: hole block (circle + centerlines on layer 0,
;;; ltype BYBLOCK), small-hole block inserted scaled, outline block inserted
;;; rotated, and a nested block holding the hidden line.
(defun dc-build-blocks ()
  ;; HOLE: unit circle r=0.375 with BYBLOCK centerlines
  (entmake '((0 . "BLOCK") (2 . "HOLE") (70 . 0) (10 0.0 0.0 0.0)))
  (entmake '((0 . "CIRCLE") (8 . "0") (6 . "Continuous") (10 0.0 0.0 0.0) (40 . 0.375)))
  (entmake '((0 . "ENDBLK")))
  (entmake '((0 . "BLOCK") (2 . "CMARK") (70 . 0) (10 0.0 0.0 0.0)))
  (entmake '((0 . "LINE") (8 . "0") (6 . "BYBLOCK") (10 -0.5 0.0 0.0) (11 0.5 0.0 0.0)))
  (entmake '((0 . "LINE") (8 . "0") (6 . "BYBLOCK") (10 0.0 -0.5 0.0) (11 0.0 0.5 0.0)))
  (entmake '((0 . "ENDBLK")))
  ;; SMALL: circle r=1, inserted at scale 0.25
  (entmake '((0 . "BLOCK") (2 . "SMALL") (70 . 0) (10 0.0 0.0 0.0)))
  (entmake '((0 . "CIRCLE") (8 . "OBJECT") (10 0.0 0.0 0.0) (40 . 1.0)))
  (entmake '((0 . "ENDBLK")))
  ;; HIDLINE: vertical line of length 2 on layer 0, ltype BYLAYER (inherits insert's layer)
  (entmake '((0 . "BLOCK") (2 . "HIDLINE") (70 . 0) (10 0.0 0.0 0.0)))
  (entmake '((0 . "LINE") (8 . "0") (10 0.0 0.0 0.0) (11 0.0 2.0 0.0)))
  (entmake '((0 . "ENDBLK")))
  ;; OUTLINE: plate outline drawn rotated by -90 deg about the origin; inserted with rotation 90
  (entmake '((0 . "BLOCK") (2 . "OUTLINE") (70 . 0) (10 0.0 0.0 0.0)))
  (entmake '((0 . "LINE") (8 . "OBJECT") (10 0.0 0.0 0.0) (11 0.0 -4.0 0.0)))
  (entmake '((0 . "LINE") (8 . "OBJECT") (10 0.0 -4.0 0.0) (11 1.5 -4.0 0.0)))
  (entmake (list '(0 . "ARC") '(8 . "OBJECT") '(10 1.5 -3.5 0.0) '(40 . 0.5) (cons 50 (dc-deg 270)) (cons 51 (dc-deg 0))))
  (entmake '((0 . "LINE") (8 . "OBJECT") (10 2.0 -3.5 0.0) (11 2.0 0.0 0.0)))
  (entmake '((0 . "LINE") (8 . "OBJECT") (10 2.0 0.0 0.0) (11 0.0 0.0 0.0)))
  (entmake '((0 . "ENDBLK")))
  ;; FEATURES: nested block containing HOLE+CMARK, SMALL and HIDLINE inserts
  (entmake '((0 . "BLOCK") (2 . "FEATURES") (70 . 0) (10 0.0 0.0 0.0)))
  (entmake '((0 . "INSERT") (2 . "HOLE") (8 . "OBJECT") (10 1.0 1.0 0.0)))
  (entmake '((0 . "INSERT") (2 . "CMARK") (8 . "CENTER") (6 . "CENTER") (10 1.0 1.0 0.0)))
  (entmake '((0 . "INSERT") (2 . "SMALL") (8 . "OBJECT") (10 2.75 1.0 0.0) (41 . 0.25) (42 . 0.25) (43 . 0.25)))
  (entmake '((0 . "INSERT") (2 . "HIDLINE") (8 . "HIDDEN") (10 2.0 0.0 0.0)))
  (entmake '((0 . "ENDBLK")))
  ;; model space
  (entmake (list '(0 . "INSERT") '(2 . "OUTLINE") '(8 . "OBJECT") '(10 0.0 0.0 0.0) (cons 50 (dc-deg 90))))
  (entmake '((0 . "INSERT") (2 . "FEATURES") (8 . "0") (10 0.0 0.0 0.0)))
  (dc-text "DIM" 0.2 2.3 0.2 "PLATE")
  (princ))

;;; Simple rectangles for the polyline-vs-lines test.
(defun dc-rect-lines ()
  (dc-line "OBJECT" 1 1 4 1) (dc-line "OBJECT" 4 1 4 3)
  (dc-line "OBJECT" 4 3 1 3) (dc-line "OBJECT" 1 3 1 1) (princ))
(defun dc-rect-pline ()
  (entmake (list '(0 . "LWPOLYLINE") '(100 . "AcDbEntity") '(8 . "OBJECT") '(100 . "AcDbPolyline")
                 '(90 . 4) '(70 . 1) '(10 1.0 1.0) '(10 4.0 1.0) '(10 4.0 3.0) '(10 1.0 3.0)))
  (princ))

;;; ------------------------------------------------------------------
;;; Orthographic reference: stepped block 3 (W) x 2 (D) x 1.5 (H), step
;;; removes x 2..3 above z 0.75, vertical through-hole D0.75 at x=1, y=1.
;;; Third-angle: front at origin, top above, right to the right.
;;; ------------------------------------------------------------------
(defun dc-ortho-front (dx dy hidvis)
  (dc-line "OBJECT" (+ dx 0) (+ dy 0) (+ dx 3) (+ dy 0))
  (dc-line "OBJECT" (+ dx 3) (+ dy 0) (+ dx 3) (+ dy 0.75))
  (dc-line "OBJECT" (+ dx 3) (+ dy 0.75) (+ dx 2) (+ dy 0.75))
  (dc-line "OBJECT" (+ dx 2) (+ dy 0.75) (+ dx 2) (+ dy 1.5))
  (dc-line "OBJECT" (+ dx 2) (+ dy 1.5) (+ dx 0) (+ dy 1.5))
  (dc-line "OBJECT" (+ dx 0) (+ dy 1.5) (+ dx 0) (+ dy 0))
  (dc-line (if hidvis "OBJECT" "HIDDEN") (+ dx 0.625) (+ dy 0) (+ dx 0.625) (+ dy 1.5))
  (dc-line "HIDDEN" (+ dx 1.375) (+ dy 0) (+ dx 1.375) (+ dy 1.5))
  (dc-line "CENTER" (+ dx 1) (- dy 0.25) (+ dx 1) (+ dy 1.75))
  (princ))
(defun dc-ortho-top (dx dy)
  (dc-line "OBJECT" (+ dx 0) (+ dy 0) (+ dx 3) (+ dy 0))
  (dc-line "OBJECT" (+ dx 3) (+ dy 0) (+ dx 3) (+ dy 2))
  (dc-line "OBJECT" (+ dx 3) (+ dy 2) (+ dx 0) (+ dy 2))
  (dc-line "OBJECT" (+ dx 0) (+ dy 2) (+ dx 0) (+ dy 0))
  (dc-line "OBJECT" (+ dx 2) (+ dy 0) (+ dx 2) (+ dy 2))
  (dc-circle "OBJECT" (+ dx 1) (+ dy 1) 0.375)
  (dc-line "CENTER" (+ dx 0.5) (+ dy 1) (+ dx 1.5) (+ dy 1))
  (dc-line "CENTER" (+ dx 1) (+ dy 0.5) (+ dx 1) (+ dy 1.5))
  (princ))
(defun dc-ortho-right (dx dy)
  (dc-line "OBJECT" (+ dx 0) (+ dy 0) (+ dx 2) (+ dy 0))
  (dc-line "OBJECT" (+ dx 2) (+ dy 0) (+ dx 2) (+ dy 1.5))
  (dc-line "OBJECT" (+ dx 2) (+ dy 1.5) (+ dx 0) (+ dy 1.5))
  (dc-line "OBJECT" (+ dx 0) (+ dy 1.5) (+ dx 0) (+ dy 0))
  (dc-line "OBJECT" (+ dx 0) (+ dy 0.75) (+ dx 2) (+ dy 0.75))
  (dc-line "HIDDEN" (+ dx 0.625) (+ dy 0) (+ dx 0.625) (+ dy 1.5))
  (dc-line "HIDDEN" (+ dx 1.375) (+ dy 0) (+ dx 1.375) (+ dy 1.5))
  (dc-line "CENTER" (+ dx 1) (- dy 0.25) (+ dx 1) (+ dy 1.75))
  (princ))
(defun dc-build-ortho (tx ty rx ry hidvis)
  (dc-ortho-front 0 0 hidvis)
  (dc-ortho-top tx ty)
  (dc-ortho-right rx ry)
  (princ))
(defun dc-ortho-projlines ()
  ;; projection lines left on the object layer, spanning the gaps between views
  (dc-line "OBJECT" 0 1.5 0 2.5)
  (dc-line "OBJECT" 2 1.5 2 2.5)
  (dc-line "OBJECT" 3 0.75 3 2.5)
  (dc-line "OBJECT" 3 0.75 4 0.75)
  (dc-line "OBJECT" 3 1.5 4 1.5)
  (dc-line "OBJECT" 3.5 2.0 6.5 5.0)          ; 45 degree miter line
  (princ))

;;; Block whose front and top views are nearly identical (3 x 2 x 2, hole D0.5).
(defun dc-similar (fx fy tx ty rx ry)
  ;; front: 3 wide x 2 tall, hidden hole
  (dc-line "OBJECT" fx fy (+ fx 3) fy) (dc-line "OBJECT" (+ fx 3) fy (+ fx 3) (+ fy 2))
  (dc-line "OBJECT" (+ fx 3) (+ fy 2) fx (+ fy 2)) (dc-line "OBJECT" fx (+ fy 2) fx fy)
  (dc-line "HIDDEN" (+ fx 1.25) fy (+ fx 1.25) (+ fy 2)) (dc-line "HIDDEN" (+ fx 1.75) fy (+ fx 1.75) (+ fy 2))
  (dc-line "CENTER" (+ fx 1.5) (- fy 0.25) (+ fx 1.5) (+ fy 2.25))
  ;; top: 3 wide x 2 deep, visible hole
  (dc-line "OBJECT" tx ty (+ tx 3) ty) (dc-line "OBJECT" (+ tx 3) ty (+ tx 3) (+ ty 2))
  (dc-line "OBJECT" (+ tx 3) (+ ty 2) tx (+ ty 2)) (dc-line "OBJECT" tx (+ ty 2) tx ty)
  (dc-circle "OBJECT" (+ tx 1.5) (+ ty 1) 0.25)
  (dc-line "CENTER" (+ tx 1.0) (+ ty 1) (+ tx 2.0) (+ ty 1))
  (dc-line "CENTER" (+ tx 1.5) (+ ty 0.5) (+ tx 1.5) (+ ty 1.5))
  ;; right: 2 deep x 2 tall, hidden hole
  (dc-line "OBJECT" rx ry (+ rx 2) ry) (dc-line "OBJECT" (+ rx 2) ry (+ rx 2) (+ ry 2))
  (dc-line "OBJECT" (+ rx 2) (+ ry 2) rx (+ ry 2)) (dc-line "OBJECT" rx (+ ry 2) rx ry)
  (dc-line "HIDDEN" (+ rx 0.75) ry (+ rx 0.75) (+ ry 2)) (dc-line "HIDDEN" (+ rx 1.25) ry (+ rx 1.25) (+ ry 2))
  (dc-line "CENTER" (+ rx 1.0) (- ry 0.25) (+ rx 1.0) (+ ry 2.25))
  (princ))

;;; ------------------------------------------------------------------
;;; Field-map "kitchen sink": one of every entity shape the loader must
;;; understand, used only to build FIELDMAP.md and loader tests.
;;; ------------------------------------------------------------------
(defun dc-build-kitchen ()
  (dc-line "OBJECT" 0 0 1 0)                                   ; plain bylayer line
  (dc-line-lt "OBJECT" "HIDDEN" 0 0.5 1 0.5)                   ; explicit linetype
  (dc-line-lt "OBJECT" "BYBLOCK" 0 0.75 1 0.75)                ; byblock in model space
  (dc-arc "OBJECT" 2 0 0.5 30 120)                             ; plain arc
  (entmake (list '(0 . "ARC") '(8 . "OBJECT") '(10 -3.0 0.0 0.0) '(40 . 0.5)
                 (cons 50 (dc-deg 30)) (cons 51 (dc-deg 120)) '(210 0.0 0.0 -1.0)))   ; mirrored-normal arc
  (entmake '((0 . "CIRCLE") (8 . "OBJECT") (10 -4.0 1.0 0.0) (40 . 0.25) (210 0.0 0.0 -1.0)))
  (dc-circle "OBJECT" 5 0 0.5)
  ;; lwpolyline with bulge and global width, open
  (entmake (list '(0 . "LWPOLYLINE") '(100 . "AcDbEntity") '(8 . "OBJECT") '(100 . "AcDbPolyline")
                 '(90 . 3) '(70 . 0) '(43 . 0.05) '(10 6.0 0.0) (cons 42 (dc-bulge 180.0)) '(10 7.0 0.0) '(10 7.0 1.0)))
  ;; closed lwpolyline with normal -Z
  (entmake (list '(0 . "LWPOLYLINE") '(100 . "AcDbEntity") '(8 . "OBJECT") '(100 . "AcDbPolyline")
                 '(90 . 3) '(70 . 1) '(10 -8.0 0.0) '(10 -9.0 0.0) (cons 42 (dc-bulge 90.0)) '(10 -9.0 1.0) '(210 0.0 0.0 -1.0)))
  ;; old-style 2D polyline with bulge, closed
  (entmake '((0 . "POLYLINE") (8 . "OBJECT") (66 . 1) (10 0.0 0.0 0.0) (70 . 1)))
  (entmake '((0 . "VERTEX") (8 . "OBJECT") (10 10.0 0.0 0.0)))
  (entmake (list '(0 . "VERTEX") '(8 . "OBJECT") '(10 11.0 0.0 0.0) (cons 42 (dc-bulge 90.0))))
  (entmake '((0 . "VERTEX") (8 . "OBJECT") (10 12.0 1.0 0.0)))
  (entmake '((0 . "SEQEND") (8 . "OBJECT")))
  ;; 3D line (nonzero Z)
  (entmake '((0 . "LINE") (8 . "OBJECT") (10 0.0 3.0 0.0) (11 1.0 3.0 0.5)))
  ;; Defpoints and paper space lines
  (dc-layer "Defpoints" 7 "Continuous")
  (dc-line "Defpoints" 0 4 1 4)
  (entmake '((0 . "LINE") (67 . 1) (8 . "OBJECT") (10 0.0 0.0 0.0) (11 2.0 2.0 0.0)))
  ;; blocks: byblock linetype + nested + mirrored + non-uniform
  (entmake '((0 . "BLOCK") (2 . "KHOLE") (70 . 0) (10 0.0 0.0 0.0)))
  (entmake '((0 . "CIRCLE") (8 . "0") (10 0.0 0.0 0.0) (40 . 0.25)))
  (entmake '((0 . "LINE") (8 . "0") (6 . "BYBLOCK") (10 -0.4 0.0 0.0) (11 0.4 0.0 0.0)))
  (entmake (list '(0 . "ARC") '(8 . "0") '(10 0.0 0.0 0.0) '(40 . 0.35) (cons 50 0.0) (cons 51 (dc-deg 90))))
  (entmake '((0 . "ENDBLK")))
  (entmake '((0 . "BLOCK") (2 . "KPAIR") (70 . 0) (10 0.0 0.0 0.0)))
  (entmake '((0 . "INSERT") (2 . "KHOLE") (8 . "0") (10 0.0 0.0 0.0)))
  (entmake '((0 . "INSERT") (2 . "KHOLE") (8 . "0") (10 1.0 0.0 0.0)))
  (entmake '((0 . "ENDBLK")))
  (entmake (list '(0 . "INSERT") '(2 . "KHOLE") '(8 . "HIDDEN") '(10 0.0 6.0 0.0) '(41 . 2.0) '(42 . 2.0) '(43 . 2.0) (cons 50 (dc-deg 30))))
  (entmake '((0 . "INSERT") (2 . "KHOLE") (8 . "OBJECT") (10 3.0 6.0 0.0) (41 . -1.0) (42 . 1.0) (43 . 1.0)))
  (entmake '((0 . "INSERT") (2 . "KHOLE") (8 . "OBJECT") (10 6.0 6.0 0.0) (41 . 2.0) (42 . 1.0) (43 . 1.0)))
  (entmake (list '(0 . "INSERT") '(2 . "KPAIR") '(8 . "CENTER") '(10 9.0 6.0 0.0) (cons 50 (dc-deg 45))))
  ;; annotation
  (dc-text "DIM" 0 8 0.2 "KITCHEN")
  (dc-mtext "DIM" 0 9 0.2 "MULTI\\PLINE")
  (setvar "CLAYER" "DIM")
  (command "_.DIMLINEAR" (dc-p 0 0) (dc-p 1 0) (dc-p 0.5 -0.5))
  (command "_.DIMALIGNED" (dc-p 10 0) (dc-p 11 0) (dc-p 10.5 -0.5))
  (setq e (dc-circle "OBJECT" 5 3 0.5))
  (command "_.DIMDIAMETER" (list e (dc-p 5.5 3)) (dc-p 6.5 4))
  (setq e (dc-circle "OBJECT" 8 3 0.5))
  (command "_.DIMRADIUS" (list e (dc-p 8.5 3)) (dc-p 9.5 4))
  (setvar "CLAYER" "OBJECT")
  ;; hatch, spline, ellipse, point
  (setq e (dc-circle "OBJECT" 12 3 0.5))
  (command "_.-HATCH" "_Properties" "ANSI31" "1" "0" "_Select" e "" "")
  (command "_.SPLINE" (dc-p 0 12) (dc-p 1 13) (dc-p 2 12) (dc-p 3 13) "" "" "")
  (command "_.ELLIPSE" (dc-p 5 12) (dc-p 7 12) 0.5)
  (entmake '((0 . "POINT") (8 . "OBJECT") (10 9.0 12.0 0.0)))
  ;; layer states: frozen, off (negative color), locked
  (entmake '((0 . "LAYER") (100 . "AcDbSymbolTableRecord") (100 . "AcDbLayerTableRecord") (2 . "FROZEN") (70 . 1) (62 . 7) (6 . "Continuous")))
  (entmake '((0 . "LAYER") (100 . "AcDbSymbolTableRecord") (100 . "AcDbLayerTableRecord") (2 . "OFFLAYER") (70 . 0) (62 . -7) (6 . "Continuous")))
  (entmake '((0 . "LAYER") (100 . "AcDbSymbolTableRecord") (100 . "AcDbLayerTableRecord") (2 . "LOCKED") (70 . 4) (62 . 7) (6 . "Continuous")))
  (dc-line "FROZEN" 0 14 1 14)
  (dc-line "OFFLAYER" 0 14.5 1 14.5)
  (dc-line "LOCKED" 0 15 1 15)
  ;; MINSERT (3 columns x 2 rows) and an XLINE
  (entmake '((0 . "INSERT") (100 . "AcDbEntity") (8 . "OBJECT") (100 . "AcDbMInsertBlock") (2 . "KHOLE")
             (10 0.0 17.0 0.0) (70 . 3) (71 . 2) (44 . 1.0) (45 . 1.0)))
  (entmake '((0 . "XLINE") (100 . "AcDbEntity") (8 . "CONSTRUCTION") (100 . "AcDbXline") (10 0.0 20.0 0.0) (11 1.0 0.0 0.0)))
  (princ))

;;; ------------------------------------------------------------------
;;; Hatch corpus: region A = 4 x 2 plate minus a D0.75 hole at (1,1),
;;; hatched ANSI31; region B = 1 x 1 square at (5,0), hatched ANSI37.
;;; ------------------------------------------------------------------
(defun dc-hatch-geom ()
  (dc-line "OBJECT" 0 0 4 0) (dc-line "OBJECT" 4 0 4 2)
  (dc-line "OBJECT" 4 2 0 2) (dc-line "OBJECT" 0 2 0 0)
  (dc-circle "OBJECT" 1 1 0.375)
  (dc-line "OBJECT" 5 0 6 0) (dc-line "OBJECT" 6 0 6 1)
  (dc-line "OBJECT" 6 1 5 1) (dc-line "OBJECT" 5 1 5 0)
  (princ))
;; hatch by picking an internal point (associative, edge-type boundary paths)
(defun dc-hpick (pat scale ang x y)
  (setvar "CLAYER" "HATCH")
  (command "_.-HATCH" "_Properties" pat (rtos scale 2 6) (rtos ang 2 6) (dc-p x y) "")
  (setvar "CLAYER" "OBJECT")
  (princ))
(defun dc-hsolid (x y)
  (setvar "CLAYER" "HATCH")
  (command "_.-HATCH" "_Properties" "_Solid" (dc-p x y) "")
  (setvar "CLAYER" "OBJECT")
  (princ))
;; hatch by drawing a polygon boundary (non-associative, polyline boundary path)
(defun dc-hdraw (pat pts)
  (setvar "CLAYER" "HATCH")
  (command "_.-HATCH" "_Properties" pat "1" "0" "_W" "_N")
  (foreach p pts (command p))
  (command "" "" "")
  (setvar "CLAYER" "OBJECT")
  (princ))
(defun dc-hatch-ref () (dc-hatch-geom) (dc-hpick "ANSI31" 1 0 3 1) (dc-hpick "ANSI37" 1 0 5.5 0.5) (princ))
(defun dc-hatch-split ()
  ;; region A hatched as two separate HATCH entities, split along x = 2
  (dc-hatch-geom)
  (setq tmp (dc-line "CONSTRUCTION" 2 0 2 2))
  (dc-hpick "ANSI31" 1 0 0.25 0.25)
  (dc-hpick "ANSI31" 1 0 3 1)
  (entdel tmp)
  (dc-hpick "ANSI37" 1 0 5.5 0.5)
  (princ))
(defun dc-hatch-edges-select ()
  ;; plate outline with the R0.5 corner, hatched by selecting the boundary objects
  (dc-ref-outline)
  (setq ss (ssadd))
  (foreach e (list *L1* *L2* *A1* *L3* *L4*) (ssadd e ss))
  (setvar "CLAYER" "HATCH")
  (command "_.-HATCH" "_Properties" "ANSI31" "1" "0" "_Select" ss "" "")
  (setvar "CLAYER" "OBJECT")
  (princ))
(defun dc-hatch-edges-pick () (dc-ref-outline) (dc-hpick "ANSI31" 1 0 1 1) (princ))
(defun dc-hatch-block ()
  (dc-hatch-ref)
  (command "_.-BLOCK" "HB" (dc-p 0 0) (dc-all) "")
  (command "_.-INSERT" "HB" (dc-p 10 5) 1 "" 30)
  (princ))
(defun dc-hatch-edges-cw ()
  ;; rectangle with a semicircular notch in the top edge: walking the boundary CCW
  ;; runs the notch arc clockwise, so the hatch stores a clockwise edge arc
  (setq ss (ssadd))
  (foreach e (list (dc-line "OBJECT" 0 0 4 0) (dc-line "OBJECT" 4 0 4 2) (dc-line "OBJECT" 4 2 2.5 2)
                   (dc-arc "OBJECT" 2 2 0.5 180 360) (dc-line "OBJECT" 1.5 2 0 2) (dc-line "OBJECT" 0 2 0 0))
    (ssadd e ss))
  (setvar "CLAYER" "HATCH")
  (command "_.-HATCH" "_Properties" "ANSI31" "1" "0" "_Select" ss "" "")
  (setvar "CLAYER" "OBJECT")
  (princ))

;;; ------------------------------------------------------------------
;;; Dimension precision (dim_*.dwg): the reference plate outline and holes
;;; with six dimensions, drawn with a named style made by dc-dimstyle:
;;;   *D1* 4.000 overall width        *D2* 1.500 right edge
;;;   *D3* 0.625 left edge to hole    *D4* diameter 0.750
;;;   *D5* radius 0.500 corner        *D6* 90 degree corner arc
;;;   *D7* arc length 0.785 of the corner arc
;;; t1 / t2: typed-over text for *D1* / *D2* (nil: none)
;;; ------------------------------------------------------------------
(defun dc-dimstyle (name dec adec zin lunit)
  (setvar "DIMDEC" dec)
  (setvar "DIMADEC" adec)
  (setvar "DIMZIN" zin)
  (setvar "DIMAZIN" 0)
  (setvar "DIMLUNIT" lunit)
  (command "_.-DIMSTYLE" "_Save" name)
  (princ))
(defun dc-dims (t1 t2)
  (setvar "CLAYER" "DIM")
  (if t1 (command "_.DIMLINEAR" (dc-p 0 0) (dc-p 4 0) "_Text" t1 (dc-p 2 -0.6))
         (command "_.DIMLINEAR" (dc-p 0 0) (dc-p 4 0) (dc-p 2 -0.6)))
  (setq *D1* (entlast))
  (if t2 (command "_.DIMLINEAR" (dc-p 4 0) (dc-p 4 1.5) "_Text" t2 (dc-p 4.6 0.75))
         (command "_.DIMLINEAR" (dc-p 4 0) (dc-p 4 1.5) (dc-p 4.6 0.75)))
  (setq *D2* (entlast))
  (command "_.DIMLINEAR" (dc-p 0 1) (dc-p 0.625 1) (dc-p 0.3125 2.5))
  (setq *D3* (entlast))
  (command "_.DIMDIAMETER" (list *C1* (dc-p 1.375 1.0)) (dc-p 1.9 2.6))
  (setq *D4* (entlast))
  (command "_.DIMRADIUS" (list *A1* (dc-p 3.853553390593 1.853553390593)) (dc-p 4.4 2.4))
  (setq *D5* (entlast))
  (command "_.DIMANGULAR" (list *A1* (dc-p 3.853553390593 1.853553390593)) (dc-p 4.3 2.3))
  (setq *D6* (entlast))
  (command "_.DIMARC" (list *A1* (dc-p 3.853553390593 1.853553390593)) (dc-p 4.5 2.5))
  (setq *D7* (entlast))
  (setvar "CLAYER" "OBJECT")
  (princ))
(defun dc-dim-ref (name dec adec zin lunit)
  (dc-ref-outline)
  (dc-ref-features)
  (dc-dimstyle name dec adec zin lunit)
  (dc-dims nil nil)
  (princ))
(defun dc-dim-family ()
  ;; DC3 with a radius sub-style (DC3$4) at one decimal place
  (dc-ref-outline)
  (dc-ref-features)
  (dc-dimstyle "DC3$4" 1 0 0 2)
  (dc-dimstyle "DC3" 3 0 0 2)
  (dc-dims nil nil)
  (princ))
