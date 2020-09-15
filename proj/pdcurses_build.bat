@ECHO off
REM Command line arguments:
REM  1= build target (ie. all or clean)
REM  2= Final folder for pdcurses.lib (if building it)
REM NOTE: Always build the release version of this library.

SET TARGET="%~1"
SET OUT_DIR="%~2"

pushd ..\..\PDCurses
mkdir build 2> NUL
cd build
ECHO Building PDCurses\build\curses.lib
nmake %TARGET% -f ..\wincon\Makefile.vc
popd

IF "%OUT_DIR%"=="" GOTO done
IF EXIST ..\..\PDCurses\build\pdcurses.lib (
  ECHO Copying PDCurses\build\pdcurses.lib to %OUT_DIR%
  copy ..\..\PDCurses\build\pdcurses.lib %OUT_DIR%
)
:done
@ECHO on
