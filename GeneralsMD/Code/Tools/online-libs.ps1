# Builds the libraries internet play links: libcurl (Schannel, WebSockets) and GameNetworkingSockets
# (native ICE) with what they pull in, static, into Libraries\Source\online\vcpkg_installed.
#
# Not part of build.bat. It costs twenty minutes the first time and the game builds without it: CMake
# compiles the online code only when this folder is there (ZH_ONLINE in CMakeLists.txt). Windows x64
# only, because the GameNetworkingSockets port does not support ARM64.
#
# The vcpkg checkout goes outside the tree and is pinned, so two machines get the same libraries.
#   -WorkRoot   where the checkout and its build trees go (default %TEMP%\zhr-vcpkg)
[CmdletBinding()]
param([string] $WorkRoot = (Join-Path $env:TEMP 'zhr-vcpkg'))

$ErrorActionPreference = 'Stop'

$commit = 'd186940b2f0c4e8e5b4ff804af57d2a26fbfa091'
$codeRoot = Split-Path -Parent $PSScriptRoot
$manifestRoot = Join-Path $codeRoot 'Libraries\Source\online'
$vcpkg = Join-Path $WorkRoot 'vcpkg'

if (-not (Test-Path (Join-Path $vcpkg '.git'))) {
  New-Item -ItemType Directory -Force -Path $vcpkg | Out-Null
  git -C $vcpkg init -q
  git -C $vcpkg remote add origin https://github.com/microsoft/vcpkg.git
}
git -C $vcpkg fetch -q --depth 1 origin $commit
git -C $vcpkg checkout -q FETCH_HEAD
if ($LASTEXITCODE -ne 0) { throw 'vcpkg checkout failed' }

$env:VCPKG_DISABLE_METRICS = '1'
& (Join-Path $vcpkg 'bootstrap-vcpkg.bat') -disableMetrics
if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed' }

& (Join-Path $vcpkg 'vcpkg.exe') install `
  --triplet x64-windows-static `
  --x-manifest-root=$manifestRoot `
  "--x-install-root=$manifestRoot\vcpkg_installed" `
  "--x-buildtrees-root=$WorkRoot\b" `
  "--x-packages-root=$WorkRoot\p" `
  "--downloads-root=$WorkRoot\d" `
  --clean-after-build
if ($LASTEXITCODE -ne 0) { throw 'vcpkg install failed' }
