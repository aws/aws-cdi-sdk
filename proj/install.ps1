[cmdletbinding()] Param()

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

$wd=$PWD.Path
$root=$wd + "\..\..";

Write-Host "Downloading latest EFADriver files..."
Invoke-WebRequest -Uri "https://ec2-windows-drivers-efa.s3-us-west-2.amazonaws.com/Latest/EFADriver.zip" -OutFile "EFADriver.zip"
Write-Host "done"

Delete-Folder-If-Exists "$wd\EFADriver"
Write-Host "Extracting EFADriver files."
Add-Type -A "System.IO.Compression.FileSystem"; [IO.Compression.ZipFile]::ExtractToDirectory("$wd\EFADriver.zip", "$wd\EFADriver");
Write-Host "done"

Write-Host "Invoking EFADriver installer"
Push-Location "$wd\EFADriver"; & ".\install.ps1"; Pop-Location
Write-Host "done"

$efaWinVersion="1.0.0"
$efaWin="efawin-$efaWinVersion"
Write-Host "Downloading efawin version ${efaWinVersion} files..."
Invoke-WebRequest -Uri "https://github.com/aws/efawin/archive/refs/tags/v${efaWinVersion}.zip" -OutFile "efawin.zip"
Write-Host "done"

Delete-Folder-If-Exists "$root\$efaWin"
Write-Host "Extracting efawin files."
Add-Type -A "System.IO.Compression.FileSystem"; [IO.Compression.ZipFile]::ExtractToDirectory("$wd\efawin.zip", "$root");
Write-Host "done"

Delete-Folder-If-Exists "$root\efawin"
Rename-File "$root\$efaWin" "efawin"
Write-Host "done"

Write-Host "Renaming libfabric_new solution and project files from libfabric... to libfabric_new..."
Rename-File "$root\libfabric_new\libfabric.sln" "libfabric_new.sln"
Rename-File "$root\libfabric_new\libfabric.vcxproj" "libfabric_new.vcxproj"
Rename-File "$root\libfabric_new\libfabric.vcxproj.filters" "libfabric_new.vcxproj.filters"
Write-Host "done"
