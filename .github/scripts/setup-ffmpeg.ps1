$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$lock = Get-Content -Raw (Join-Path $repo '.github/dependencies/ffmpeg.json') | ConvertFrom-Json
$target = Join-Path $repo 'third_party/ffmpeg'
$marker = Join-Path $target '.ci-sha256'
if ((Test-Path $marker) -and (Get-Content -Raw $marker).Trim() -eq $lock.sha256) {
    Write-Output "FFmpeg $($lock.version) restored from cache."
    exit 0
}
if (Test-Path $target) { throw 'Existing FFmpeg directory is not the locked CI distribution. Use a clean checkout.' }
$download = Join-Path $repo 'build/ffmpeg-download'
New-Item -ItemType Directory -Path $download -Force | Out-Null
$archive = Join-Path $download 'ffmpeg.zip'
Invoke-WebRequest -Uri $lock.url -OutFile $archive
if ((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $lock.sha256) {
    throw 'FFmpeg archive checksum mismatch.'
}
Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $download 'extracted')
$roots = @(Get-ChildItem (Join-Path $download 'extracted') -Directory)
if ($roots.Count -ne 1) { throw 'Unexpected FFmpeg archive layout.' }
foreach ($file in @('include/libavformat/avformat.h', 'lib/avcodec.lib', 'bin/ffmpeg.exe', 'LICENSE.txt')) {
    if (-not (Test-Path (Join-Path $roots[0].FullName $file))) { throw "Missing FFmpeg file: $file" }
}
New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
# The target is absent; move exactly the verified extraction root into this checkout.
$workspacePrefix = [IO.Path]::GetFullPath($repo) + [IO.Path]::DirectorySeparatorChar
foreach ($path in @($roots[0].FullName, $target)) {
    if (-not [IO.Path]::GetFullPath($path).StartsWith($workspacePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'FFmpeg source and destination must stay inside this checkout.'
    }
}
Move-Item -LiteralPath $roots[0].FullName -Destination $target
Set-Content -LiteralPath $marker -Value $lock.sha256
