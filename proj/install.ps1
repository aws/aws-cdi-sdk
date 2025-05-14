[cmdletbinding()] Param( [String]$defines )

if ($defines.length -ne 0) {
	Write-Host "Setting CL options: $Defines"
    $env:CL="$Defines"
}

$ErrorActionPreference="Stop"

Function Delete-Folder-If-Exists($FolderName) {
    if (Test-Path $FolderName) {
        Write-Host "Deleting existing folder $FolderName"
        Remove-Item $FolderName -Recurse -Force -Confirm:$false
    }
}

Function Rename-File($SourceName, $NewName) {
    $path=Split-Path -Path "$SourceName" -Parent
    if (-not(Test-Path "$path\$NewName")) {
        if (Test-Path $SourceName) {
            Write-Host "Renaming $SourceName to $NewName"
            Rename-Item "$SourceName" "$NewName"
        } else {
            Write-Host "Error: $SourceName does not exist"
        }
    }
}

# Define project folder. Note: Must be the current folder when this script is invoked.
$proj=$PWD.Path

# Define AWS CDI-SDK folder.
$cdisdk=$proj + "\.."

# Define root folder.
$root=$proj + "\..\.."

Write-Host "Downloading the aws-sdk-cpp..."
Push-Location "$root"
git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp
Pop-Location
Write-Host "done"

Write-Host "Copying aws-cpp-sdk-cdi files to aws-sdk-cpp..."
Copy-Item -Recurse -Force -Path $cdisdk\aws-cpp-sdk-cdi\ -Destination $root\aws-sdk-cpp\generated\src\
Write-Host "done"

Write-Host "Building the aws-sdk-cpp monitoring and cdi modules..."
Push-Location "$root\aws-sdk-cpp"
cmake . -D CMAKE_BUILD_TYPE=Release -D BUILD_ONLY="monitoring;cdi" -D ENABLE_TESTING="OFF" -D AUTORUN_UNIT_TESTS="OFF"
msbuild .\ALL_BUILD.vcxproj /p:Configuration=Release
msbuild .\INSTALL.vcxproj /p:Configuration=Release
Pop-Location
Write-Host "done"

Write-Host "Downloading libfabric, libfabric_new, PDCurses repositories..."
Push-Location "$root"
Delete-Folder-If-Exists "libfabric"
git clone --single-branch --branch v1.9.x-cdi https://github.com/aws/libfabric libfabric
if ($LASTEXITCODE -ne 0) {
	Pop-Location
	throw "git clone failed with exit code $LASTEXITCODE"
}
Delete-Folder-If-Exists "libfabric_new"
git clone --single-branch --branch v1.15.x https://github.com/ofiwg/libfabric libfabric_new
if ($LASTEXITCODE -ne 0) {
	Pop-Location
	throw "git clone failed with exit code $LASTEXITCODE"
}
Delete-Folder-If-Exists "PDCurses"
git clone --single-branch --branch 3.9 https://github.com/wmcbrine/PDCurses PDCurses
if ($LASTEXITCODE -ne 0) {
	Pop-Location
	throw "git clone failed with exit code $LASTEXITCODE"
}
Pop-Location
Write-Host "done"

Write-Host "Downloading latest EFADriver files..."
Invoke-WebRequest -Uri "https://ec2-windows-drivers-efa.s3-us-west-2.amazonaws.com/Latest/EFADriver.zip" -OutFile "$proj\EFADriver.zip"
Write-Host "done"

Delete-Folder-If-Exists "$proj\EFADriver"
Write-Host "Extracting EFADriver files."
Add-Type -A "System.IO.Compression.FileSystem"; [IO.Compression.ZipFile]::ExtractToDirectory("$proj\EFADriver.zip", "$proj\EFADriver");
Write-Host "done"

Write-Host "Invoking EFADriver installer"
Push-Location "EFADriver"; & ".\install.ps1"; Pop-Location
Write-Host "done"

$efaWinVersion="1.1.0"
$efaWin="efawin-$efaWinVersion"
Write-Host "Downloading efawin version ${efaWinVersion} files..."
Invoke-WebRequest -Uri "https://github.com/aws/efawin/archive/refs/tags/v${efaWinVersion}.zip" -OutFile "$proj\efawin.zip"
Write-Host "done"

