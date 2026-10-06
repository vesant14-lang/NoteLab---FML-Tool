$ErrorActionPreference = 'Stop'
$noteRepo = [IO.Path]::GetFullPath($PSScriptRoot)
foreach ($noteRequired in @('src/main.cpp','core/NoteStyle.cpp','core/NoteExport.cpp','core/NoteCreate.cpp','support/render/GlRenderer.cpp','build-project.bat','FML_SYNC_MAP.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $noteRepo $noteRequired) -PathType Leaf)) { throw "Missing Note Lab file: $noteRequired" }
}
foreach ($noteOtherTool in @('NoteLab','src/fml_app','src/fml_script','src/fml_modexplorer','src/fml_stagelab','script_host','ModExplorer','AtlasBuilder','StageLab','Importer')) {
    if (Test-Path -LiteralPath (Join-Path $noteRepo $noteOtherTool)) { throw "Another FML tool is present: $noteOtherTool" }
}
$noteModules = @('NoteStyle','NoteStyleRead','NoteStyleCheck','NotePreview','NoteSongs','NoteTypes','NoteProject','NoteImage','NoteExport','NoteBlocks','NoteCode','NoteInstall','NoteCreate','NoteResources')
foreach ($noteModule in $noteModules) {
    foreach ($noteExtension in @('cpp','hpp')) {
        if (-not (Test-Path -LiteralPath (Join-Path $noteRepo "core/$noteModule.$noteExtension"))) { throw "Missing note module: $noteModule.$noteExtension" }
    }
}
$noteUnits = @(foreach ($noteFolder in @('src','core','support','tests')) {
    Get-ChildItem -LiteralPath (Join-Path $noteRepo $noteFolder) -File -Recurse | Where-Object Extension -in @('.cpp','.c','.hpp','.h')
})
foreach ($noteUnit in $noteUnits) {
    foreach ($noteInclude in [regex]::Matches([IO.File]::ReadAllText($noteUnit.FullName), '(?m)^\s*#\s*include\s*"([^"\r\n]+)"')) {
        $noteHeader = [IO.Path]::GetFullPath((Join-Path $noteUnit.DirectoryName $noteInclude.Groups[1].Value))
        if (-not (Test-Path -LiteralPath $noteHeader -PathType Leaf)) { $noteHeader = Join-Path $noteRepo ('third_party/imgui/' + $noteInclude.Groups[1].Value) }
        $noteHeader = [IO.Path]::GetFullPath($noteHeader)
        if (-not $noteHeader.StartsWith($noteRepo + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Include requires an external checkout: $($noteUnit.Name)" }
        if (-not (Test-Path -LiteralPath $noteHeader -PathType Leaf)) { throw "Unresolved include in $($noteUnit.Name): $($noteInclude.Groups[1].Value)" }
    }
}
Write-Output "Note Lab-only layout: $($noteModules.Count) core modules, $($noteUnits.Count) source/header files; all quoted includes are self-contained."
