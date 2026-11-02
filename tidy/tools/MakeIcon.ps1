# Erzeugt icon.ico (Farbverlauf-Kachel mit Glitzer-Stern, passend zum Programm-Design)
Add-Type -AssemblyName System.Drawing
$root = Join-Path (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)) 'assets'

function Star($g, $brush, $cx, $cy, $r, $k) {
    # vierzackiger Stern; $k = Einzug der Innenpunkte (0..1)
    $pts = @()
    for ($i = 0; $i -lt 8; $i++) {
        $a = [Math]::PI / 4 * $i - [Math]::PI / 2
        $rad = if ($i % 2 -eq 0) { $r } else { $r * $k }
        $pts += New-Object Drawing.PointF ([single]($cx + $rad * [Math]::Cos($a))), ([single]($cy + $rad * [Math]::Sin($a)))
    }
    $g.FillPolygon($brush, [Drawing.PointF[]]$pts)
}

function Draw($size) {
    $bmp = New-Object Drawing.Bitmap $size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.Clear([Drawing.Color]::Transparent)
    $m = [int]($size * 0.04); $d = $size - 2 * $m; $rr = [int]($size * 0.22)
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddArc($m, $m, $rr, $rr, 180, 90); $path.AddArc($m + $d - $rr, $m, $rr, $rr, 270, 90)
    $path.AddArc($m + $d - $rr, $m + $d - $rr, $rr, $rr, 0, 90); $path.AddArc($m, $m + $d - $rr, $rr, $rr, 90, 90)
    $path.CloseFigure()
    $grad = New-Object Drawing.Drawing2D.LinearGradientBrush ((New-Object Drawing.Point 0, 0), (New-Object Drawing.Point $size, $size),
        [Drawing.Color]::FromArgb(124, 58, 237), [Drawing.Color]::FromArgb(14, 165, 233))
    $g.FillPath($grad, $path)
    $white = [Drawing.Brushes]::White
    Star $g $white ($size * 0.45) ($size * 0.55) ($size * 0.36) 0.22
    Star $g $white ($size * 0.74) ($size * 0.26) ($size * 0.15) 0.25
    Star $g $white ($size * 0.76) ($size * 0.74) ($size * 0.09) 0.28
    $g.Dispose()
    return $bmp
}

$sizes = 16, 32, 48, 64, 128, 256
$pngs = foreach ($s in $sizes) {
    $b = Draw $s; $ms = New-Object IO.MemoryStream; $b.Save($ms, [Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
    , $ms.ToArray()
}
$out = New-Object IO.MemoryStream; $w = New-Object IO.BinaryWriter $out
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $dim = if ($s -ge 256) { 0 } else { $s }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$pngs[$i].Length); $w.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $w.Write($p) }
[IO.File]::WriteAllBytes((Join-Path $root 'icon.ico'), $out.ToArray())
# Vorschau
$prev = Draw 256; $prev.Save((Join-Path $root 'icon-preview.png')); $prev.Dispose()
Write-Host 'icon.ico erstellt.'

