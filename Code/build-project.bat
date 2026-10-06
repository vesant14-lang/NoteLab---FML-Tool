@echo off
setlocal
set "FML_NOTE_MODE=%~1"
if not defined FML_NOTE_MODE set "FML_NOTE_MODE=app"
if /I not "%FML_NOTE_MODE%"=="app" if /I not "%FML_NOTE_MODE%"=="check" (
    echo Usage: build.bat [app^|check] [output-folder]
    exit /b 2
)
for %%I in ("%~dp0.") do set "FML_NOTE_SOURCE=%%~fI"
set "FML_NOTE_OUT=%~2"
if not defined FML_NOTE_OUT set "FML_NOTE_OUT=%~dp0build"
for %%I in ("%FML_NOTE_OUT%") do set "FML_NOTE_OUT=%%~fI"
if not exist "%FML_NOTE_OUT%" mkdir "%FML_NOTE_OUT%"
call "%FML_NOTE_SOURCE%\setup_msvc.bat" || exit /b 1
pushd "%FML_NOTE_OUT%" || exit /b 1
set "FML_NOTE_FLAGS=/nologo /c /O2 /MT /EHsc /std:c++17 /utf-8 /W3 /wd4996 /wd4267 /wd4244 /wd4245 /wd4456 /wd4457 /wd4701 /bigobj"
for %%F in (miniz miniz_tdef miniz_tinfl miniz_zip) do (
    cl %FML_NOTE_FLAGS% /TC "%FML_NOTE_SOURCE%\third_party\miniz\%%F.c" || exit /b 1
)
cl %FML_NOTE_FLAGS% "%FML_NOTE_SOURCE%\third_party\pugixml.cpp" "%FML_NOTE_SOURCE%\support\core\Hash.cpp" "%FML_NOTE_SOURCE%\support\io\Vfs.cpp" || exit /b 1
for %%F in (SparrowAtlas AnimateAtlas LegacyChart CodenameChart SongMeta ChartExchange) do (
    cl %FML_NOTE_FLAGS% "%FML_NOTE_SOURCE%\support\formats\%%F.cpp" || exit /b 1
)
for %%F in (NoteStyle NoteStyleRead NoteStyleCheck NotePreview NoteSongs NoteTypes NoteProject NoteImage NoteExport NoteBlocks NoteCode NoteInstall NoteCreate NoteResources) do (
    cl %FML_NOTE_FLAGS% "%FML_NOTE_SOURCE%\core\%%F.cpp" || exit /b 1
)
cl %FML_NOTE_FLAGS% "%FML_NOTE_SOURCE%\support\runtime\Scene.cpp" || exit /b 1
set "FML_NOTE_CORE=NoteStyle.obj NoteStyleRead.obj NoteStyleCheck.obj NotePreview.obj NoteSongs.obj NoteTypes.obj NoteProject.obj NoteImage.obj NoteExport.obj NoteBlocks.obj NoteCode.obj NoteInstall.obj NoteCreate.obj NoteResources.obj Scene.obj AnimateAtlas.obj LegacyChart.obj CodenameChart.obj SongMeta.obj ChartExchange.obj SparrowAtlas.obj Vfs.obj Hash.obj pugixml.obj miniz.obj miniz_tdef.obj miniz_tinfl.obj miniz_zip.obj"
if /I "%FML_NOTE_MODE%"=="check" goto :check
cl %FML_NOTE_FLAGS% "%FML_NOTE_SOURCE%\support\audio\AudioEngine.cpp" || exit /b 1
cl %FML_NOTE_FLAGS% /TC "%FML_NOTE_SOURCE%\support\audio\stb_vorbis_impl.c" || exit /b 1
set "FML_NOTE_UI_FLAGS=%FML_NOTE_FLAGS% /I"%FML_NOTE_SOURCE%\third_party\SDL3-3.4.14\include" /I"%FML_NOTE_SOURCE%\third_party\imgui""
for %%F in (FxShaders GlRenderer ShaderLibrary TextRaster) do (
    cl %FML_NOTE_UI_FLAGS% "%FML_NOTE_SOURCE%\support\render\%%F.cpp" || exit /b 1
)
for %%F in (imgui imgui_draw imgui_tables imgui_widgets imgui_impl_sdl3 imgui_impl_opengl3) do (
    cl %FML_NOTE_UI_FLAGS% "%FML_NOTE_SOURCE%\third_party\imgui\%%F.cpp" || exit /b 1
)
cl %FML_NOTE_UI_FLAGS% "%~dp0src\main.cpp" "%~dp0src\BlockCanvas.cpp" || exit /b 1
rc /nologo /I"%FML_NOTE_SOURCE%\assets" /fo NoteLab.res "%~dp0resources.rc" || exit /b 1
link /nologo main.obj BlockCanvas.obj NoteLab.res %FML_NOTE_CORE% AudioEngine.obj stb_vorbis_impl.obj GlRenderer.obj ShaderLibrary.obj TextRaster.obj FxShaders.obj imgui.obj imgui_draw.obj imgui_tables.obj imgui_widgets.obj imgui_impl_sdl3.obj imgui_impl_opengl3.obj "%FML_NOTE_SOURCE%\third_party\SDL3-3.4.14\lib\x64\SDL3.lib" opengl32.lib shell32.lib ole32.lib /OUT:NoteLab.exe || exit /b 1
set "FML_NOTE_DLL=%FML_NOTE_SOURCE%\third_party\SDL3-3.4.14\lib\x64\SDL3.dll"
if not exist "%FML_NOTE_DLL%" set "FML_NOTE_DLL=%FML_NOTE_SOURCE%\build\SDL3.dll"
copy /Y "%FML_NOTE_DLL%" "%FML_NOTE_OUT%\SDL3.dll" >nul || exit /b 1
popd
exit /b 0

:check
cl %FML_NOTE_FLAGS% "%FML_NOTE_SOURCE%\tests\CoreRegression.cpp" || exit /b 1
link /nologo CoreRegression.obj %FML_NOTE_CORE% /OUT:NoteLabCoreTests.exe || exit /b 1
"%FML_NOTE_OUT%\NoteLabCoreTests.exe"
exit /b %ERRORLEVEL%
