# Fetches the third-party sources the build needs and this repository does not carry.
#
# EA stripped them from the source release and they are not ours to commit, so every fresh clone
# has to get them once. build.bat runs this before it configures; running it again when everything
# is in place costs one directory check per library and nothing else.
#
#   -Force   re-fetch even what is already there
#
# What it cannot get is the game itself: the .big files from a Zero Hour install go next to
# generals.exe in GeneralsMD\Run, and the base game's in Run\ZH_Generals. The game says so on
# startup when they are missing.
[CmdletBinding()]
param([switch] $Force)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$codeRoot = Split-Path -Parent $PSScriptRoot
$libraries = Join-Path $codeRoot 'Libraries'
$runFolder = Join-Path (Split-Path -Parent $codeRoot) 'Run'
$work = Join-Path $env:TEMP 'zhr-vendor'

function Step($message) { Write-Host "[vendor] $message" }

function Get-File($url, $destination) {
  if (Test-Path $destination) { return $destination }
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
  Step "downloading $(Split-Path -Leaf $destination)"
  Invoke-WebRequest -Uri $url -OutFile $destination -UseBasicParsing
  return $destination
}

# Unpacks into a folder of its own and hands back whatever single directory the archive contained,
# which for a GitHub source zip is the repository at that commit.
function Expand-Source($archive, $name) {
  $target = Join-Path $work $name
  if (Test-Path $target) { Remove-Item -Recurse -Force $target }
  New-Item -ItemType Directory -Force -Path $target | Out-Null
  Step "unpacking $name"
  if ($archive.EndsWith('.tar.gz')) {
    tar -xf $archive -C $target
  } else {
    Expand-Archive -Path $archive -DestinationPath $target -Force
  }
  $entries = @(Get-ChildItem $target)
  if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) { return $entries[0].FullName }
  return $target
}

function Copy-Files($sourceFiles, $destination) {
  New-Item -ItemType Directory -Force -Path $destination | Out-Null
  foreach ($file in $sourceFiles) { Copy-Item $file.FullName (Join-Path $destination $file.Name) -Force }
}

# The files directly in a folder with one of these extensions. Get-ChildItem -Include is not that:
# without -Recurse it silently matches nothing, which is how the first run of this copied no zlib.
function Get-TopLevel($folder, [string[]] $extensions) {
  Get-ChildItem $folder -File | Where-Object { $extensions -contains $_.Extension }
}

# --- zlib 1.1.4, flat. maketree.c is a generator with its own main() and does not belong in the lib.
function Install-Zlib {
  $destination = Join-Path $libraries 'Source\Compression\ZLib'
  if ((Test-Path (Join-Path $destination 'deflate.c')) -and -not $Force) { return }
  $archive = Get-File 'https://zlib.net/fossils/zlib-1.1.4.tar.gz' (Join-Path $work 'zlib-1.1.4.tar.gz')
  $source = Expand-Source $archive 'zlib'
  Copy-Files (Get-TopLevel $source @('.c', '.h') | Where-Object { $_.Name -ne 'maketree.c' }) $destination
  Step "zlib 1.1.4 -> Libraries\Source\Compression\ZLib"
}

# --- LZH-Light 1.0. Lzhl_tcp.cpp is a socket layer nothing calls; Test.c has its own main().
function Install-Lzhl {
  $header = Join-Path $libraries 'Source\Compression\LZHCompress\CompLibHeader'
  $sourceFolder = Join-Path $libraries 'Source\Compression\LZHCompress\CompLibSource'
  if ((Test-Path (Join-Path $sourceFolder 'Lzhl.cpp')) -and -not $Force) { return }
  $archive = Get-File 'https://github.com/TheSuperHackers/lzhl-1.0/archive/dfd96e2.zip' (Join-Path $work 'lzhl.zip')
  $source = Expand-Source $archive 'lzhl'
  Copy-Files (Get-ChildItem $source -File -Filter *.h) $header
  Copy-Files (Get-TopLevel $source @('.cpp', '.tbl') |
    Where-Object { $_.Name -notin @('Lzhl_tcp.cpp', 'Test.c') }) $sourceFolder
  Step "LZH-Light 1.0 -> Libraries\Source\Compression\LZHCompress"
}

