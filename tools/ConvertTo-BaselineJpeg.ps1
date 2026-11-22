param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string]$InputPath,

    [Parameter(Position = 1)]
    [string]$OutputPath,

    [string]$JpegtranPath
)

$ErrorActionPreference = 'Stop'

function Get-JpegFrame([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 4 -or $bytes[0] -ne 0xFF -or $bytes[1] -ne 0xD8) {
        throw "Input is not a JPEG image: $Path"
    }
    $offset = 2
    while ($offset -lt $bytes.Length) {
        if ($bytes[$offset] -ne 0xFF) { throw 'Invalid JPEG marker.' }
        while ($offset -lt $bytes.Length -and $bytes[$offset] -eq 0xFF) { $offset++ }
        if ($offset -ge $bytes.Length) { throw 'Truncated JPEG marker.' }
        $marker = $bytes[$offset]
        $offset++
        if ($marker -eq 0xDA -or $marker -eq 0xD9) { break }
        if ($marker -eq 0x01) { continue }
        if ($offset + 2 -gt $bytes.Length) { throw 'Truncated JPEG segment.' }
        $length = ([int]$bytes[$offset] -shl 8) -bor $bytes[$offset + 1]
        if ($length -lt 2 -or $offset + $length -gt $bytes.Length) {
            throw 'Invalid JPEG segment length.'
        }
        if ($marker -ge 0xC0 -and $marker -le 0xCF -and $marker -notin @(0xC4, 0xC8, 0xCC)) {
            if ($marker -notin @(0xC0, 0xC1, 0xC2) -or $length -lt 8) {
                throw 'Only Huffman-coded, DCT-based JPEG images are supported.'
            }
            $precision = $bytes[$offset + 2]
            $height = ([int]$bytes[$offset + 3] -shl 8) -bor $bytes[$offset + 4]
            $width = ([int]$bytes[$offset + 5] -shl 8) -bor $bytes[$offset + 6]
            $components = $bytes[$offset + 7]
            if ($precision -ne 8 -or $width -eq 0 -or $height -eq 0 -or
                $components -eq 0 -or $length -ne 8 + 3 * $components) {
                throw 'A valid 8-bit JPEG with explicit dimensions is required.'
            }
            return [PSCustomObject]@{
                Marker = $marker
                Width = $width
                Height = $height
                Components = $components
            }
        }
        $offset += $length
    }
    throw 'JPEG frame header was not found.'
}

$sourcePath = (Resolve-Path -LiteralPath $InputPath).ProviderPath
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "Input is not a file: $sourcePath"
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path ([IO.Path]::GetDirectoryName($sourcePath)) (
        [IO.Path]::GetFileNameWithoutExtension($sourcePath) + '.baseline.jpg')
}
$destinationPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
if ([IO.Path]::GetExtension($destinationPath) -notin @('.jpg', '.jpeg')) {
    throw 'Output must have a .jpg or .jpeg extension.'
}
if (Test-Path -LiteralPath $destinationPath) {
    throw "Output already exists; choose another path: $destinationPath"
}
$inputFrame = Get-JpegFrame $sourcePath

if ([string]::IsNullOrWhiteSpace($JpegtranPath)) {
    $localTool = Join-Path $PSScriptRoot 'jpegtran.exe'
    if (Test-Path -LiteralPath $localTool -PathType Leaf) {
        $JpegtranPath = $localTool
    } else {
        $command = Get-Command jpegtran -CommandType Application -ErrorAction SilentlyContinue
        if ($command) { $JpegtranPath = $command.Source }
    }
}
if ([string]::IsNullOrWhiteSpace($JpegtranPath)) {
    throw 'jpegtran was not found. Install libjpeg-turbo, place jpegtran.exe and its DLLs beside this script, add it to PATH, or pass -JpegtranPath. Downloads: https://github.com/libjpeg-turbo/libjpeg-turbo/releases'
}
$toolPath = (Resolve-Path -LiteralPath $JpegtranPath).ProviderPath
if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
    throw "jpegtran is not a file: $toolPath"
}

$destinationDirectory = [IO.Path]::GetDirectoryName($destinationPath)
[IO.Directory]::CreateDirectory($destinationDirectory) | Out-Null
$temporaryPath = Join-Path $destinationDirectory ('.jpegtran-' + [guid]::NewGuid().ToString('N') + '.tmp')
try {
    & $toolPath -copy all -optimize -outfile $temporaryPath $sourcePath
    if ($LASTEXITCODE -ne 0) { throw "jpegtran failed or reported warnings (exit code $LASTEXITCODE)." }
    $outputFrame = Get-JpegFrame $temporaryPath
    if ($outputFrame.Marker -ne 0xC0) {
        throw 'jpegtran could not produce baseline JPEG without changing the source quantization. Output was not saved.'
    }
    if ($outputFrame.Width -ne $inputFrame.Width -or $outputFrame.Height -ne $inputFrame.Height -or
        $outputFrame.Components -ne $inputFrame.Components) {
        throw 'JPEG dimensions or component count changed. Output was not saved.'
    }
    [IO.File]::Move($temporaryPath, $destinationPath)
} finally {
    if (Test-Path -LiteralPath $temporaryPath) {
        Remove-Item -LiteralPath $temporaryPath -Force
    }
}
Write-Output "Created: $destinationPath"
Write-Output "Baseline JPEG (SOF0); $($outputFrame.Width)x$($outputFrame.Height); lossless DCT-coefficient conversion."
