-- premake.lua
workspace "euler-rk-graph"
  configurations {"debug", "release"}
  architecture "x86_64"

  filter "configurations:debug"
    symbols "On"
    defines "DEBUG"

  filter "configurations:release"
    optimize "On"

  filter {}

project "euler-rk-graph"
  kind "ConsoleApp"
  language "C"

  targetdir "bin/%{cfg.buildcfg}"
  objdir "bin/%{cfg.buildcfg}/obj"

  files { "**.c", "**.h" }

  filter "not system:windows"
    links { "SDL2", "SDL2main", "m" }

  filter "system:windows"
    includedirs { "./SDL2-2.26.1/" }
    libdirs { "./SDL2-2.26.1/lib/x64" }
    links { "SDL2.lib", "SDL2main.lib" }
    prebuildcommands { ".\\get_sdl2.bat" }
    postbuildcommands { "copy .\\SDL2-2.26.1\\lib\\x64\\SDL2.dll bin\\%{cfg.buildcfg}\\" }

  filter {}