# --- The fork's one change to LZH-Light, Libraries\Source\lzhl-clear-history.patch: LZBuffer's history
# starts cleared (vendor.sh says why).  Windows compiles LZH-Light too, so it gets the same patch, and a
# copy fetched before the patch existed gets it on the next build.
function Install-LzhlPatch {
  $destination = Join-Path $libraries 'Source\Compression\LZHCompress\CompLibHeader'
  $header = Join-Path $destination '_lz.h'
  if (Select-String -LiteralPath $header -Pattern 'Zero Hour Reforged: altered' -SimpleMatch -Quiet) { return }
  # The patch with the header's own line endings: the fetched header is LF, and a checkout made before
  # .gitattributes pinned the patch to LF holds a CRLF copy of it (core.autocrlf), which matches no line.
  $text = [IO.File]::ReadAllText((Join-Path $libraries 'Source\lzhl-clear-history.patch')) -replace "`r`n", "`n"
  if ([IO.File]::ReadAllText($header).Contains("`r`n")) { $text = $text -replace "`n", "`r`n" }
  $patch = Join-Path $work 'lzhl-clear-history.patch'
  [IO.File]::WriteAllText($patch, $text)
  # without the ceiling git finds this checkout around the folder and skips the patch as outside it
  $env:GIT_CEILING_DIRECTORIES = Join-Path $libraries 'Source'
  try { git -C $destination -c core.autocrlf=false apply $patch }
  finally { Remove-Item Env:GIT_CEILING_DIRECTORIES }
  if (-not (Select-String -LiteralPath $header -Pattern 'Zero Hour Reforged: altered' -SimpleMatch -Quiet)) {
    throw "lzhl-clear-history.patch did not apply to Libraries\Source\Compression\LZHCompress"
  }
  Step "lzhl-clear-history.patch -> Libraries\Source\Compression\LZHCompress"
}

# --- DirectX 8 headers and import libraries. extra\ is not wholesale-copyable: basetsd.h, d3d.h,
# ddraw.h and dsound.h there shadow the modern Windows SDK and break winnt.h. Three files from it
# are needed, because ww3d2\pointgr.cpp includes D3DXMath.h.
function Install-DirectX {
  $include = Join-Path $libraries 'DirectX\Include'
  $lib = Join-Path $libraries 'DirectX\Lib'
  if ((Test-Path (Join-Path $include 'd3d8.h')) -and -not $Force) { return }
  $archive = Get-File 'https://github.com/TheSuperHackers/min-dx8-sdk/archive/7bddff8.zip' (Join-Path $work 'dx8.zip')
  $source = Expand-Source $archive 'dx8'
  Copy-Files (Get-TopLevel $source @('.h', '.inl')) $include
  Copy-Files (Get-ChildItem $source -File -Filter *.lib) $lib
  Copy-Files (Get-ChildItem (Join-Path $source 'extra') -File |
    Where-Object { $_.Name -in @('d3dxmath.h', 'd3dxmath.inl', 'd3dxerr.h') }) $include
  Step "min-dx8-sdk -> Libraries\DirectX"
}

# --- GameSpy SDK, whole repository. It brings its own CMakeLists, which CMakeLists.txt adds.
function Install-GameSpy {
  $destination = Join-Path $libraries 'Source\GameSpy'
  if ((Test-Path (Join-Path $destination 'CMakeLists.txt')) -and -not $Force) { return }
  $archive = Get-File 'https://github.com/TheSuperHackers/GamespySDK/archive/b1b77d8.zip' (Join-Path $work 'gamespy.zip')
  $source = Expand-Source $archive 'gamespy'
  # Every one of these folders holds a committed .gitignore that keeps the code out of the
  # repository. Emptying the folder first takes that with it, and then the whole SDK shows up as
  # untracked - which is how 780 files of third-party source nearly went into a commit.
  $keep = Join-Path $destination '.gitignore'
  $kept = if (Test-Path $keep) { Get-Content $keep -Raw } else { $null }
  if (Test-Path $destination) { Remove-Item -Recurse -Force $destination }
  New-Item -ItemType Directory -Force -Path $destination | Out-Null
  Copy-Item (Join-Path $source '*') $destination -Recurse -Force
  if ($null -ne $kept) { Set-Content -Path $keep -Value $kept -NoNewline }
  Step "GamespySDK -> Libraries\Source\GameSpy"
}

# --- litehtml 0.10, whole repository: the HTML and CSS layout engine behind the pages drawn over
# the battlefield. Its CMakeLists builds the bundled gumbo parser as well. Same .gitignore dance as
# GameSpy, for the same reason.
function Install-Litehtml {
  $destination = Join-Path $libraries 'Source\litehtml'
  if ((Test-Path (Join-Path $destination 'CMakeLists.txt')) -and -not $Force) { return }
  $archive = Get-File 'https://github.com/litehtml/litehtml/archive/9bc84b8b8d15a4e50f18b327aa30955048b441c2.zip' (Join-Path $work 'litehtml-0.10.zip')
  $source = Expand-Source $archive 'litehtml'
  $keep = Join-Path $destination '.gitignore'
  $kept = if (Test-Path $keep) { Get-Content $keep -Raw } else { $null }
  if (Test-Path $destination) { Remove-Item -Recurse -Force $destination }
  New-Item -ItemType Directory -Force -Path $destination | Out-Null
  Copy-Item (Join-Path $source '*') $destination -Recurse -Force
  if ($null -ne $kept) { Set-Content -Path $keep -Value $kept -NoNewline }
  Step "litehtml 0.10 -> Libraries\Source\litehtml"
}

