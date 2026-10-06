param(
    [Parameter(Mandatory)][string]$Codename,
    [Parameter(Mandatory)][string]$Psych,
    [Parameter(Mandatory)][string]$VSlice,
    [switch]$PrepareOnly
)
$ErrorActionPreference = 'Stop'
$noteTestRoot = Join-Path $PSScriptRoot ('.qa/engines-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
if (Test-Path -LiteralPath $noteTestRoot) { throw 'An isolated test destination already exists.' }
New-Item -ItemType Directory -Path $noteTestRoot | Out-Null
$noteEngines = @(
    @{Key='codename'; Root=$Codename; Exe='CodenameEngine.exe'; Env='NOTELAB_TEST_CODENAME'},
    @{Key='psych'; Root=$Psych; Exe='PsychEngine.exe'; Env='NOTELAB_TEST_PSYCH'},
    @{Key='vslice'; Root=$VSlice; Exe='Funkin.exe'; Env='NOTELAB_TEST_VSLICE'}
)
foreach ($noteEngine in $noteEngines) {
    $noteOriginalRoot = [IO.Path]::GetFullPath($noteEngine.Root)
    if (-not (Test-Path -LiteralPath (Join-Path $noteOriginalRoot $noteEngine.Exe) -PathType Leaf)) { throw "Missing engine executable: $noteOriginalRoot" }
    $noteClone = Join-Path $noteTestRoot $noteEngine.Key
    New-Item -ItemType Directory -Path $noteClone,(Join-Path $noteClone 'mods') | Out-Null
    foreach ($noteFile in Get-ChildItem -LiteralPath $noteOriginalRoot -File) {
        if ($noteFile.Extension -in '.exe','.dll','.ndll','.ico' -or $noteFile.Name -eq 'modsList.txt') {
            Copy-Item -LiteralPath $noteFile.FullName -Destination $noteClone
        }
    }
    foreach ($noteDirectory in 'assets','manifest','plugins','lua') {
        $noteOriginalDirectory = Join-Path $noteOriginalRoot $noteDirectory
        if (Test-Path -LiteralPath $noteOriginalDirectory -PathType Container) {
            Copy-Item -LiteralPath $noteOriginalDirectory -Destination $noteClone -Recurse
        }
    }
    [Environment]::SetEnvironmentVariable($noteEngine.Env, $noteClone, 'Process')
    $noteEngine['Clone'] = $noteClone
    Write-Output "Isolated engine ready: $($noteEngine.Key)"
}
[IO.File]::WriteAllText((Join-Path $noteTestRoot 'engines.json'), ($noteEngines | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
if (-not $PrepareOnly) {
    & (Join-Path $PSScriptRoot 'test-workflow.bat')
    if ($LASTEXITCODE) { throw "Workflow/matrix tests failed: $LASTEXITCODE" }
    $noteFixture = Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot '.qa') -Directory -Filter 'workflow-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $noteFixture) { throw 'The workflow did not produce fixtures.' }
    & (Join-Path $PSScriptRoot 'prepare-game-mods.ps1') -EngineCopies $noteTestRoot -Fixtures $noteFixture.FullName
    Write-Output 'Game copies are prepared, not certified. Open their executables and play the QA chart; record what actually ran.'
}
Write-Output "Engine test folder: $noteTestRoot"
