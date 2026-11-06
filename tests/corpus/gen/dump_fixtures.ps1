<#
.SYNOPSIS
  Dumps every corpus DWG to tests/fixtures/<name>.json with the pinned dwgread.
  dwgread prints "SUCCESS"/errors on stderr; that is captured to a .log beside the JSON.
#>
param([Parameter(Mandatory = $true)][string]$DwgRead)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dwgDir = (Resolve-Path (Join-Path $here '..\dwg')).Path
$fixDir = (Resolve-Path (Join-Path $here '..\..\fixtures')).Path
$ver = (& $DwgRead --version) | Select-Object -First 1
Set-Content -Path (Join-Path $fixDir 'DWGREAD_VERSION.txt') -Value $ver -Encoding ascii
Get-ChildItem $dwgDir -Filter *.dwg | Sort-Object Name | ForEach-Object {
    $json = Join-Path $fixDir ($_.BaseName + '.json')
    $err = Join-Path ([IO.Path]::GetTempPath()) ($_.BaseName + '.dwgread.err')
    $p = Start-Process -FilePath $DwgRead -ArgumentList @('-O', 'JSON', '-o', "`"$json`"", "`"$($_.FullName)`"") `
        -NoNewWindow -Wait -PassThru -RedirectStandardError $err
    $msg = (Get-Content $err -Raw -ErrorAction SilentlyContinue)
    if ($msg) { $msg = $msg.Trim() -replace '\s+', ' ' }
    Write-Host ("{0,-24} exit {1}  {2,9} bytes  {3}" -f $_.BaseName, $p.ExitCode, (Get-Item $json).Length, $msg)
}
