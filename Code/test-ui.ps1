param([int]$TimeoutSeconds = 45)
$ErrorActionPreference = 'Stop'
$noteUiRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$noteApp = Join-Path $noteUiRoot 'build/app/NoteLab.exe'
if (-not (Test-Path -LiteralPath $noteApp -PathType Leaf)) { throw 'Run build.bat first.' }
$noteFixture = Get-ChildItem -LiteralPath (Join-Path $noteUiRoot '.qa') -Directory -Filter 'workflow-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $noteFixture) { throw 'Run test-workflow.bat first to generate independent fixtures.' }
$noteOldLocal = $env:LOCALAPPDATA
$noteOldTemp = $env:TEMP
$noteOldTmp = $env:TMP
try {
    $env:LOCALAPPDATA = Join-Path $noteUiRoot '.qa/ui-settings'
    $env:TEMP = Join-Path $noteUiRoot '.qa/temp'
    $env:TMP = $env:TEMP
    New-Item -ItemType Directory -Path $env:LOCALAPPDATA,$env:TEMP -Force | Out-Null
    $noteChecks = 0
    foreach ($noteCase in @(@('code','heal,poison',0), @('blocks','hey',0), @('resources','heal,hey',0), @('resources-compact','heal,hey',0,'1024x768'), @('audition','heal,hey',0), @('audition-compact','heal,hey',0,'1024x768'), @('score','hey',0), @('tutorial','hey',0), @('notes','hey',0), @('code-empty','',1))) {
        $noteMode = if ($noteCase[0] -eq 'code-empty') { 'code' } elseif ($noteCase[0] -eq 'resources-compact') { 'resources' } elseif ($noteCase[0] -eq 'audition-compact') { 'audition' } else { $noteCase[0] }
        $noteSize = if ($noteCase.Count -gt 3) { $noteCase[3] } else { '1480x900' }
        $noteArgs = @('"--root=' + (Join-Path $noteFixture.FullName 'first') + '"','--no-auto-base','--lang=en',('--window=' + $noteSize),'--block-type=NoteLabQATest',('--ui-test=' + $noteMode))
        if ($noteCase[1]) { $noteArgs += '--presets=' + $noteCase[1] }
        $noteLog = Join-Path $noteUiRoot ('.qa/ui-' + $noteCase[0] + '.log')
        $noteErrorLog = Join-Path $noteUiRoot ('.qa/ui-' + $noteCase[0] + '-stderr.log')
        $noteProcess = Start-Process -FilePath $noteApp -ArgumentList $noteArgs -WorkingDirectory $noteUiRoot -WindowStyle Hidden -RedirectStandardOutput $noteLog -RedirectStandardError $noteErrorLog -PassThru
        # Windows PowerShell 5.1 only keeps ExitCode if the handle was opened while the process ran.
        $null = $noteProcess.Handle
        if (-not $noteProcess.WaitForExit($TimeoutSeconds * 1000)) {
            $noteProcess.Kill()
            throw "Native UI test timed out: $($noteCase[0]). Only this script's child process was stopped."
        }
        $noteProcess.WaitForExit()
        if ($noteCase[0] -ne 'code-empty') {
            Get-Content -LiteralPath $noteLog
            $noteChecks += @(Select-String -LiteralPath $noteLog -Pattern '^\[ok\]').Count
        }
        if ($noteProcess.ExitCode -ne $noteCase[2]) { throw "UI test $($noteCase[0]) failed: exit $($noteProcess.ExitCode). See $noteLog and $noteErrorLog." }
        if ($noteCase[0] -eq 'code-empty' -and -not ((Get-Content -LiteralPath $noteErrorLog -Raw) -like '*needs a seeded block program*')) { throw 'Missing-program guard was not exercised.' }
        if ($noteCase[0] -eq 'code-empty') { Write-Output '[ok] missing-program test is rejected cleanly, without a crash.' }
    }
    Write-Output "Native UI: $noteChecks interaction checks plus the missing-program guard passed."
} finally {
    $env:LOCALAPPDATA = $noteOldLocal
    $env:TEMP = $noteOldTemp
    $env:TMP = $noteOldTmp
}