Delete-Folder-If-Exists "$root\$efaWin"
Write-Host "Extracting efawin files."
Add-Type -A "System.IO.Compression.FileSystem"; [IO.Compression.ZipFile]::ExtractToDirectory("$proj\efawin.zip", "$root");
Write-Host "done"

Delete-Folder-If-Exists "$root\efawin"
Rename-File "$root\$efaWin" "efawin"
Write-Host "done"

Write-Host "Renaming libfabric_new solution and project files from libfabric... to libfabric_new..."
Rename-File "$root\libfabric_new\libfabric.sln" "libfabric_new.sln"
Rename-File "$root\libfabric_new\libfabric.vcxproj" "libfabric_new.vcxproj"
Rename-File "$root\libfabric_new\libfabric.vcxproj.filters" "libfabric_new.vcxproj.filters"
Write-Host "done"

Write-Host "Replacing libfabric.vcxproj with libfabric_new.vcxproj in libfabric_new\info.vcxproj"
(Get-Content -Path "$root\libfabric_new\info.vcxproj" -Raw) -replace "libfabric.vcxproj", "libfabric_new.vcxproj" | Set-Content -Path "$root\libfabric_new\info.vcxproj"
Write-Host "done"

Write-Host "Replacing libfabric.vcxproj with libfabric_new.vcxproj in libfabric_new\pingpong.vcxproj"
(Get-Content -Path "$root\libfabric_new\pingpong.vcxproj" -Raw) -replace "libfabric.vcxproj", "libfabric_new.vcxproj" | Set-Content -Path "$root\libfabric_new\pingpong.vcxproj"
Write-Host "done"

Write-Host "Running libfabric_new\.appveyor.ps1 installer script..."
Push-Location "$root\libfabric_new"
try {
    & ".\.appveyor.ps1"
} catch {
    Write-Host "An error occurred. Note: The appveyor.ps1 script must be run on a clean folder."
	Write-Host $_
}
Write-Host "done"
Pop-Location

Write-Host "Building Debug version of the aws-cdi-sdk binaries..."
Push-Location "$root"
try {
	# Can't simply launch MSBuild with the cdi_proj.sln file. It doesn't work due to different configurations in libfabric_new.
	# Must build everything separately. Need to duplicate folders used by the cdi_proj.sln file, since the projects will depend on them.
    cd "$root\libfabric_new"
    MSBuild.exe "libfabric_new.vcxproj" /p:Configuration=Debug-Efa-v142 /p:Platform=x64 /p:SolutionDir="$root\libfabric_new\" /p:OutDir="$proj\x64\Debug-Efa-v142\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "info.vcxproj" /p:Configuration=Debug-v142 /p:Platform=x64 /p:SolutionDir="$root\libfabric_new\" /p:ProjectConfig=Debug-v142 /p:OutDir="$proj\x64\Debug\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "pingpong.vcxproj" /p:Configuration=Debug-v142 /p:Platform=x64 /p:SolutionDir="$root\libfabric_new\" /p:ProjectConfig=Debug-v142 /p:OutDir="$proj\x64\Debug\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    cd "$root\libfabric"
    MSBuild.exe "libfabric.vcxproj" /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$root\libfabric\" /p:OutDir="$proj\x64\Debug\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    cd "$proj"
    MSBuild.exe "cdi_proj.sln" /t:libfabric /p:Configuration=Debug /p:Platform=x64
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "cdi_proj.sln" /t:efawin /p:Configuration=Debug /p:Platform=x64
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "cdi_proj.sln" /t:pdcurses /p:Configuration=Debug /p:Platform=x64
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "cdi_proj.sln" /t:cdi_libfabric_api /p:Configuration=Debug /p:Platform=x64
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "cdi_libfabric_new_api.vcxproj"  /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$proj\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "cdi_sdk.vcxproj" /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$proj\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "cdi_test.vcxproj" /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$proj\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "cdi_test_min_rx.vcxproj" /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$proj\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "cdi_test_min_tx.vcxproj" /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$proj\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "cdi_test_unit.vcxproj" /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$proj\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
    MSBuild.exe "dump_riff.vcxproj" /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir="$proj\"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }
} catch {
    Write-Host "An error occurred."
	Write-Host $_
}
Pop-Location
Write-Host "done"
