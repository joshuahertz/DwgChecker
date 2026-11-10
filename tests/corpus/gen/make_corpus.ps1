<#
.SYNOPSIS
  Regenerates the DWG Checker test corpus (tests/corpus/dwg/*.dwg) with AutoCAD's
  headless core console, then (optionally) dumps every file to JSON fixtures.

.EXAMPLE
  ./make_corpus.ps1 -AccoreConsole "C:\Program Files\Autodesk\AutoCAD 2026\accoreconsole.exe" `
                    -DwgRead "C:\tools\libredwg\dwgread.exe"
#>
param(
    [string]$AccoreConsole = "C:\Program Files\Autodesk\AutoCAD 2026\accoreconsole.exe",
    [string]$DwgRead = "",
    [string[]]$Only = @()
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = (Resolve-Path (Join-Path $here '..\dwg')).Path
$fixDir = (Resolve-Path (Join-Path $here '..\..\fixtures')).Path
$work = Join-Path ([IO.Path]::GetTempPath()) 'dwgcheck-corpus'
New-Item -ItemType Directory -Force $work | Out-Null

function Fwd([string]$p) { return ($p -replace '\\', '/') }
function Out([string]$name) { return Fwd (Join-Path $outDir "$name.dwg") }

# AutoCAD's SECURELOAD refuses (load ...) from untrusted folders, and we do not
# change that setting. Instead the helper library is inlined into each script
# line by line (the LISP reader keeps reading while parentheses are open).
# Comments and blank lines are stripped: a blank script line would repeat the
# previous command, and very long single lines get truncated by the console.
function Get-InlineLisp([string]$path) {
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($raw in Get-Content $path) {
        $line = $raw
        $inStr = $false
        for ($i = 0; $i -lt $line.Length; $i++) {   # strip ; comments outside strings
            $ch = $line[$i]
            if ($ch -eq '"') { $inStr = -not $inStr }
            elseif ($ch -eq ';' -and -not $inStr) { $line = $line.Substring(0, $i); break }
        }
        $line = $line.Trim()
        if ($line.Length -gt 0) { $out.Add($line) }
    }
    return $out
}
$inlineLisp = Get-InlineLisp (Join-Path $here 'corpus.lsp')

# name => @{ body = <lisp>; input = <dwg to open or $null>; fmt = <SAVEAS format> }
$variants = [ordered]@{
  'ref'                  = @{ body = '(dc-build-ref)' }
  'ref_translated'       = @{ body = '(dc-build-ref) (command "_.MOVE" (dc-all) "" (dc-p 0 0) (dc-p 5.25 3.125))' }
  'ref_rotated37'        = @{ body = '(dc-build-ref) (command "_.ROTATE" (dc-all) "" (dc-p 0 0) 37)' }
  'ref_mirrored'         = @{ body = '(dc-build-ref) (command "_.MIRROR" (dc-all) "" (dc-p 0 0) (dc-p 0 1) "_Y")' }
  'ref_flipped3d'        = @{ body = '(dc-build-ref) (command "_.ROTATE3D" (dc-all) "" "_X" (dc-p 0 0) 180)' }
  'ref_split'            = @{ body = '(dc-build-ref) (entdel *L1*) (dc-line "OBJECT" 0 0 1.5 0) (dc-line "OBJECT" 1.5 0 4 0)' }
  'rect_lines'           = @{ body = '(dc-rect-lines)' }
  'rect_pline'           = @{ body = '(dc-rect-pline)' }
  'ref_polyline'         = @{ body = '(dc-ref-features) (dc-ref-outline-pline) (dc-ref-annot)' }
  'ref_missing'          = @{ body = '(dc-build-ref) (entdel *C2*)' }
  'ref_extra'            = @{ body = '(dc-build-ref) (dc-line "CONSTRUCTION" -0.5 -0.25 4.5 2.25)' }
  'ref_sizeoff'          = @{ body = '(dc-build-ref) (entmod (subst (cons 40 0.37) (assoc 40 (entget *C1*)) (entget *C1*)))' }
  'ref_mm'               = @{ body = '(dc-build-ref) (command "_.SCALE" (dc-all) "" (dc-p 0 0) 25.4) (setvar "INSUNITS" 4)' }
  'ref_blocks'           = @{ body = '(dc-build-blocks)' }
  'ref_saveas'           = @{ body = '(princ)'; input = 'ref' }
  'ref_copypaste'        = @{ body = ('(command "_.-INSERT" "*' + (Out 'ref') + '" (dc-p 0 0) 1 0)') }
  'ref_redraw'           = @{ body = '(dc-build-redraw 2.2 1.7)' }
  'ref_r2013'            = @{ body = '(princ)'; input = 'ref'; fmt = '2013' }
  'ref_r2000'            = @{ body = '(princ)'; input = 'ref'; fmt = '2000' }
  'ortho_ref'            = @{ body = '(dc-build-ortho 0 2.5 4 0 nil)' }
  'ortho_spacing'        = @{ body = '(dc-build-ortho 0 3.25 4.6 0 nil)' }
  'ortho_misaligned'     = @{ body = '(dc-build-ortho 0.25 2.5 4 0 nil)' }
  'ortho_firstangle'     = @{ body = '(dc-build-ortho 0 -3.0 -3.0 0 nil)' }
  'ortho_hidden_visible' = @{ body = '(dc-build-ortho 0 2.5 4 0 T)' }
  'ortho_projlines'      = @{ body = '(dc-build-ortho 0 2.5 4 0 nil) (dc-ortho-projlines)' }
  'ortho_similar'        = @{ body = '(dc-similar 0 0 0 3 4 0)' }
  'ortho_similar_spaced' = @{ body = '(dc-similar 0 0 0 3.6 4.8 0)' }
  'hatch_ref'            = @{ body = '(dc-hatch-ref)' }
  'hatch_missing_b'      = @{ body = '(dc-hatch-geom) (dc-hpick "ANSI31" 1 0 3 1)' }
  'hatch_pattern'        = @{ body = '(dc-hatch-geom) (dc-hpick "ANSI37" 1 0 3 1) (dc-hpick "ANSI37" 1 0 5.5 0.5)' }
  'hatch_scale_angle'    = @{ body = '(dc-hatch-geom) (dc-hpick "ANSI31" 2 30 3 1) (dc-hpick "ANSI37" 0.5 0 5.5 0.5)' }
  'hatch_partial'        = @{ body = '(dc-hatch-geom) (dc-hdraw "ANSI31" (list (dc-p 2 0) (dc-p 4 0) (dc-p 4 2) (dc-p 2 2))) (dc-hpick "ANSI37" 1 0 5.5 0.5)' }
  'hatch_extra'          = @{ body = '(dc-hatch-ref) (dc-hpick "ANSI31" 1 0 1 1)' }
  'hatch_spill'          = @{ body = '(dc-hatch-geom) (dc-hdraw "ANSI31" (list (dc-p 0 0) (dc-p 4 0) (dc-p 4 2) (dc-p 0 2))) (dc-hpick "ANSI37" 1 0 5.5 0.5)' }
  'hatch_split'          = @{ body = '(dc-hatch-split)' }
  'hatch_solid'          = @{ body = '(dc-hatch-geom) (dc-hsolid 3 1) (dc-hpick "ANSI37" 1 0 5.5 0.5)' }
  'hatch_rotated'        = @{ body = '(dc-hatch-ref) (command "_.ROTATE" (dc-all) "" (dc-p 0 0) 37)' }
  'hatch_block'          = @{ body = '(dc-hatch-block)' }
  'hatch_edges_select'   = @{ body = '(dc-hatch-edges-select)' }
  'hatch_edges_pick'     = @{ body = '(dc-hatch-edges-pick)' }
  'hatch_edges_cw'       = @{ body = '(dc-hatch-edges-cw)' }
  'dim_ref'              = @{ body = '(dc-dim-ref "DC3" 3 0 0 2)' }
  'dim_prec2'            = @{ body = '(dc-dim-ref "DC2" 2 0 0 2)' }
  'dim_angprec'          = @{ body = '(dc-dim-ref "DC3A1" 3 1 0 2)' }
  'dim_zin'              = @{ body = '(dc-dim-ref "DC3Z" 3 0 8 2)' }
  'dim_override'         = @{ body = '(dc-dim-ref "DC3" 3 0 0 2) (command "_.DIMOVERRIDE" "DIMDEC" 2 "" *D2* "")' }
  'dim_text'             = @{ body = '(dc-ref-outline) (dc-ref-features) (dc-dimstyle "DC3" 3 0 0 2) (dc-dims "4.00" "<> TYP")' }
  'dim_frac'             = @{ body = '(dc-dim-ref "DCF" 4 0 0 5)' }
  'dim_arch'             = @{ body = '(dc-dim-ref "DCA" 4 0 0 4)' }
  'dim_family'           = @{ body = '(dc-dim-family)' }
  'kitchen'              = @{ body = '(dc-build-kitchen)' }
  'kitchen_r2000'        = @{ body = '(princ)'; input = 'kitchen'; fmt = '2000' }
  'color_ref'            = @{ body = '(dc-color-ref 1 16711680)' }
  'color_wrong'          = @{ body = '(dc-color-ref 5 16711680)' }
  'color_near'           = @{ body = '(dc-color-ref 1 16385285)' }
  'color_bylayer'        = @{ body = '(dc-color-bylayer)' }
}

foreach ($name in $variants.Keys) {
    if ($Only.Count -gt 0 -and -not ($Only -contains $name)) { continue }
    $v = $variants[$name]
    $fmt = if ($v.fmt) { $v.fmt } else { '2018' }
    $target = Out $name
    if (Test-Path $target) { Remove-Item $target -Force }
    $scr = Join-Path $work "$name.scr"
    $lines = @($inlineLisp) + @(
        "(dc-init)",
        $v.body,
        "(dc-save-fmt `"$fmt`" `"$target`")",
        "_.QUIT",
        "_Y"
    )
    Set-Content -Path $scr -Value $lines -Encoding ascii
    $acArgs = @('/s', "`"$scr`"", '/l', 'en-US')
    if ($v.input) { $acArgs = @('/i', "`"$(Join-Path $outDir ($v.input + '.dwg'))`"") + $acArgs }
    $log = Join-Path $work "$name.log"
    $p = Start-Process -FilePath $AccoreConsole -ArgumentList $acArgs -NoNewWindow -PassThru -RedirectStandardOutput $log
    if (-not $p.WaitForExit(180000)) { $p.Kill(); Write-Warning "$name timed out" }
    if (Test-Path $target) { Write-Host ("{0,-22} ok  {1,8} bytes" -f $name, (Get-Item $target).Length) }
    else { Write-Warning "$name FAILED (see $log)" }
}

# Byte copy of the reference (corpus item 15).
if ($Only.Count -eq 0 -or ($Only -contains 'ref_bytecopy')) {
    Copy-Item (Join-Path $outDir 'ref.dwg') (Join-Path $outDir 'ref_bytecopy.dwg') -Force
    Write-Host 'ref_bytecopy           ok  (file copy)'
}

if ($DwgRead) {
    & (Join-Path $here 'dump_fixtures.ps1') -DwgRead $DwgRead
}
