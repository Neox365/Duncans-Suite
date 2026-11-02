# Erzeugt icon.ico aus dem Bild icon-source.png (obere, grosse Kachel: Schild, Glitzer-Stern, Pfeile, Besen).
# Die Kachel wird ausgeschnitten, auf abgerundete Ecken mit transparentem Rand maskiert und in allen Icon-Groessen gespeichert.
# Das alte, selbst gezeichnete Icon liegt als icon-old.ico bereit (MakeIcon.ps1 erzeugt es neu).
Add-Type -AssemblyName System.Drawing
$root = Join-Path (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)) 'assets'
$src = New-Object Drawing.Bitmap (Join-Path $root 'icon-source.png')

# Kachel finden: alles, was sich deutlich vom hellen Hintergrund (Ecke) abhebt, in der oberen Bildhaelfte
$bg = $src.GetPixel(2, 2)
$minX = $src.Width; $minY = $src.Height; $maxX = 0; $maxY = 0
for ($y = 0; $y -lt [int]($src.Height * 0.72); $y++) { for ($x = 0; $x -lt $src.Width; $x++) {
    $c = $src.GetPixel($x, $y)
    if ([Math]::Abs($c.R - $bg.R) + [Math]::Abs($c.G - $bg.G) + [Math]::Abs($c.B - $bg.B) -gt 60) {
        if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }; if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y } } } }
$side = [Math]::Min($maxX - $minX + 1, $maxY - $minY + 1) - 2   # 1 px Rand weg: kein grauer Saum an den Kanten
$crop = New-Object Drawing.Rectangle ($minX + 1), ($minY + 1), $side, $side

function Master($size) {
    $bmp = New-Object Drawing.Bitmap $size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([Drawing.Color]::Transparent)
    $rr = [int]($size * 0.45)   # Durchmesser der Ecken (Radius ca. 22 %)
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddArc(0, 0, $rr, $rr, 180, 90); $path.AddArc($size - $rr, 0, $rr, $rr, 270, 90)
    $path.AddArc($size - $rr, $size - $rr, $rr, $rr, 0, 90); $path.AddArc(0, $size - $rr, $rr, $rr, 90, 90)
    $path.CloseFigure()
    $g.SetClip($path)
    $g.DrawImage($src, (New-Object Drawing.Rectangle 0, 0, $size, $size), $crop, [Drawing.GraphicsUnit]::Pixel)
    $g.Dispose()
    return $bmp
}
$m256 = Master 256
function Sized($size) {
    if ($size -eq 256) { return $m256 }
    $bmp = New-Object Drawing.Bitmap $size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'; $g.SmoothingMode = 'AntiAlias'
    $g.Clear([Drawing.Color]::Transparent)
    $g.DrawImage($m256, 0, 0, $size, $size)
    $g.Dispose()
    return $bmp
}

$sizes = 16, 24, 32, 48, 64, 128, 256
$pngs = foreach ($s in $sizes) { $b = Sized $s; $ms = New-Object IO.MemoryStream; $b.Save($ms, [Drawing.Imaging.ImageFormat]::Png); , $ms.ToArray() }
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
$m256.Save((Join-Path $root 'icon-preview.png'))
Write-Host "icon.ico erstellt (Kachel $($crop.Width) px, Quelle $minX,$minY)."

