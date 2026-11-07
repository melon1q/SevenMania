$ErrorActionPreference = 'Stop'
$destination = Join-Path $PSScriptRoot '..\Samples\click.wav'
$destination = [System.IO.Path]::GetFullPath($destination)
$sampleRate = 44100
$channels = 2
$bitsPerSample = 16
$frameCount = 8820
$dataSize = $frameCount * $channels * ($bitsPerSample / 8)
$stream = [System.IO.File]::Open($destination, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
$writer = [System.IO.BinaryWriter]::new($stream)
try {
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF'))
    $writer.Write([uint32](36 + $dataSize))
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
    $writer.Write([uint32]16)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$channels)
    $writer.Write([uint32]$sampleRate)
    $writer.Write([uint32]($sampleRate * $channels * $bitsPerSample / 8))
    $writer.Write([uint16]($channels * $bitsPerSample / 8))
    $writer.Write([uint16]$bitsPerSample)
    $writer.Write([System.Text.Encoding]::ASCII.GetBytes('data'))
    $writer.Write([uint32]$dataSize)
    for ($i = 0; $i -lt $frameCount; $i++) {
        $envelope = [Math]::Min(1.0, ($frameCount - $i) / 1200.0)
        $sample = [int16]([Math]::Sin(2.0 * [Math]::PI * 523.25 * $i / $sampleRate) * 5500.0 * $envelope)
        $writer.Write($sample)
        $writer.Write($sample)
    }
}
finally {
    $writer.Dispose()
}
Write-Output $destination
