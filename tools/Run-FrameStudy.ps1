param(
    [Parameter(Mandatory)][string]$Profiler,
    [Parameter(Mandatory)][string]$Label,
    [string]$OutputDirectory='build-frame-study/results',
    [int]$Repeat=1,
    [string[]]$Cases=@('creamy-gpu-replay','creamy-gpu-idle','creamy-cpu-replay','happy-gpu-replay'),
    [switch]$BoundaryOnly,
    [string]$SourceCommit='',
    [string]$ConfigTemplate=''
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe=(Get-Item -LiteralPath $Profiler).FullName
$out=[IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
New-Item -ItemType Directory -Force -Path $out | Out-Null
$songs=Join-Path $root 'build/Release/Songs'
$creamy=Join-Path $songs '2018-7ed233c287aa654e/[BTB]Creamy Sugary OVERDRIVE!!!/__btb_cso_7another.bme'
$happyFolder=(Get-ChildItem -LiteralPath $songs -Directory | Where-Object Name -Like '*ha@ppy*').FullName
$happy=Join-Path $happyFolder 'hsn_n.bms'
$oldConfig=$env:BMS_PLAYER_CONFIG_PATH
$oldBoundary=$env:BMS_STUDY_BOUNDARY_ONLY
try {
    foreach($case in $Cases) {
        $chart=if($case.StartsWith('happy')){$happy}else{$creamy}
        $mode=if($case.Contains('-cpu-')){'cpu'}else{'gpu'}
        $inputMode=if($case.EndsWith('-idle')){'idle'}else{'replay'}
        $fps=if($case.Contains('-240')){240}elseif($case.Contains('-vsync')){-1}else{0}
        $sdk=if($case.Contains('-reflex')){'reflex'}else{'off'}
        $dc=if($case.Contains('-dc-')){1}else{0}
        $name="$Label-$case-r$Repeat"
        $config=Join-Path $out "$name.ini"
        if($ConfigTemplate) {
            # The runner loads its own chart; never import a personal library or records.
            $sections=@('Display','Sound','Gameplay','System','KeyBindings','NoteColors')
            $keep=$false
            $settings=Get-Content -LiteralPath $ConfigTemplate | ForEach-Object {
                if($_ -match '^\[(.+)\]$'){$keep=$sections -contains $Matches[1]}
                if($keep){$_}
            }
            $settings | Set-Content -LiteralPath $config -Encoding Unicode
        }else{@"
[Display]
FPS=$fps
SpeedSteps=20
BGADarkness=50
LaneTransparency=34
ScreenMode=1
ResolutionWidth=1920
ResolutionHeight=1080
DirectComposition=$dc
[System]
CpuLimitPercent=100
MemoryLimitPercent=100
GpuLimitPercent=100
"@ | Set-Content -LiteralPath $config -Encoding Unicode
        }
        Copy-Item -LiteralPath $config -Destination (Join-Path $out "$name.initial.ini")
        $env:BMS_PLAYER_CONFIG_PATH=$config
        if($BoundaryOnly){$env:BMS_STUDY_BOUNDARY_ONLY='1'}else{Remove-Item Env:BMS_STUDY_BOUNDARY_ONLY -ErrorAction SilentlyContinue}
        @{label=$Label;case=$case;repeat=$Repeat;sourceCommit=$SourceCommit;exe=$exe;exeSha256=(Get-FileHash -LiteralPath $exe).Hash;
          chart=$chart;chartSha256=(Get-FileHash -LiteralPath $chart).Hash;seed='0x20261024';start=30;duration=10;analyzeStart=31;analyzeEnd=39;
          boundaryOnly=[bool]$BoundaryOnly;directComposition=$dc;configSha256=(Get-FileHash -LiteralPath $config).Hash;
          scriptSha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash;utc=[DateTime]::UtcNow.ToString('o')
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out "$name.manifest.json") -Encoding utf8
        & $exe $chart (Join-Path $out "$name.csv") $mode 30 10 $fps $sdk 'no-quit' $inputMode 2>&1 |
            Tee-Object -FilePath (Join-Path $out "$name.log")
        if($LASTEXITCODE -ne 0){throw "Profile failed: $name"}
        Write-Output "Completed $name"
    }
}finally{$env:BMS_PLAYER_CONFIG_PATH=$oldConfig;$env:BMS_STUDY_BOUNDARY_ONLY=$oldBoundary}
