# Generates the mod's own icons (PNG) and ding sounds (WAV). No third-party assets are used.
# Run:  powershell -ExecutionPolicy Bypass -File tools\gen_assets.ps1
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$spr  = Join-Path $root 'resources\sprites'
$sfx  = Join-Path $root 'resources\sfx'
New-Item -ItemType Directory -Force $spr, $sfx | Out-Null

function C($hex, $a = 255) {
    $c = [System.Drawing.ColorTranslator]::FromHtml($hex)
    [System.Drawing.Color]::FromArgb($a, $c.R, $c.G, $c.B)
}
function New-Canvas {
    $bmp = New-Object System.Drawing.Bitmap 128, 128
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([System.Drawing.Color]::Transparent)
    return ,@($bmp, $g)
}
function New-Pen($color, $w) {
    $p = New-Object System.Drawing.Pen $color, $w
    $p.StartCap = 'Round'; $p.EndCap = 'Round'; $p.LineJoin = 'Round'
    return $p
}
function Save-Icon($cv, $name) {
    $cv[1].Dispose()
    $cv[0].Save((Join-Path $spr $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $cv[0].Dispose()
}

# ---- glyphs. drawn in $col so the same code makes white tab icons and the colored menu button ----
function Glyph-Hacks($g, $col) {                     # lightning bolt
    $b = New-Object System.Drawing.SolidBrush $col
    $pts = @(
        (New-Object System.Drawing.PointF 72, 14), (New-Object System.Drawing.PointF 28, 72),
        (New-Object System.Drawing.PointF 58, 72), (New-Object System.Drawing.PointF 50, 114),
        (New-Object System.Drawing.PointF 100, 52), (New-Object System.Drawing.PointF 68, 52)
    )
    $g.FillPolygon($b, $pts)
}
function Glyph-Hitbox($g, $col) {                    # outer box + inner box
    $p = New-Pen $col 9
    $g.DrawRectangle($p, 22, 22, 84, 84)
    $b = New-Object System.Drawing.SolidBrush $col
    $g.FillRectangle($b, 52, 52, 24, 24)
}
function Glyph-Path($g, $col) {                      # S-curve with dots
    $p = New-Pen $col 9
    $g.DrawBezier($p, 20, 104, 20, 50, 108, 78, 108, 24)
    $b = New-Object System.Drawing.SolidBrush $col
    $g.FillEllipse($b, 8, 92, 24, 24)
    $g.FillEllipse($b, 96, 12, 24, 24)
}
function Glyph-Display($g, $col) {                   # eye
    $p = New-Pen $col 9
    $g.DrawBezier($p, 12, 64, 36, 22, 92, 22, 116, 64)
    $g.DrawBezier($p, 12, 64, 36, 106, 92, 106, 116, 64)
    $b = New-Object System.Drawing.SolidBrush $col
    $g.FillEllipse($b, 46, 46, 36, 36)
}
function Glyph-Counter($g, $col) {                   # frame ticks with a highlighted window
    $p = New-Pen $col 9
    for ($i = 0; $i -lt 7; $i++) {
        $x = 18 + 15 * $i
        $h = 44; if ($i -ge 2 -and $i -le 4) { $h = 64 }
        $g.DrawLine($p, $x, 26, $x, 26 + $h)
    }
    $g.DrawLine($p, 33, 108, 93, 108)
    $g.DrawLine($p, 33, 100, 33, 108)
    $g.DrawLine($p, 93, 100, 93, 108)
}
function Glyph-Speed($g, $col) {                     # gauge
    $p = New-Pen $col 9
    $g.DrawArc($p, 16, 24, 96, 96, 150, 240)
    $g.DrawLine($p, 64, 72, 92, 44)
    $b = New-Object System.Drawing.SolidBrush $col
    $g.FillEllipse($b, 54, 62, 20, 20)
}

# tab icons (white; tinted in-game)
$white = C '#FFFFFF'
$tabs = [ordered]@{
    'kt_tab_hacks.png'   = 'Glyph-Hacks'
    'kt_tab_hitbox.png'  = 'Glyph-Hitbox'
    'kt_tab_path.png'    = 'Glyph-Path'
    'kt_tab_display.png' = 'Glyph-Display'
    'kt_tab_counter.png' = 'Glyph-Counter'
    'kt_tab_speed.png'   = 'Glyph-Speed'
}
foreach ($k in $tabs.Keys) {
    $cv = New-Canvas
    & $tabs[$k] $cv[1] $white
    Save-Icon $cv $k
}

# pause/main-menu button: dark disc, mint ring, mint hitbox glyph, frame ticks
$cv = New-Canvas; $g = $cv[1]
$g.FillEllipse((New-Object System.Drawing.SolidBrush (C '#161A24')), 4, 4, 120, 120)
$g.DrawEllipse((New-Pen (C '#4DD6A4') 6), 7, 7, 114, 114)
$g.ScaleTransform(0.62, 0.62); $g.TranslateTransform(40, 38)
Glyph-Hitbox $g (C '#4DD6A4')
$g.ResetTransform()
$tick = New-Pen (C '#F2C14E') 5
foreach ($x in 40, 52, 64, 76, 88) { $g.DrawLine($tick, $x, 98, $x, 108) }
Save-Icon $cv 'kt_menu.png'

# ---- ding sounds: ding_0.wav .. ding_12.wav, pitch rises with window size ----
function Write-Wav($path, $freq) {
    $sr = 44100; $n = [int]($sr * 0.30)
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    $bw.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF')); $bw.Write([int](36 + $n * 2))
    $bw.Write([System.Text.Encoding]::ASCII.GetBytes('WAVEfmt ')); $bw.Write([int]16)
    $bw.Write([int16]1); $bw.Write([int16]1); $bw.Write([int]$sr); $bw.Write([int]($sr * 2))
    $bw.Write([int16]2); $bw.Write([int16]16)
    $bw.Write([System.Text.Encoding]::ASCII.GetBytes('data')); $bw.Write([int]($n * 2))
    for ($i = 0; $i -lt $n; $i++) {
        $t = $i / $sr
        $env = [Math]::Exp(-$t * 13) * [Math]::Min(1.0, $t * 500)
        $s = ([Math]::Sin(2 * [Math]::PI * $freq * $t) + 0.3 * [Math]::Sin(4 * [Math]::PI * $freq * $t)) * $env * 0.45
        $bw.Write([int16]($s * 32767))
    }
    $bw.Flush()
    [System.IO.File]::WriteAllBytes($path, $ms.ToArray())
}
for ($n = 0; $n -le 12; $n++) {
    Write-Wav (Join-Path $sfx "ding_$n.wav") (440 * [Math]::Pow(2, $n * 2 / 12))
}
Write-Host "assets written to $spr and $sfx"
