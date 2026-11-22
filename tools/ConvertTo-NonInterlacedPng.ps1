param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string]$InputPath,

    [Parameter(Position = 1)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Get-PngChunks([byte[]]$Bytes) {
    $offset = 8
    while ($offset -lt $Bytes.Length) {
        if ($Bytes.Length - $offset -lt 12) { throw 'Truncated PNG chunk.' }
        $lengthBytes = [byte[]]$Bytes[$offset..($offset + 3)]
        [Array]::Reverse($lengthBytes)
        $length = [BitConverter]::ToUInt32($lengthBytes, 0)
        $end = [long]$offset + $length + 12
        if ($end -gt $Bytes.Length) { throw 'Invalid PNG chunk length.' }
        [PSCustomObject]@{
            Type = [Text.Encoding]::ASCII.GetString($Bytes, $offset + 4, 4)
            Bytes = [byte[]]$Bytes[$offset..($end - 1)]
        }
        $offset = $end
    }
}

$sourcePath = (Resolve-Path -LiteralPath $InputPath).ProviderPath
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Input is not a file: $sourcePath"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path ([IO.Path]::GetDirectoryName($sourcePath)) (
        [IO.Path]::GetFileNameWithoutExtension($sourcePath) + '.non-interlaced.png')
}
$destinationPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
if ([IO.Path]::GetExtension($destinationPath) -ine '.png') {
    throw 'Output must have a .png extension.'
}
if (Test-Path -LiteralPath $destinationPath) {
    throw "Output already exists; choose another path: $destinationPath"
}

$originalBytes = [IO.File]::ReadAllBytes($sourcePath)
if ($originalBytes.Length -lt 33 -or
    [BitConverter]::ToString($originalBytes, 0, 8) -ne '89-50-4E-47-0D-0A-1A-0A') {
    throw 'Input must be a PNG image.'
}
$originalChunks = @(Get-PngChunks $originalBytes)
if ($originalChunks.Type -contains 'acTL') {
    throw 'Animated PNG is not supported.'
}
if ($originalChunks.Type -contains 'dSIG') {
    throw 'Digitally signed PNG is not supported.'
}

$originalBitmap = [Drawing.Bitmap]::new($sourcePath)
$encodedStream = [IO.MemoryStream]::new()
$outputStream = [IO.MemoryStream]::new()
try {
    $originalBitmap.Save($encodedStream, [Drawing.Imaging.ImageFormat]::Png)
    $encodedBytes = $encodedStream.ToArray()
    if ($encodedBytes[28] -ne 0) { throw 'Encoder did not produce a non-interlaced PNG.' }
    for ($index = 16; $index -lt 28; $index++) {
        if ($originalBytes[$index] -ne $encodedBytes[$index]) {
            throw 'This PNG pixel format cannot be preserved by the Windows encoder.'
        }
    }
    $newChunks = @(Get-PngChunks $encodedBytes)
    $outputStream.Write($originalBytes, 0, 8)
    $wroteImageData = $false
    foreach ($chunk in $originalChunks) {
        if ($chunk.Type -eq 'IHDR') {
            $header = ($newChunks | Where-Object Type -eq 'IHDR').Bytes
            $outputStream.Write($header, 0, $header.Length)
        } elseif ($chunk.Type -eq 'IDAT') {
            if (-not $wroteImageData) {
                foreach ($imageChunk in ($newChunks | Where-Object Type -eq 'IDAT')) {
                    $outputStream.Write($imageChunk.Bytes, 0, $imageChunk.Bytes.Length)
                }
                $wroteImageData = $true
            }
        } elseif ($chunk.Type -eq 'PLTE') {
            $palette = @($newChunks | Where-Object Type -eq 'PLTE')
            if ($palette.Count -ne 1 -or
                [Convert]::ToBase64String($chunk.Bytes) -ne [Convert]::ToBase64String($palette[0].Bytes)) {
                throw 'This PNG palette cannot be preserved by the Windows encoder.'
            }
            $outputStream.Write($chunk.Bytes, 0, $chunk.Bytes.Length)
        } else {
            $outputStream.Write($chunk.Bytes, 0, $chunk.Bytes.Length)
        }
    }
    $outputBytes = $outputStream.ToArray()
    $outputStream.Position = 0
    $convertedBitmap = [Drawing.Bitmap]::new($outputStream)
    try {
        for ($row = 0; $row -lt $originalBitmap.Height; $row++) {
            for ($column = 0; $column -lt $originalBitmap.Width; $column++) {
                if ($originalBitmap.GetPixel($column, $row).ToArgb() -ne
                    $convertedBitmap.GetPixel($column, $row).ToArgb()) {
                    throw "Pixel verification failed at $column,$row."
                }
            }
        }
        $width = $originalBitmap.Width
        $height = $originalBitmap.Height
    } finally {
        $convertedBitmap.Dispose()
    }
} finally {
    $originalBitmap.Dispose()
    $encodedStream.Dispose()
    $outputStream.Dispose()
}

[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destinationPath)) | Out-Null
$destination = [IO.File]::Open($destinationPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
try {
    $destination.Write($outputBytes, 0, $outputBytes.Length)
} finally {
    $destination.Dispose()
}
Write-Output "Created: $destinationPath"
Write-Output "Non-interlaced PNG; ${width}x${height}; all pixels verified unchanged."
