@echo off

if exist "SDL2-2.26.1/" goto SkipSDL
echo Downloading SDL
call curl "https://www.libsdl.org/release/SDL2-devel-2.26.1-VC.zip" -o SDL2.zip
call tar -xf SDL2.zip
del SDL2.zip
pushd SDL2-2.26.1
ren include SDL2
popd
:SkipSDL
