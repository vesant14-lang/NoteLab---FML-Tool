@echo off
setlocal
call "%~dp0setup_msvc.bat" || exit /b 1
set "FML_NOTE_KIT=%~dp0"
if not exist "%FML_NOTE_KIT%build\app\NoteLab.exe" call "%FML_NOTE_KIT%build.bat" || exit /b 1
pushd "%FML_NOTE_KIT%build\app" || exit /b 1
cl /nologo /c /O2 /MT /EHsc /std:c++17 /utf-8 /W3 /bigobj /wd4996 /wd4267 /wd4244 /I"%FML_NOTE_KIT%third_party\SDL3-3.4.14\include" /I"%FML_NOTE_KIT%third_party\imgui" "%FML_NOTE_KIT%tests\PublicWorkflow.cpp" || exit /b 1
link /nologo PublicWorkflow.obj BlockCanvas.obj NoteLab.res NoteStyle.obj NoteStyleRead.obj NoteStyleCheck.obj NotePreview.obj NoteSongs.obj NoteTypes.obj NoteProject.obj NoteImage.obj NoteExport.obj NoteBlocks.obj NoteCode.obj NoteInstall.obj NoteCreate.obj NoteResources.obj Scene.obj AnimateAtlas.obj LegacyChart.obj CodenameChart.obj SongMeta.obj ChartExchange.obj SparrowAtlas.obj Vfs.obj Hash.obj pugixml.obj miniz.obj miniz_tdef.obj miniz_tinfl.obj miniz_zip.obj AudioEngine.obj stb_vorbis_impl.obj GlRenderer.obj ShaderLibrary.obj TextRaster.obj FxShaders.obj imgui.obj imgui_draw.obj imgui_tables.obj imgui_widgets.obj imgui_impl_sdl3.obj imgui_impl_opengl3.obj "%FML_NOTE_KIT%third_party\SDL3-3.4.14\lib\x64\SDL3.lib" opengl32.lib shell32.lib ole32.lib /OUT:PublicWorkflow.exe || exit /b 1
set "LOCALAPPDATA=%FML_NOTE_KIT%.qa\settings"
set "TEMP=%FML_NOTE_KIT%.qa\temp"
set "TMP=%FML_NOTE_KIT%.qa\temp"
if not exist "%TEMP%" mkdir "%TEMP%"
pushd "%FML_NOTE_KIT%" || exit /b 1
"%FML_NOTE_KIT%build\app\PublicWorkflow.exe"
exit /b %ERRORLEVEL%
