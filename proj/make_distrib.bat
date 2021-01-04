:: This script builds a distribution directory out of the Release_DLL configuration

@echo off
pushd %~dp0..

if not exist proj\x64\Release_DLL\cdi_sdk.dll (
   echo To make a distribution, build the Release_DLL configuration first
   goto end
)

:: create or recreate the target directory
if exist build\aws-cdi-sdk rmdir /s /q build\aws-cdi-sdk
mkdir build\aws-cdi-sdk

:: copy LICENSE and make a VERSION file out of the git branch/commit
copy LICENSE build\aws-cdi-sdk
if exist .git (
	echo|set /p="Branch " > build\aws-cdi-sdk\VERSION
	git rev-parse --abbrev-ref HEAD  >> build\aws-cdi-sdk\VERSION
	echo|set /p="Commit " >> build\aws-cdi-sdk\VERSION
	git rev-parse HEAD >> build\aws-cdi-sdk\VERSION
)

:: copy include directory
mkdir build\aws-cdi-sdk\include
xcopy /s include build\aws-cdi-sdk\include

:: copy import libraries/PDBs into a lib directory
mkdir build\aws-cdi-sdk\lib
copy proj\x64\Release_DLL\libfabric.lib build\aws-cdi-sdk\lib
copy proj\x64\Release_DLL\libfabric.pdb build\aws-cdi-sdk\lib
copy proj\x64\Release_DLL\cdi_sdk.lib build\aws-cdi-sdk\lib
copy proj\x64\Release_DLL\cdi_sdk.pdb build\aws-cdi-sdk\lib

:: copy required DLLs into a bin directory
mkdir build\aws-cdi-sdk\bin
copy proj\x64\Release_DLL\libfabric.dll build\aws-cdi-sdk\bin
copy proj\x64\Release_DLL\cdi_sdk.dll build\aws-cdi-sdk\bin
copy "C:\Program Files (x86)\aws-cpp-sdk-all\bin\aws-c-common.dll" build\aws-cdi-sdk\bin
copy "C:\Program Files (x86)\aws-cpp-sdk-all\bin\aws-c-event-stream.dll" build\aws-cdi-sdk\bin
copy "C:\Program Files (x86)\aws-cpp-sdk-all\bin\aws-checksums.dll" build\aws-cdi-sdk\bin
copy "C:\Program Files (x86)\aws-cpp-sdk-all\bin\aws-cpp-sdk-cdi.dll" build\aws-cdi-sdk\bin
copy "C:\Program Files (x86)\aws-cpp-sdk-all\bin\aws-cpp-sdk-core.dll" build\aws-cdi-sdk\bin
copy "C:\Program Files (x86)\aws-cpp-sdk-all\bin\aws-cpp-sdk-monitoring.dll" build\aws-cdi-sdk\bin

:: copy API documentation if built
if exist build\documentation\api (
  mkdir build\aws-cdi-sdk\documentation
  xcopy /s build\documentation\api build\aws-cdi-sdk\documentation
)

echo.
echo Copied binary distribution to build\aws-cdi-sdk

:end
popd
