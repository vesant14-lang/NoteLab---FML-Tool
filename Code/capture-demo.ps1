param([Parameter(Mandatory)][string]$BaseGame, [int]$TimeoutSeconds = 30)
$ErrorActionPreference = 'Stop'
$noteApp = Join-Path $PSScriptRoot 'build/app/NoteLab.exe'
$notePictures = Join-Path $PSScriptRoot 'docs/images'
$noteDemoStorage = Join-Path $PSScriptRoot '.qa/demo-settings'
New-Item -ItemType Directory -Path $noteDemoStorage,$notePictures -Force | Out-Null
$noteDemoCases = @(
    @{Name='welcome-en'; Args=@('--lang=en','--window=1480x900')},
    @{Name='welcome-compact-es'; Args=@('--lang=es','--window=1024x768')},
    @{Name='base-preview-en'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--song=bopeebo','--difficulty=normal','--seek=18000','--lang=en','--window=1480x900')},
    @{Name='base-assets-en'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--assets','--asset-part=0','--lang=en','--window=1480x900')},
    @{Name='code-editor-en'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--blocks','--block-type=NoteLabDemo','--presets=heal,hey','--blocks-view=both','--lang=en','--window=1480x900')},
    @{Name='blocks-resources-es'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--block-resources','--block-type=NoteLabDemo','--presets=heal,hey','--blocks-view=both','--lang=es','--window=1480x900')},
    @{Name='blocks-resources-compact-en'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--block-resources','--block-type=NoteLabDemo','--presets=heal,hey','--blocks-view=both','--lang=en','--window=1024x768')},
    @{Name='blocks-in-use-base-en'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--block-type=NoteLabDemo','--presets=heal,hey','--ui-test=resources','--lang=en','--window=1480x900')},
    @{Name='combine-styles-en'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--combine-styles','--lang=en','--window=1480x900')},
    @{Name='preview-compact-es'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--song=bopeebo','--difficulty=normal','--seek=18000','--lang=es','--window=1024x768')},
    @{Name='resources-base-es'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--resources','--lang=es','--window=1480x900')},
    @{Name='resource-image-base-es'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--mod-resources=images/ui/popup/funkin/sick.png','--lang=es','--window=1480x900')},
    @{Name='resource-sound-base-es'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--mod-resources=shared/sounds/missnote1.ogg','--lang=es','--window=1480x900')},
    @{Name='creator-base-es'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--custom-create',('--custom-files=' + (Join-Path $BaseGame 'assets/images/ui/popup/funkin/sick.png')),'--lang=es','--window=1480x900')},
    @{Name='export-base-es'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--export','--lang=es','--window=1480x900')},
    @{Name='new-note-base-es'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--new-note','--new-note-name=Mi nota','--lang=es','--window=1480x900')},
    @{Name='sounds-songs-base-es'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--mod-resources=assets/songs/bopeebo/Inst.ogg','--sound-filter=2','--lang=es','--window=1480x900')},
    @{Name='script-to-blocks-base-en'; Args=@(('--root=' + $BaseGame),'--select=vslice:notestyle/funkin','--block-type=NoteLabDemo','--presets=heal,hey','--ui-test=notes','--lang=en','--window=1600x900')}
)
$noteOldLocal = $env:LOCALAPPDATA
$noteOldTemp = $env:TEMP
$noteOldTmp = $env:TMP
try {
    $env:LOCALAPPDATA = $noteDemoStorage
    $env:TEMP = Join-Path $PSScriptRoot '.qa/temp'; $env:TMP = $env:TEMP
    foreach ($noteDemo in $noteDemoCases) {
        $noteArgs = @('--no-auto-base','--capture-frames=60',('--capture=' + (Join-Path $notePictures ($noteDemo.Name + '.png')))) + $noteDemo.Args
        $noteQuoted = $noteArgs | ForEach-Object { '"' + $_ + '"' }
        $noteLog = Join-Path $noteDemoStorage ($noteDemo.Name + '.log')
        $noteErrorLog = Join-Path $noteDemoStorage ($noteDemo.Name + '-stderr.log')
        $noteChild = Start-Process -FilePath $noteApp -ArgumentList $noteQuoted -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -RedirectStandardOutput $noteLog -RedirectStandardError $noteErrorLog -PassThru
        # Windows PowerShell 5.1 only keeps ExitCode if the handle was opened while the process ran.
        $null = $noteChild.Handle
        if (-not $noteChild.WaitForExit($TimeoutSeconds * 1000)) { $noteChild.Kill(); throw "Own demo process timed out: $($noteDemo.Name)" }
        $noteChild.WaitForExit()
        if ($noteChild.ExitCode) { throw "Native capture failed: $($noteDemo.Name)" }
        Write-Output "Native capture: $($noteDemo.Name)"
    }
} finally { $env:LOCALAPPDATA = $noteOldLocal; $env:TEMP = $noteOldTemp; $env:TMP = $noteOldTmp }
