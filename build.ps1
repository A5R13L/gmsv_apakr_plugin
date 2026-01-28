param(
    [string]$Preset = "",
    [string]$TagVersion = ""
)

if ($Preset -eq "") {
    $Arch = $env:PROCESSOR_ARCHITECTURE

    if ($Arch -eq "AMD64" -or $Arch -eq "x64") {
        $Preset = "x86_64"
    }
    else {
        $Preset = "x86"
    }
}

if ($TagVersion -eq "") {
    $TagVersion = "unknown"
}

Invoke-WebRequest -Uri 'https://github.com/premake/premake-core/releases/download/v5.0.0-beta2/premake-5.0.0-beta2-windows.zip' -OutFile premake5.zip
Expand-Archive premake5.zip -DestinationPath premake5
premake5/premake5.exe vs2022 --tag_version=$TagVersion
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

if ($Preset -eq "x86_64") {
    .\vcpkg.exe install curl[ssl]:x64-windows-static
}
else {
    .\vcpkg.exe install curl[ssl]:x86-windows-static
}

.\vcpkg.exe integrate install
cd ..

if ($Preset -eq "x86_64") {
    cd projects/apakr_64/windows/vs2022
    msbuild apakr_64.sln /p:Configuration=releasewithsymbols /p:Platform=x64 /p:VcpkgTriplet=x64-windows-static
}
else {
    cd projects/apakr_32/windows/vs2022
    msbuild apakr_32.sln /p:Configuration=releasewithsymbols /p:VcpkgTriplet=x86-windows-static
}