# --- The fork's one change to litehtml, Libraries\Source\litehtml-parsed-css.patch: createFromString
# takes stylesheets the caller parsed once, which was two thirds of building a page. HtmlOverlay.cpp
# passes them and does not compile without it. A copy that has the patch says so by the parameter's
# name in document.h, so a copy fetched before the patch existed gets it on the next build too.
function Install-LitehtmlPatch {
  $destination = Join-Path $libraries 'Source\litehtml'
  $header = Join-Path $destination 'include\litehtml\document.h'
  if (Select-String -LiteralPath $header -Pattern 'master_parsed' -SimpleMatch -Quiet) { return }
  $patch = Join-Path $libraries 'Source\litehtml-parsed-css.patch'
  # without the ceiling git finds this checkout around the folder and skips every file in the patch
  # as outside it, and says nothing
  $env:GIT_CEILING_DIRECTORIES = Join-Path $libraries 'Source'
  try { git -C $destination -c core.autocrlf=false apply $patch }
  finally { Remove-Item Env:GIT_CEILING_DIRECTORIES }
  if ($LASTEXITCODE -ne 0) { throw "litehtml-parsed-css.patch did not apply to Libraries\Source\litehtml" }
  Step "litehtml-parsed-css.patch -> Libraries\Source\litehtml"
}

# --- nanosvg, the two headers: parses and rasterises the SVG pictures a page names in url(), which
# the game then draws pixel by pixel. Same .gitignore dance as litehtml.
function Install-Nanosvg {
  $destination = Join-Path $libraries 'Source\nanosvg'
  if ((Test-Path (Join-Path $destination 'nanosvgrast.h')) -and -not $Force) { return }
  $archive = Get-File 'https://github.com/memononen/nanosvg/archive/239e102ec2c691f2902e20ace2ed36ee4a35cfe6.zip' (Join-Path $work 'nanosvg.zip')
  $source = Expand-Source $archive 'nanosvg'
  Copy-Files (Get-TopLevel (Join-Path $source 'src') @('.h')) $destination
  Copy-Item (Join-Path $source 'LICENSE.txt') $destination -Force
  Step "nanosvg -> Libraries\Source\nanosvg"
}

# --- miniaudio 0.11.25, the one header and its one implementation file, the same pin as vendor.sh.
# Windows plays through XAudio2 but records -wav through miniaudio's mix (miles_dispatch.cpp), the
# one the logic clock can move. Copied file by file so the committed .gitignore is left alone.
function Install-Miniaudio {
  $destination = Join-Path $libraries 'Source\miniaudio'
  if ((Test-Path (Join-Path $destination 'miniaudio.c')) -and -not $Force) { return }
  $archive = Get-File 'https://github.com/mackron/miniaudio/archive/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d.zip' (Join-Path $work 'miniaudio-0.11.25.zip')
  $source = Expand-Source $archive 'miniaudio'
  foreach ($name in @('miniaudio.h', 'miniaudio.c', 'LICENSE')) {
    Copy-Item (Join-Path $source $name) (Join-Path $destination $name) -Force
  }
  Step "miniaudio 0.11.25 -> Libraries\Source\miniaudio"
}

# --- The fork's own upscaled art: every 3D texture at twice its size, and the ground. Not in git -
# ReforgedTextures.big alone is a gigabyte, ten times
# what GitHub takes in a file, and LFS in a fork is billed to the parent repository.
#
# It comes from the release channel, the same place a player's launcher takes it from, and the
# channel's own _versions.json carries the sha256 of every file in the newest release. Reading the
# hash from there rather than pinning it here means regenerating the art does not leave this script
# lying.
#
# Two places it can come from, in this order:
#
#   1. the release channel, if this checkout knows one. This repository is public and does not name
#      it: the address comes from ZHR_CHANNEL_URL, or from the launcher checkout beside this one.
#   2. this repository's own art release on GitHub, which is where anyone who just cloned the
#      public repository gets it. art.json there lists each file with its sha256, so the hashes are
#      not pinned in this script and regenerating the art does not leave it lying.
#
# With neither, the step is skipped: the game plays at the textures it shipped with, and
# experiments/doku-upscale is where the art is made.
#
# ReforgedNormals.big, the generated normal maps, is left out: the game stopped reading them on
# 2026-10-06, and a channel release from before that still lists the archive.
$artPattern = 'Reforged(?!Normals).*\.big$'
$artRelease = 'https://github.com/olcayseygan/CnCGeneralsZH-Reforged/releases/download/art-latest'

