$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$noteLogo = Join-Path $PSScriptRoot 'assets/notelab-logo.png'
$noteIcon = Join-Path $PSScriptRoot 'assets/notelab.ico'
$noteOriginal = [Drawing.Image]::FromFile($noteLogo)
$noteSizes = @(16, 24, 32, 48, 64, 128, 256)
$noteFrames = [Collections.Generic.List[byte[]]]::new()
try {
    foreach ($noteSize in $noteSizes) {
        $noteBitmap = [Drawing.Bitmap]::new($noteSize, $noteSize)
        $noteGraphics = [Drawing.Graphics]::FromImage($noteBitmap)
        $noteStream = [IO.MemoryStream]::new()
        try {
            $noteGraphics.Clear([Drawing.Color]::Black)
            $noteGraphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $noteScale = [Math]::Min($noteSize / $noteOriginal.Width, $noteSize / $noteOriginal.Height)
            $noteWidth = [single]($noteOriginal.Width * $noteScale)
            $noteHeight = [single]($noteOriginal.Height * $noteScale)
            $noteGraphics.DrawImage($noteOriginal, [single](($noteSize - $noteWidth) / 2), [single](($noteSize - $noteHeight) / 2), $noteWidth, $noteHeight)
            $noteBitmap.Save($noteStream, [Drawing.Imaging.ImageFormat]::Png)
            $noteFrames.Add($noteStream.ToArray())
        } finally { $noteStream.Dispose(); $noteGraphics.Dispose(); $noteBitmap.Dispose() }
    }
} finally { $noteOriginal.Dispose() }
$noteOutput = [IO.File]::Create($noteIcon)
$noteWriter = [IO.BinaryWriter]::new($noteOutput)
try {
    $noteWriter.Write([uint16]0); $noteWriter.Write([uint16]1); $noteWriter.Write([uint16]$noteSizes.Count)
    $noteOffset = [uint32](6 + 16 * $noteSizes.Count)
    for ($noteIndex = 0; $noteIndex -lt $noteSizes.Count; ++$noteIndex) {
        $noteDimension = [byte]($noteSizes[$noteIndex] % 256)
        $noteWriter.Write($noteDimension); $noteWriter.Write($noteDimension)
        $noteWriter.Write([byte]0); $noteWriter.Write([byte]0)
        $noteWriter.Write([uint16]1); $noteWriter.Write([uint16]32)
        $noteWriter.Write([uint32]$noteFrames[$noteIndex].Length); $noteWriter.Write($noteOffset)
        $noteOffset += $noteFrames[$noteIndex].Length
    }
    foreach ($noteFrame in $noteFrames) { $noteWriter.Write($noteFrame) }
} finally { $noteWriter.Dispose(); $noteOutput.Dispose() }
Write-Output 'Native Windows icon created from the supplied logo. No artwork generated.'
