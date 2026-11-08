param([string]$BuildDirectory = 'build/ci', [string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$output = Join-Path $BuildDirectory 'gallery-test'
New-Item -ItemType Directory -Force $output | Out-Null
foreach ($mode in @('light', 'dark')) {
    $shot = Join-Path $output "gallery-$mode.png"
    $arguments = @('--hidden', '--backend=d3d11', "--script=sleep:300;shot:$shot;quit")
    if ($mode -eq 'dark') { $arguments += '--dark' }
    $arguments = $arguments | ForEach-Object { '"' + $_ + '"' }
    $process = Start-Process -FilePath "$BuildDirectory/examples/gallery/$Configuration/silta_gallery.exe" -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(60000)) {
        $process.Kill(); throw 'Gallery timed out.'
    }
    if ($process.ExitCode -ne 0) { throw "Gallery exited with $($process.ExitCode)." }
    if (-not (Test-Path $shot) -or (Get-Item $shot).Length -lt 1000) { throw "Missing screenshot: $shot" }
}