function Get-ChannelUrl {
  if ($env:ZHR_CHANNEL_URL) { return $env:ZHR_CHANNEL_URL.TrimEnd('/') + '/' }
  $launcher = Join-Path (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $codeRoot))) 'launcher\update.js'
  if (Test-Path $launcher) {
    $match = [regex]::Match((Get-Content $launcher -Raw), "CHANNEL_URL\s*=\s*'([^']+)'")
    if ($match.Success) { return $match.Groups[1].Value.TrimEnd('/') + '/' }
  }
  return $null
}

# Each source hands back the same shape: name, url, size and sha256 per file.
function Get-ArtFromChannel {
  $channelUrl = Get-ChannelUrl
  if (-not $channelUrl) { return @() }
  try {
    $versions = Invoke-RestMethod -Uri "${channelUrl}_versions.json" -UseBasicParsing
  } catch {
    Step 'the release channel is not reachable'
    return @()
  }
  $release = $versions.game | Select-Object -First 1
  @($release.files | Where-Object { $_.path -match $artPattern } | ForEach-Object {
    [pscustomobject]@{
      name   = Split-Path -Leaf $_.path
      url    = "$channelUrl$($release.folder)/$($_.path -replace '\\', '/')"
      size   = $_.size
      sha256 = $_.sha256
    }
  })
}

function Get-ArtFromRelease {
  try {
    $manifest = Invoke-RestMethod -Uri "$artRelease/art.json" -UseBasicParsing
  } catch {
    return @()
  }
  @($manifest.files | Where-Object { $_.name -match $artPattern } | ForEach-Object {
    [pscustomobject]@{
      name   = $_.name
      url    = "$artRelease/$($_.name)"
      size   = $_.size
      sha256 = $_.sha256
    }
  })
}

function Install-Art {
  $wanted = @(Get-ArtFromChannel)
  if ($wanted.Count -eq 0) { $wanted = @(Get-ArtFromRelease) }
  if ($wanted.Count -eq 0) {
    Step 'no upscaled art is published yet, so the game will use the textures it shipped with'
    return
  }

  New-Item -ItemType Directory -Force -Path $runFolder | Out-Null
  foreach ($file in $wanted) {
    $target = Join-Path $runFolder $file.name
    if ((Test-Path $target) -and -not $Force -and
        (Get-FileHash $target -Algorithm SHA256).Hash -eq $file.sha256.ToUpper()) {
      continue
    }
    $partial = "$target.part"
    Step "downloading $($file.name) ($([Math]::Round($file.size / 1MB)) MB)"
    Invoke-WebRequest -Uri $file.url -OutFile $partial -UseBasicParsing
    $hash = (Get-FileHash $partial -Algorithm SHA256).Hash
    if ($hash -ne $file.sha256.ToUpper()) {
      Remove-Item $partial -ErrorAction SilentlyContinue
      throw "$($file.name) downloaded with hash $hash, and it was published as $($file.sha256)"
    }
    Move-Item $partial $target -Force
    Step "$($file.name) -> Run"
  }
}

New-Item -ItemType Directory -Force -Path $work | Out-Null
Install-Zlib
Install-Lzhl
Install-LzhlPatch
Install-DirectX
Install-GameSpy
Install-Litehtml
Install-LitehtmlPatch
Install-Nanosvg
Install-Miniaudio
# SDL3 is the platform layer for everything that is not Windows (decision 3 in PORTING.md). Windows
# keeps Win32Device, so it is not fetched here; vendor.sh fetches it, and says it skips DirectX the
# same way. The same holds for SDL3's Metal patch, Libraries\Source\sdl3-metal-windowless.patch:
# vendor.sh applies it, and there is nothing here to apply.
Step 'skipping SDL3: not Windows, and vendor.sh is what fetches it'
# glslang, SPIRV-Cross and SDL_shadercross compile the shader generators' SDL3 GPU target (decision 4).
# Windows compiles the D3D11 target with d3dcompiler_47.dll, so they are not fetched here.
Step 'skipping glslang, SPIRV-Cross and SDL_shadercross: the SDL3 GPU shader path, vendor.sh fetches them'
# FreeType rasterises text off Windows (decision 6); Windows draws it with GDI, so it is not fetched here.
Step 'skipping FreeType: text off Windows, vendor.sh fetches it'
# FFmpeg's source is built only off Windows (V1); Windows links the committed dist/ that
# Tools/ffmpeg-build.sh made, so the tarball is not fetched here.
Step 'skipping the FFmpeg source: the POSIX movie decoder, vendor.sh fetches it'
Install-Art
Step 'everything the build needs is in place'
