param([Parameter(Mandatory)][string]$EngineCopies, [Parameter(Mandatory)][string]$Fixtures)
$ErrorActionPreference = 'Stop'
$noteQaRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '.qa'))
$noteEngineCopies = [IO.Path]::GetFullPath($EngineCopies)
$noteFixtures = [IO.Path]::GetFullPath($Fixtures)
foreach ($notePath in $noteEngineCopies,$noteFixtures) {
    if (-not $notePath.StartsWith($noteQaRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Only isolated .qa folders may be used.' }
}
function Write-NoteQaJson([string]$Path, $Value) {
    New-Item -ItemType Directory -Path (Split-Path $Path -Parent) -Force | Out-Null
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 80), [Text.UTF8Encoding]::new($false))
}
$noteBasePsych = Join-Path $noteEngineCopies 'psych/assets/shared/data/bopeebo/bopeebo.json'
$notePsychChart = Get-Content -LiteralPath $noteBasePsych -Raw | ConvertFrom-Json
$noteCodeLines = @(
    @{type=0; position='dad'; characters=@('dad'); notes=[Collections.Generic.List[object]]::new()},
    @{type=1; position='boyfriend'; characters=@('bf'); notes=[Collections.Generic.List[object]]::new()},
    @{type=2; position='girlfriend'; characters=@('gf'); notes=@()}
)
$noteIndex = 0
foreach ($noteSection in $notePsychChart.song.notes) {
    $noteRows = [Collections.Generic.List[object]]::new()
    foreach ($noteRow in $noteSection.sectionNotes) {
        $notePlayer = [bool]$noteSection.mustHitSection
        if ([int]$noteRow[1] -ge 4) { $notePlayer = -not $notePlayer }
        $noteCustom = $notePlayer -and (($noteIndex % 3) -eq 0)
        $noteLane = [int]$noteRow[1] % 4
        $noteType = if ($noteCustom) { 'NoteLabQA' } else { '' }
        $noteRows.Add([object[]]@($noteRow[0],$noteRow[1],$noteRow[2],$noteType))
        $noteCodeLines[[int]$notePlayer].notes.Add(@{time=$noteRow[0]; id=$noteLane; sLen=$noteRow[2]; type=[int]$noteCustom})
        ++$noteIndex
    }
    $noteSection.sectionNotes = $noteRows.ToArray()
}
$notePsychChart.song | Add-Member -NotePropertyName disableNoteRGB -NotePropertyValue $true -Force
Write-NoteQaJson (Join-Path $noteFixtures 'engine-exports/psych/chart/data/bopeebo/bopeebo.json') $notePsychChart
Write-NoteQaJson (Join-Path $noteFixtures 'engine-exports/psych/chart/pack.json') @{name='Note Lab QA'; description='Generated test assets. Not for distribution.'; runsGlobally=$false; color=@(155,123,245)}
$noteWeek = @{songs=@(@('Bopeebo','dad',@(155,123,245))); weekCharacters=@('dad','bf','gf'); weekBackground=''; weekBefore=''; storyName='Note Lab QA'; weekName='Note Lab QA'; startUnlocked=$true; hideStoryMode=$false; hideFreeplay=$false; difficulties='Normal'}
Write-NoteQaJson (Join-Path $noteFixtures 'engine-exports/psych/chart/weeks/notelabqa.json') $noteWeek
$noteCodeChart = @{codenameChart=$true; scrollSpeed=1.0; noteTypes=@('NoteLabQA'); events=@(); stage='stage'; strumLines=$noteCodeLines}
Write-NoteQaJson (Join-Path $noteFixtures 'engine-exports/codename/chart/songs/bopeebo/charts/normal.json') $noteCodeChart
Write-NoteQaJson (Join-Path $noteFixtures 'engine-exports/codename/chart/songs/bopeebo/meta.json') @{name='Bopeebo'; displayName='Bopeebo - Note Lab QA'; bpm=$notePsychChart.song.bpm; difficulties=@('normal'); needsVoices=$true; color='#9b7bf5'}
Write-NoteQaJson (Join-Path $noteFixtures 'engine-exports/codename/chart/pack.json') @{name='Note Lab QA'; description='Generated test assets. Not for distribution.'}
$noteCodeAudio = Join-Path $noteFixtures 'engine-exports/codename/chart/songs/bopeebo/song'
New-Item -ItemType Directory -Path $noteCodeAudio -Force | Out-Null
foreach ($noteTrack in 'Inst.ogg','Voices.ogg') {
    $noteTrackSource = Join-Path $noteEngineCopies ('psych/assets/songs/bopeebo/' + $noteTrack)
    if (Test-Path -LiteralPath $noteTrackSource) { Copy-Item -LiteralPath $noteTrackSource -Destination $noteCodeAudio }
}
$noteVSliceChart = Get-Content -LiteralPath (Join-Path $noteEngineCopies 'vslice/assets/data/songs/bopeebo/bopeebo-chart.json') -Raw | ConvertFrom-Json
$noteIndex = 0
foreach ($noteRow in $noteVSliceChart.notes.normal) {
    if ([int]$noteRow.d -lt 4 -and (($noteIndex % 3) -eq 0)) { $noteRow | Add-Member -NotePropertyName k -NotePropertyValue 'NoteLabQA' -Force }
    ++$noteIndex
}
Write-NoteQaJson (Join-Path $noteFixtures 'engine-exports/vslice/chart/data/songs/bopeebo/bopeebo-chart.json') $noteVSliceChart
$noteVSliceMeta = Get-Content -LiteralPath (Join-Path $noteEngineCopies 'vslice/assets/data/songs/bopeebo/bopeebo-metadata.json') -Raw | ConvertFrom-Json
$noteVSliceMeta.playData.noteStyle = 'matrix'
Write-NoteQaJson (Join-Path $noteFixtures 'engine-exports/vslice/chart/data/songs/bopeebo/bopeebo-metadata.json') $noteVSliceMeta
Write-NoteQaJson (Join-Path $noteFixtures 'engine-exports/vslice/chart/_polymod_meta.json') @{title='Note Lab QA'; description='Generated engine test; never distribute game assets.'; author='Note Lab'; api_version='0.8.0'; mod_version='1.0.0'; license='MIT'}
foreach ($noteEngine in 'codename','psych','vslice') {
    $noteInstall = Join-Path $noteEngineCopies ($noteEngine + '/mods/NoteLabQA')
    New-Item -ItemType Directory -Path $noteInstall -Force | Out-Null
    foreach ($noteRole in 'skin','type','chart') {
        $noteExport = Join-Path $noteFixtures ('engine-exports/' + $noteEngine + '/' + $noteRole)
        if (-not (Test-Path -LiteralPath $noteExport)) { throw "Required QA package absent: $noteExport" }
        foreach ($noteItem in Get-ChildItem -LiteralPath $noteExport) { Copy-Item -LiteralPath $noteItem.FullName -Destination $noteInstall -Recurse -Force }
    }
    Write-Output "QA skin, custom-note script and chart installed in $noteInstall"
}
Write-Output 'Only isolated copies changed. Original installations and releases are untouched.'
