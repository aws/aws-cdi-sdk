:: This script builds the CDI SDK documentation.
:: It takes a single argument, either 'api' or 'all', and builds either just the API
:: documentation or all documentation for the SDK and test applications, respectively.

@ECHO OFF
IF "%1"=="all" (
    ECHO Building all documentation.
    SET doxygen_exclude=aws-cpp-sdk-cdi src\common\src\os_windows.c
    SET doxygen_enabled_sections=ALL_DOCS
    SET doc_dir=%__CD__%..\build\documentation\all
    GOTO Continue
)
IF "%1"=="api" (
    ECHO Building only API documentation.
    SET doxygen_exclude=aws-cpp-sdk-cdi src\common\src\os_windows.c src
    SET doxygen_enabled_sections=
    SET doc_dir=%__CD__%..\build\documentation\api
    GOTO Continue
)

:: We got here if the argument was either empty or unknown, so print help.
ECHO.
ECHO    %0 : A script to build CDI SDK Documentation
ECHO.
ECHO    Usage: %0 [api ^| all]
ECHO     - The argument value of 'api' builds only the API documentation.
ECHO     - The argument value of 'all' builds the API documentation as well as all SDK code and test application code documentation.
ECHO.
EXIT /B 1

:Continue

:: If the output directory does not yet exist, then make it.
IF NOT EXIST %doc_dir% (
    mkdir %doc_dir%
)

:: We need to run Doxygen from the project root, so jump there, run it, and come back.
PUSHD ..
doxygen.exe doc\Doxyfile
POPD

SET out_file=%doc_dir%\html\index.html

ECHO.
ECHO Read generated documentation at at: %out_file%.
