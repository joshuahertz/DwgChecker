<#
.SYNOPSIS
  Draws the DWG Checker app icon (src/gui/resources/icons/app_<size>.png) with GDI+.
  A blue tile with a white CAD plate (rounded corner, hole, center marks) and a green
  check badge. Small sizes are drawn with heavier strokes and no center marks.
  Then run make_ico.py to pack the PNGs into src/gui/resources/app.ico.
#>
param([string]$OutDir = (Join-Path $PSScriptRoot '..\..\src\gui\resources\icons'))
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $OutDir | Out-Null

function RoundRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = 2 * $r
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

foreach ($size in 16, 24, 32, 48, 64, 128, 256) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $s = $size / 256.0
    $g.ScaleTransform($s, $s)
    $boost = if ($size -le 24) { 1.9 } elseif ($size -le 32) { 1.5 } elseif ($size -le 48) { 1.2 } else { 1.0 }

    # Tile.
    $tile = RoundRect 8 8 240 240 44
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush ([System.Drawing.PointF]::new(0, 8)), ([System.Drawing.PointF]::new(0, 248)), ([System.Drawing.Color]::FromArgb(255, 52, 120, 200)), ([System.Drawing.Color]::FromArgb(255, 24, 72, 140))
    $g.FillPath($grad, $tile)

    # Plate: 4 x 2.6 outline with a rounded top-right corner, drawn in white.
    $white = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), ([float](12 * $boost))
    $white.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $plate = New-Object System.Drawing.Drawing2D.GraphicsPath
    $plate.AddLine(40, 70, 160, 70)
    $plate.AddArc(140, 70, 52, 52, 270, 90)
    $plate.AddLine(192, 96, 192, 176)
    $plate.AddLine(192, 176, 40, 176)
    $plate.CloseFigure()
    $g.DrawPath($white, $plate)
    # Hole.
    $g.DrawEllipse($white, 72, 98, 52, 52)
    # Center marks (larger sizes only).
    if ($size -ge 48) {
        $cl = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(200, 190, 220, 255)), ([float]5)
        $cl.DashPattern = [single[]](4, 1.6, 1, 1.6)
        $g.DrawLine($cl, 58, 124, 138, 124)
        $g.DrawLine($cl, 98, 84, 98, 164)
    }

    # Check badge, bottom right, with a ring that separates it from the plate.
    $ringW = 10 * [Math]::Min($boost, 1.4)
    $g.FillEllipse((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 24, 72, 140))), 140 - $ringW, 140 - $ringW, 104 + 2 * $ringW, 104 + 2 * $ringW)
    $g.FillEllipse((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 46, 170, 80))), 140, 140, 104, 104)
    $check = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), ([float](15 * $boost))
    $check.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $check.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $check.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawLines($check, [System.Drawing.PointF[]]@([System.Drawing.PointF]::new(166, 194), [System.Drawing.PointF]::new(186, 214), [System.Drawing.PointF]::new(221, 172)))

    $g.Dispose()
    $out = Join-Path $OutDir ("app_{0}.png" -f $size)
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "wrote $out"
}
