# Builds the plugin with CMake + vcpkg, then copies the DLL and the ini into package\ (the folder layout a mod
# manager expects). See BUILDING.txt.
#
# Where the two things it needs are (first that exists wins):
#   CommonLibSSE-NG:  -CommonLib, the COMMONLIB_DIR environment variable, extern\CommonLibSSE-NG here, the author's checkout
#   vcpkg:            -Vcpkg, the SPL_VCPKG_ROOT environment variable, extern\vcpkg here, the author's checkout
param(
    [string]$Config = 'Release',
    [string]$CommonLib = $env:COMMONLIB_DIR,
    [string]$Vcpkg = $env:SPL_VCPKG_ROOT,
    [string]$BuildDir = (Join-Path $env:LOCALAPPDATA 'spl-build')
)

# Native tools write progress to stderr; that must not turn into an error.
$ErrorActionPreference = 'Continue'

$root = $PSScriptRoot
$author = 'C:\Users\badmi\Documents\Skyrim Multiclass Mod\tools'

function Find-Folder([string]$given, [string[]]$candidates, [string]$what) {
    foreach ($c in @($given) + $candidates) {
        if ($c -and (Test-Path -LiteralPath $c)) { return (Resolve-Path -LiteralPath $c).Path }
    }
    Write-Host "Could not find $what. Pass it as a parameter or see BUILDING.txt."
    exit 2
}
$commonLib = Find-Folder $CommonLib @((Join-Path $root 'extern\CommonLibSSE-NG'), (Join-Path $author 'ref-CommonLibSSE-NG')) 'CommonLibSSE-NG'
$vcpkg = Find-Folder $Vcpkg @((Join-Path $root 'extern\vcpkg'), (Join-Path $author 'vcpkg')) 'vcpkg'

# CMake from Visual Studio when it is there, otherwise the one on PATH.
$vsCmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$cmakeDir = if (Test-Path $vsCmake) { "$vsCmake;" } else { '' }

# On the author's machine MSYS2, MinGW, devkitPro and Cygwin on PATH break CMake, so they are left out for this process only.
$clean = ($env:Path -split ';' | Where-Object { $_ -and $_ -notmatch 'msys|mingw|devkit|cygwin' }) -join ';'
$env:Path = "$cmakeDir$clean"
$env:VCPKG_ROOT = $vcpkg
$env:VCPKG_DISABLE_METRICS = '1'

# Short path on purpose: MSBuild's file tracker fails once the build folder is nested deep inside a long source path.
$bld = $BuildDir
$triplet = 'x64-windows-static-md'

# CMake refuses a build folder whose cache was made for another source folder (after the project is moved).
# Dropping only the cache lets it configure again while the compiled objects, the slow part, are kept.
$cache = Join-Path $bld 'CMakeCache.txt'
if (Test-Path $cache) {
    $line = Select-String -Path $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$' | Select-Object -First 1
    if ($line) {
        $cachedSource = $line.Matches[0].Groups[1].Value.Replace('/', '\').TrimEnd('\')
        if ($cachedSource -ne $root.TrimEnd('\')) {
            Write-Host "Build folder was made for $cachedSource; configuring again for $root"
            Remove-Item $cache -Force
        }
    }
}

# Configuring (CMake plus the vcpkg dependency check) is most of the build time and only matters when
# CMakeLists.txt or vcpkg.json changed since the last configure; editing .cpp files does not need it.
$needConfigure = -not (Test-Path $cache)
if (-not $needConfigure) {
    $configured = (Get-Item $cache).LastWriteTime
    foreach ($f in 'CMakeLists.txt', 'vcpkg.json') {
        if ((Get-Item (Join-Path $root $f)).LastWriteTime -gt $configured) { $needConfigure = $true }
    }
}
if ($needConfigure) {
    cmake -S $root -B $bld -G 'Visual Studio 17 2022' -A x64 `
      "-DCMAKE_TOOLCHAIN_FILE=$vcpkg\scripts\buildsystems\vcpkg.cmake" `
      "-DVCPKG_TARGET_TRIPLET=$triplet" "-DVCPKG_HOST_TRIPLET=$triplet" `
      "-DCOMMONLIB_DIR=$commonLib" `
      -DUSE_VCPKG=ON
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} else {
    Write-Host 'Project unchanged since the last configure, skipping it.'
}

cmake --build $bld --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$out = Join-Path $root 'package\SKSE\Plugins'
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item (Join-Path $bld "$Config\SpellLeveling.dll") $out -Force
# The ini is next to this script in the project folder; in the release archive (this script is in
# SKSE\Plugins\Skyrim Leveling\src) it is already in SKSE\Plugins, two levels up.
$ini = @('SpellLeveling.ini', '..\..\SpellLeveling.ini') | ForEach-Object { Join-Path $root $_ } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $ini) {
    Write-Host 'Could not find SpellLeveling.ini (next to build.ps1, or two folders up).'
    exit 2
}
Copy-Item $ini $out -Force
Write-Host "Packaged to $out"
exit 0
