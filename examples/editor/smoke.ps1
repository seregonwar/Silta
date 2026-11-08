param(
    [string]$BuildDirectory = 'build/win',
    [ValidateSet('d3d11','vulkan')][string]$Backend = 'd3d11'
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Push-Location $repo
try {
    $bin = Join-Path $BuildDirectory 'examples/editor/Release'
    $ffmpeg = Join-Path $repo 'third_party/ffmpeg/bin/ffmpeg.exe'
    $run = Join-Path $repo ('build/editor-test/' + [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $run | Out-Null
    function Native([string]$Program, [string[]]$Arguments) {
        & $Program @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)" }
    }
    Native $ffmpeg @('-v','error','-f','lavfi','-i','testsrc2=size=1920x1080:rate=30','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t','4','-c:v','libopenh264','-b:v','3M','-c:a','aac',"$run/clip-a.mp4")
    Native $ffmpeg @('-v','error','-f','lavfi','-i','testsrc2=size=1280x720:rate=24','-f','lavfi','-i','sine=frequency=660:sample_rate=44100','-t','3','-c:v','libopenh264','-b:v','2M','-c:a','aac',"$run/clip-b.mp4")
    Native $ffmpeg @('-v','error','-f','lavfi','-i','smptebars=size=1280x720:rate=30','-t','2','-c:v','libopenh264',"$run/bars.mp4")
    Native $ffmpeg @('-v','error','-f','lavfi','-i','sine=frequency=220:sample_rate=44100','-t','7','-c:a','aac',"$run/music.m4a")
    Native "$bin/cut_media_test.exe" @("$run/clip-a.mp4","$run/clip-b.mp4","$run/bars.mp4","$run/music.m4a")
    Native "$bin/cut_engine_test.exe" @("$run/clip-a.mp4","$run/engine.mp4")
    # Real input through ImGui, in logical window coordinates. Assertions inspect
    # the resulting model; no edit operation is called directly by this script.
    $script = @(
        'await:analysis','await:preview','check:clips=3',
        'click:250,690','seek:1','key:s','check:clips=4',
        'key:undo','check:clips=3','key:redo','check:clips=4',
        'click:200,690','key:delete','check:clips=3','key:undo','check:clips=4',
        'click:200,690','click:440,535','check:clips=3',
        'key:undo','check:clips=4','key:undo','check:clips=3',
        'down:163,690','move:203,690','up:203,690','check:start=0.5','check:in=0.5',
        'key:undo','click:250,690','check:start=0',
        'down:250,690','move:327,625','up:327,625','check:start=1','check:track=0',
        'key:undo','click:250,690',
        'down:477,690','move:437,690','up:437,690','check:out=3.5',
        'key:undo','click:250,690','click:1255,330','check:volume=0.5',
        'key:undo','click:250,690','check:volume=1',
        'down:150,270','move:800,625','up:800,625','check:clips=4','check:track=0','check:start=8',
        'key:undo','check:clips=3','click:220,535','click:320,690','check:clips=4',
        'key:undo','check:clips=3','key:v','click:250,690','click:30,700','key:delete','check:clips=3',
        'click:30,700','click:400,575','check:position=3','click:1305,530','check:zoom=129.58',
        'seek:1.5','sleep:300',"shot:$run/editor.png",
        "export:$run/timeline.mp4",'sleep:300',"shot:$run/export-progress.png",
        'await:export','check:export=1','sleep:300',"shot:$run/export-done.png",
        'click:900,585','sleep:200','check:dialog=0',"shot:$run/export-toast.png",'quit'
    ) -join ';'
    $arguments = @('--hidden',"--backend=$Backend","--import=$run/clip-a.mp4","--import=$run/clip-b.mp4","--import=$run/music.m4a","--script=$script")
    $arguments = $arguments | ForEach-Object { '"' + $_ + '"' }
    $process = Start-Process -FilePath "$bin/silta_cut.exe" -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru -RedirectStandardError "$run/ui-errors.txt"
    if ($process.ExitCode -ne 0) { throw "UI test failed: $(Get-Content -Raw -LiteralPath "$run/ui-errors.txt")" }
    Native $ffmpeg @('-v','error','-i',"$run/timeline.mp4",'-f','null','-')
    Write-Output "All tests passed ($Backend). Screenshots and MP4: $run"
} finally {
    Pop-Location
}
