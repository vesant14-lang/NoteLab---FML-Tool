param(
    [string]$Destination = (Join-Path (Split-Path $PSScriptRoot -Parent) 'Delivery'),
    [string]$Version = '1.0.3',
    [switch]$DeveloperOnly
)

$ErrorActionPreference = 'Stop'
if ($Version -notmatch '^\d+\.\d+\.\d+([-.][a-zA-Z0-9]+)*$') { throw 'Invalid release version.' }
$noteKitRoot = [IO.Path]::GetFullPath($PSScriptRoot)
& (Join-Path $noteKitRoot 'test-layout.ps1')
$noteReleaseRoot = [IO.Path]::GetFullPath($Destination)
if (Test-Path -LiteralPath $noteReleaseRoot) { throw 'Choose a new destination: existing releases are never overwritten.' }
if ($noteReleaseRoot.StartsWith($noteKitRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Place the release outside the source kit.' }

$noteFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$noteQueue = [Collections.Generic.Queue[string]]::new()
function Add-NoteFile([string]$Relative) {
    $noteSourceFile = [IO.Path]::GetFullPath((Join-Path $noteKitRoot $Relative))
    if (-not $noteSourceFile.StartsWith($noteKitRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Source escapes kit: $Relative" }
    if (-not (Test-Path -LiteralPath $noteSourceFile -PathType Leaf)) { throw "Missing required source: $Relative" }
    $noteRelativeFile = $noteSourceFile.Substring($noteKitRoot.Length + 1).Replace('\', '/')
    if ($noteFiles.Add($noteRelativeFile) -and $noteRelativeFile -match '\.(cpp|c|hpp|h)$') { $noteQueue.Enqueue($noteRelativeFile) }
}
function Add-NoteTree([string]$Relative) {
    $noteTree = Join-Path $noteKitRoot $Relative
    if (-not (Test-Path -LiteralPath $noteTree -PathType Container)) { throw "Missing required directory: $Relative" }
    foreach ($noteFile in Get-ChildItem -LiteralPath $noteTree -File -Recurse) {
        $noteRelative = $noteFile.FullName.Substring($noteKitRoot.Length + 1)
        # 1.0.3: the tutorial sounds are silenced and their files stay out of both packages.
        if ($noteRelative.Replace('\', '/') -like 'assets/sounds/*') { continue }
        Add-NoteFile $noteRelative
    }
}

foreach ($noteFile in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md','CONTRIBUTING.md','SECURITY.md','CHANGELOG.md','BITACORA.md','.gitignore','.gitattributes','setup_msvc.bat','build.bat','test.bat','test-workflow.bat','test-ui.ps1','test-layout.ps1','FML_SYNC_MAP.json','test-engines.ps1','prepare-game-mods.ps1','prepare-assets.ps1','capture-demo.ps1','package-release.ps1','build-project.bat','resources.rc')) { Add-NoteFile $noteFile }
foreach ($noteTree in @('assets','docs','licenses','tests','.github','src','third_party/imgui','third_party/miniz','third_party/SDL3-3.4.14/include')) { Add-NoteTree $noteTree }
foreach ($noteFile in @('third_party/json.hpp','third_party/miniaudio.h','third_party/pugixml.cpp','third_party/pugixml.hpp','third_party/pugiconfig.hpp','third_party/stb_image.h','third_party/stb_image_write.h','third_party/stb_vorbis.c','third_party/SDL3-3.4.14/lib/x64/SDL3.lib','third_party/SDL3-3.4.14/lib/x64/SDL3.dll','third_party/SDL3-3.4.14/LICENSE.txt','tests/CoreRegression.cpp','support/audio/stb_vorbis_impl.c','support/core/Hash.cpp','support/io/Vfs.cpp','support/runtime/Scene.cpp','support/audio/AudioEngine.cpp')) { Add-NoteFile $noteFile }
foreach ($noteModule in @('SparrowAtlas','AnimateAtlas','LegacyChart','CodenameChart','SongMeta','ChartExchange')) { Add-NoteFile "support/formats/$noteModule.cpp" }
foreach ($noteModule in @('NoteStyle','NoteStyleRead','NoteStyleCheck','NotePreview','NoteSongs','NoteTypes','NoteProject','NoteImage','NoteExport','NoteBlocks','NoteCode','NoteInstall','NoteCreate','NoteResources')) { Add-NoteFile "core/$noteModule.cpp" }
foreach ($noteModule in @('FxShaders','GlRenderer','ShaderLibrary','TextRaster')) { Add-NoteFile "support/render/$noteModule.cpp" }
while ($noteQueue.Count) {
    $noteRelativeFile = $noteQueue.Dequeue()
    $noteSourceFile = Join-Path $noteKitRoot $noteRelativeFile
    $noteText = [IO.File]::ReadAllText($noteSourceFile)
    foreach ($noteInclude in [regex]::Matches($noteText, '(?m)^\s*#\s*include\s*"([^"\r\n]+)"')) {
        $noteHeaderName = $noteInclude.Groups[1].Value
        $noteHeader = Join-Path (Split-Path $noteSourceFile -Parent) $noteHeaderName
        if (-not (Test-Path -LiteralPath $noteHeader -PathType Leaf)) { $noteHeader = Join-Path (Join-Path $noteKitRoot 'third_party/imgui') $noteHeaderName }
        if (Test-Path -LiteralPath $noteHeader -PathType Leaf) {
            $noteHeader = [IO.Path]::GetFullPath($noteHeader)
            Add-NoteFile $noteHeader.Substring($noteKitRoot.Length + 1)
        }
    }
}
foreach ($noteRequired in @('build/app/NoteLab.exe','build/app/SDL3.dll','docs/RELEASE_CHECKS.md','docs/images/welcome-en.png')) {
    if (-not (Test-Path -LiteralPath (Join-Path $noteKitRoot $noteRequired) -PathType Leaf)) { throw "Build/checks not ready: $noteRequired" }
}
$noteBuildVersion = [regex]::Match([IO.File]::ReadAllText((Join-Path $noteKitRoot 'src/BuildPolicy.hpp')), 'version\s*=\s*"([^"]+)"').Groups[1].Value
$noteBaseVersion = ($Version -split '[-]', 2)[0]
if ($noteBuildVersion -ne $noteBaseVersion) { throw "Source version $noteBuildVersion does not match requested release $Version." }
if (-not $DeveloperOnly) {
    $noteBinaryVersion = (Get-Item -LiteralPath (Join-Path $noteKitRoot 'build/app/NoteLab.exe')).VersionInfo.ProductVersion
    if ($noteBinaryVersion -ne $noteBaseVersion) { throw "Rebuild the application: binary version $noteBinaryVersion does not match release $Version." }
}

$notePublicRoot = Join-Path $noteReleaseRoot 'Public'
$noteDevRoot = Join-Path $noteReleaseRoot 'Developer'
New-Item -ItemType Directory -Path $noteDevRoot -ErrorAction Stop | Out-Null
if (-not $DeveloperOnly) { New-Item -ItemType Directory -Path $notePublicRoot -ErrorAction Stop | Out-Null }
function Copy-NoteFile([string]$Relative, [string]$TargetRoot, [string]$TargetName = '') {
    if (-not $TargetName) { $TargetName = $Relative }
    $noteTarget = Join-Path $TargetRoot $TargetName
    New-Item -ItemType Directory -Path (Split-Path $noteTarget -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $noteKitRoot $Relative) -Destination $noteTarget -ErrorAction Stop
}
foreach ($noteFile in ($noteFiles | Sort-Object)) { Copy-NoteFile $noteFile $noteDevRoot }
if (-not $DeveloperOnly) {
    Copy-NoteFile 'build/app/NoteLab.exe' $notePublicRoot 'NoteLab.exe'
    Copy-NoteFile 'build/app/SDL3.dll' $notePublicRoot 'SDL3.dll'
    foreach ($noteFile in @('README.md','LICENSE','THIRD_PARTY_NOTICES.md','CHANGELOG.md','BITACORA.md','CONTRIBUTING.md','SECURITY.md')) { Copy-NoteFile $noteFile $notePublicRoot }
    foreach ($noteFile in $noteFiles) {
        if ($noteFile -like 'licenses/*' -or $noteFile -like 'docs/*' -or $noteFile -like 'assets/*') { Copy-NoteFile $noteFile $notePublicRoot }
    }
}

function Write-NoteManifest([string]$TargetRoot, [string]$ManifestName) {
    $noteEntries = @(Get-ChildItem -LiteralPath $TargetRoot -File -Recurse | Sort-Object FullName | ForEach-Object {
        [ordered]@{ path = $_.FullName.Substring($TargetRoot.Length + 1).Replace('\','/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
    $noteManifest = [ordered]@{ product = 'Note Lab'; version = $Version; platform = 'Windows x64'; files = $noteEntries }
    [IO.File]::WriteAllText((Join-Path $TargetRoot $ManifestName), ($noteManifest | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
}
Write-NoteManifest $noteDevRoot 'SOURCE_MANIFEST.json'
if (-not $DeveloperOnly) { Write-NoteManifest $notePublicRoot 'FILES_SHA256.json' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$notePackages = [Collections.Generic.List[object]]::new()
$notePackages.Add(@('Developer',"NoteLab-$Version-Developer.zip"))
if (-not $DeveloperOnly) { $notePackages.Add(@('Public',"NoteLab-$Version-Windows-x64.zip")) }
foreach ($notePackage in $notePackages) {
    $noteArchivePath = Join-Path $noteReleaseRoot $notePackage[1]
    [IO.Compression.ZipFile]::CreateFromDirectory((Join-Path $noteReleaseRoot $notePackage[0]), $noteArchivePath, [IO.Compression.CompressionLevel]::Optimal, $false)
    $noteArchive = [IO.Compression.ZipFile]::OpenRead($noteArchivePath)
    try {
        foreach ($noteEntry in $noteArchive.Entries) {
            if ($noteEntry.FullName -match '(^|/)(\.qa|build|backups|settings|mods|sounds|tools)(/|$)|\.(obj|log|pdb|fmlnote|wav)$') { throw "Private/build file in release: $($noteEntry.FullName)" }
        }
    } finally { $noteArchive.Dispose() }
}
$noteHashes = Get-ChildItem -LiteralPath $noteReleaseRoot -Filter '*.zip' -File | Sort-Object Name | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $_.Name }
[IO.File]::WriteAllLines((Join-Path $noteReleaseRoot 'SHA256SUMS.txt'), [string[]]$noteHashes, [Text.UTF8Encoding]::new($false))
Write-Output "Release created: $noteReleaseRoot"
Get-ChildItem -LiteralPath $noteReleaseRoot -Filter '*.zip' -File | Select-Object Name,Length
