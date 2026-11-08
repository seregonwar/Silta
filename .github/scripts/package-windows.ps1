param([string]$BuildDirectory = 'build/ci')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$build = (Resolve-Path $BuildDirectory).Path
$dist = Join-Path $repo 'build/packages'
$package = Join-Path $dist 'Silta-windows-x64'
if (Test-Path $package) { throw 'Package directory already exists; use a clean package output.' }
New-Item -ItemType Directory -Path $package -Force | Out-Null
foreach ($example in @('gallery', 'hello')) {
    Copy-Item -LiteralPath "$build/examples/$example/Release/silta_$example.exe" -Destination $package
}
Copy-Item -LiteralPath "$build/examples/editor/Release/silta_cut.exe" -Destination $package
Copy-Item -Path "$build/examples/editor/Release/*.dll" -Destination $package
Copy-Item -LiteralPath "$repo/assets/fonts" -Destination "$package/fonts" -Recurse
Copy-Item -LiteralPath "$repo/LICENSE", "$repo/README.md" -Destination $package
Copy-Item -LiteralPath "$repo/examples/editor/README.md" -Destination "$package/Silta-Cut.md"
New-Item -ItemType Directory -Path "$package/docs" -Force | Out-Null
Copy-Item -LiteralPath "$repo/docs/screenshots" -Destination "$package/docs/screenshots" -Recurse -Force
$licenses = Join-Path $package 'licenses'
New-Item -ItemType Directory -Path $licenses -Force | Out-Null
Copy-Item -LiteralPath "$build/_deps/imgui-src/LICENSE.txt" -Destination "$licenses/Dear-ImGui.txt"
Copy-Item -LiteralPath "$build/_deps/glfw-src/LICENSE.md" -Destination "$licenses/GLFW.txt"
Copy-Item -LiteralPath "$build/_deps/freetype-src/docs/FTL.TXT" -Destination "$licenses/FreeType.txt"
Copy-Item -LiteralPath "$build/_deps/harfbuzz-src/COPYING" -Destination "$licenses/HarfBuzz.txt"
Copy-Item -LiteralPath "$build/_deps/sheenbidi-src/LICENSE" -Destination "$licenses/SheenBidi.txt"
Copy-Item -LiteralPath "$repo/assets/fonts/Inter-LICENSE.txt" -Destination "$licenses/Inter.txt"
# The header carries miniaudio's full license text and version information.
Copy-Item -LiteralPath "$repo/third_party/miniaudio/miniaudio.h" -Destination "$licenses/miniaudio.h"
Copy-Item -LiteralPath "$repo/third_party/ffmpeg/LICENSE.txt" -Destination "$licenses/FFmpeg.txt"
Copy-Item -LiteralPath "$repo/third_party/ffmpeg/doc" -Destination "$package/ffmpeg-doc" -Recurse
Copy-Item -LiteralPath "$repo/THIRD_PARTY.md" -Destination $package
$lock = Get-Content -Raw "$repo/.github/dependencies/ffmpeg.json" | ConvertFrom-Json
$commit = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve package commit.' }
@{
    project = 'Silta'
    commit = $commit
    built_at_utc = [DateTime]::UtcNow.ToString('o')
    platform = 'windows-x64'
    ffmpeg = $lock
    dependencies = @{ imgui = '1.92.9b'; freetype = '2.13.3'; glfw = '3.4'; harfbuzz = '14.6.0'; sheenbidi = '2.9.0'; miniaudio = '0.11.23'; inter = '4.1' }
} | ConvertTo-Json -Depth 5 | Set-Content "$package/BUILD.json"
# A source snapshot accompanies binaries; fetched dependencies remain upstream.
$source = Join-Path $dist 'Silta-source.zip'
& git -C $repo archive --format=zip "--output=$source" HEAD
if ($LASTEXITCODE -ne 0) { throw 'Source archive failed.' }
Compress-Archive -LiteralPath $package -DestinationPath "$dist/Silta-windows-x64.zip"
$checksums = Get-ChildItem -LiteralPath $dist -Filter '*.zip' | Sort-Object Name | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name
}
# GNU sha256sum consumes this file in the Linux publication job.
Set-Content -LiteralPath "$dist/SHA256SUMS.txt" -NoNewline -Value (($checksums -join "`n") + "`n")
