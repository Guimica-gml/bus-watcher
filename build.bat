@echo off
if not exist build (mkdir build)

set CFLAGS=/W2 /DEBUG /std:c11
set CLIBS=

cl.exe %CFLAGS% /c /Fo:build\main.obj src\main.c %CLIBS% && ^
cl.exe %CFLAGS% /c /Fo:build\utils.obj src\utils.c %CLIBS% && ^
cl.exe %CFLAGS% /c /Fo:build\json.obj src\json.c %CLIBS% && ^
link.exe /out:build\main.exe build\main.obj build\utils.obj build\json.obj Ws2_32.lib
