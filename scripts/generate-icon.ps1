[CmdletBinding()]
param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $PSScriptRoot '..\resources\app.ico'
}

function New-RoundedRectanglePath {
    param(
        [System.Drawing.RectangleF]$Bounds,
        [float]$Radius
    )

    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $Radius * 2
    $arc = [System.Drawing.RectangleF]::new($Bounds.X, $Bounds.Y, $diameter, $diameter)
    $path.AddArc($arc, 180, 90)
    $arc.X = $Bounds.Right - $diameter
    $path.AddArc($arc, 270, 90)
    $arc.Y = $Bounds.Bottom - $diameter
    $path.AddArc($arc, 0, 90)
    $arc.X = $Bounds.X
    $path.AddArc($arc, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-AgentLatchIconDib {
    param([int]$Size)

    $bitmap = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $scale = $Size / 256.0

    $surface = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 11, 18, 32))
    $border = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 38, 56, 82), [Math]::Max(1.0, 6 * $scale))
    $surfacePath = New-RoundedRectanglePath ([System.Drawing.RectangleF]::new(14 * $scale, 14 * $scale, 228 * $scale, 228 * $scale)) (54 * $scale)
    $graphics.FillPath($surface, $surfacePath)
    $graphics.DrawPath($border, $surfacePath)

    $shackle = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 110, 231, 183), [Math]::Max(2.0, 18 * $scale))
    $shackle.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $shackle.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawArc($shackle, 89 * $scale, 54 * $scale, 78 * $scale, 84 * $scale, 180, 180)
    $graphics.DrawLine($shackle, 89 * $scale, 96 * $scale, 89 * $scale, 121 * $scale)
    $graphics.DrawLine($shackle, 167 * $scale, 96 * $scale, 167 * $scale, 121 * $scale)

    $body = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 52, 211, 153))
    $bodyPath = New-RoundedRectanglePath ([System.Drawing.RectangleF]::new(69 * $scale, 112 * $scale, 118 * $scale, 96 * $scale)) (25 * $scale)
    $graphics.FillPath($body, $bodyPath)

    $keyBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 11, 56, 47))
    $graphics.FillEllipse($keyBrush, 115 * $scale, 142 * $scale, 26 * $scale, 26 * $scale)
    $keyPen = [System.Drawing.Pen]::new($keyBrush.Color, [Math]::Max(2.0, 11 * $scale))
    $keyPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $keyPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawLine($keyPen, 128 * $scale, 164 * $scale, 128 * $scale, 183 * $scale)

    $nodeBlue = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 96, 165, 250))
    $nodeViolet = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 167, 139, 250))
    $nodePen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 59, 85, 116), [Math]::Max(1.0, 5 * $scale))
    $graphics.DrawLine($nodePen, 53 * $scale, 128 * $scale, 69 * $scale, 128 * $scale)
    $graphics.DrawLine($nodePen, 187 * $scale, 128 * $scale, 203 * $scale, 128 * $scale)
    $graphics.FillEllipse($nodeBlue, 39 * $scale, 121 * $scale, 14 * $scale, 14 * $scale)
    $graphics.FillEllipse($nodeViolet, 203 * $scale, 121 * $scale, 14 * $scale, 14 * $scale)

    # Store standard 32-bit DIB frames rather than PNG-compressed frames.
    # Windows SDK resource compilers accept this format consistently on both
    # x64 and ARM64 build hosts.
    $stream = [System.IO.MemoryStream]::new()
    $dibWriter = [System.IO.BinaryWriter]::new($stream)
    $maskRowBytes = [int]([Math]::Ceiling($Size / 32.0) * 4)
    $pixelBytes = $Size * $Size * 4

    $dibWriter.Write([uint32]40)             # BITMAPINFOHEADER size
    $dibWriter.Write([int32]$Size)
    $dibWriter.Write([int32]($Size * 2))     # XOR bitmap plus AND mask
    $dibWriter.Write([uint16]1)
    $dibWriter.Write([uint16]32)
    $dibWriter.Write([uint32]0)              # BI_RGB
    $dibWriter.Write([uint32]$pixelBytes)
    $dibWriter.Write([int32]0)
    $dibWriter.Write([int32]0)
    $dibWriter.Write([uint32]0)
    $dibWriter.Write([uint32]0)

    for ($y = $Size - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $Size; $x++) {
            $pixel = $bitmap.GetPixel($x, $y)
            $dibWriter.Write([byte]$pixel.B)
            $dibWriter.Write([byte]$pixel.G)
            $dibWriter.Write([byte]$pixel.R)
            $dibWriter.Write([byte]$pixel.A)
        }
    }
    for ($y = $Size - 1; $y -ge 0; $y--) {
        $maskRow = [byte[]]::new($maskRowBytes)
        for ($x = 0; $x -lt $Size; $x++) {
            if ($bitmap.GetPixel($x, $y).A -eq 0) {
                $byteIndex = [int][Math]::Floor($x / 8.0)
                $mask = 0x80 -shr ($x % 8)
                $maskRow[$byteIndex] = [byte]($maskRow[$byteIndex] -bor $mask)
            }
        }
        $dibWriter.Write($maskRow)
    }
    $dibWriter.Flush()
    $bytes = $stream.ToArray()

    $dibWriter.Dispose()
    $stream.Dispose()
    $nodePen.Dispose()
    $nodeBlue.Dispose()
    $nodeViolet.Dispose()
    $keyPen.Dispose()
    $keyBrush.Dispose()
    $bodyPath.Dispose()
    $body.Dispose()
    $shackle.Dispose()
    $surfacePath.Dispose()
    $border.Dispose()
    $surface.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    return $bytes
}

$sizes = @(16, 24, 32, 48, 64, 256)
$images = [System.Collections.Generic.List[byte[]]]::new()
foreach ($size in $sizes) {
    # A PowerShell pipeline flattens byte arrays. A typed list preserves each
    # complete image frame for the ICO directory offsets and lengths below.
    $images.Add((New-AgentLatchIconDib -Size $size))
}
$fullOutputPath = [System.IO.Path]::GetFullPath($OutputPath)
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($fullOutputPath)) | Out-Null

$file = [System.IO.File]::Open($fullOutputPath, [System.IO.FileMode]::Create)
$writer = [System.IO.BinaryWriter]::new($file)
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$sizes.Count)

$offset = 6 + (16 * $sizes.Count)
for ($index = 0; $index -lt $sizes.Count; $index++) {
    $size = $sizes[$index]
    $writer.Write([byte]($(if ($size -eq 256) { 0 } else { $size })))
    $writer.Write([byte]($(if ($size -eq 256) { 0 } else { $size })))
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$images[$index].Length)
    $writer.Write([uint32]$offset)
    $offset += $images[$index].Length
}
foreach ($image in $images) {
    $writer.Write($image)
}

$writer.Dispose()
$file.Dispose()
Write-Host "Generated $fullOutputPath"
