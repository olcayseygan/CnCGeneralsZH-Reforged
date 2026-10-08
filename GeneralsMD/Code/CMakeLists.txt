cmake_minimum_required(VERSION 3.20)

# P2: the oldest macOS the app runs on.  It has to be set before project(), and every target and
# vendored library built here inherits it (SDL3, freetype, glslang and the rest through add_subdirectory;
# FFmpeg through FFMPEG_POSIX_CFLAGS below).  Unset, the tools stamp the build machine's own version into
# every binary and the app refuses every Mac not on the newest macOS.  13.0 (Ventura): every Apple
# silicon Mac and Intel Macs from 2017 on run it, and nothing in the tree needs a newer one.  An explicit -DCMAKE_OSX_DEPLOYMENT_TARGET wins;
# an empty one (as every earlier build cache holds) takes the default.
if(CMAKE_HOST_APPLE AND "${CMAKE_OSX_DEPLOYMENT_TARGET}" STREQUAL "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "The oldest macOS the build runs on (P2)" FORCE)
endif()

project(GeneralsMD CXX C)

# Which operating system this build is for.  MSVC used to answer that question too, because for
# twenty years there was only one answer, and this file used the two interchangeably from its first
# line to its last.  They are different questions now.  Everything that is really about the
# platform - the CRT, the Windows SDK import libraries, MIDL, DirectX, the window - asks
# ZH_PLATFORM_WINDOWS; only a flag written in one compiler's own syntax asks which compiler is
# reading it.  PORTING.md has the rest of the design.
# Windows against POSIX, not Windows against macOS.  The port targets macOS first but is meant to
# reach Linux and other POSIX systems, so "is this not Windows" is ZH_PLATFORM_POSIX, and the two
# refinements below are for the few things that genuinely differ between POSIX systems (a window,
# audio, CPU detection) - never for "is this not Windows".  An unported platform-specific file fails
# with an #error naming what it lacks (cpudetect.cpp does this), which is louder and more useful than
# refusing to configure.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(ZH_PLATFORM_WINDOWS TRUE)
else()
  set(ZH_PLATFORM_POSIX TRUE)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(ZH_PLATFORM_MACOS TRUE)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ZH_PLATFORM_LINUX TRUE)
  endif()
endif()

# The other half of the split.  A target that only Windows can build is gated out below; a target
# whose sources do not compile under clang *yet* is a different thing, and this is it.  Those stay
# defined on macOS, so that tasks B1-B6 have something to drive with --target and see real errors
# from, but out of the default build, so that a Mac build failure means a regression in what was
# working rather than the next task's work showing through.  Take a name off the list as its task
# lands.  On Windows this expands to no argument at all and changes no target.
if(ZH_PLATFORM_POSIX)
  set(ZH_NOT_YET_PORTABLE EXCLUDE_FROM_ALL)
else()
  set(ZH_NOT_YET_PORTABLE "")
endif()

# A compiler may assume a member function's `this` is never null and delete the checks written on it.
# EA's code depends on three: MemoryPoolObject::deleteInstance() (GameMemory.h:762, `if (this)`, called
# on null pointers all over the engine), StateMachine::deleteInstance() (StateMachine.h:356) and
# WindowLayout::destroyWindows() (WindowLayout.cpp:176, `if (this == NULL)`).  GCC 6 and later delete
# them at -O2 and up - the native Linux build segfaulted in ScriptEngine::init (W1) - and GCC's release
# notes name this switch as the way to keep such code working.  Clang keeps them today (measured: the
# Release build's out-of-line deleteInstance and destroyWindows both begin with a null test, `cbz x0`)
# but warns that it may assume otherwise, so every POSIX compiler gets the switch.  This is what MSVC
# does already: it never made the assumption.
if(ZH_PLATFORM_POSIX AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-fno-delete-null-pointer-checks)
endif()

# Linux links with LLD when it is there, else mold.  The engine's static libraries reference each other in
# cycles (gameengine, ww3d2, wwsaveload, w3ddevice ...), which GNU ld resolves only in link order, so
# generals failed to link with hundreds of undefined references; Apple's linker, LLD and mold resolve
# archives in any order.  mold is for the Steam Runtime's SDK (P3's portable build), which has it and no
# LLD.  With neither, GNU ld is left as it was and the cycles still need resolving (W1 records it).
if(ZH_PLATFORM_LINUX)
  find_program(ZH_LLD NAMES ld.lld)
  find_program(ZH_MOLD NAMES ld.mold mold)
  if(ZH_LLD)
    add_link_options(-fuse-ld=lld)
  elseif(ZH_MOLD)
    add_link_options(-fuse-ld=mold)
  else()
    message(WARNING "No ld.lld or mold: GNU ld cannot resolve the engine's circular static libraries in link order")
  endif()
endif()

# compile_commands.json: every source's exact compile command, which Tools/syntax_sweep.py and
# clangd read.  Ninja can produce it on demand; a Makefiles tree has nothing else that carries each
# target's own -D flags.  POSIX only because only the POSIX tooling reads it - include_case_check
# among them - and it changes no target and no command line.
if(ZH_PLATFORM_POSIX)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# The zip goes to machines that have never installed a Visual Studio redistributable, and
# generals.exe imported MSVCP140.dll and VCRUNTIME140.dll by name.  That fails in the Windows
# loader before a line of our code runs, so there is no DebugLogFile.txt and no crash file to
# send back - the same undiagnosable failure the retail Miles and Bink DLLs are packaged to
# avoid.  Link the CRT into the exe and into d3d8.dll instead of depending on a machine having
# it.  CMAKE_POLICY_DEFAULT is what reaches the vendored subdirectories: d3d8to9 declares
# cmake_minimum_required(3.12), which predates CMP0091, and would otherwise ignore the setting
# below and keep importing the DLL.
# Both of these are Windows concerns and both are read later than this - CMAKE_MSVC_RUNTIME_LIBRARY
# when a target is created, CMAKE_POLICY_DEFAULT_CMP0091 when a vendored subdirectory is added - so
# the guard costs the Windows build nothing.
if(ZH_PLATFORM_WINDOWS)
  set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# x64 only.  The 32-bit build was dropped once this one was proven against it: same simulation to
# the bit, same picture to within sub-pixel sampling, and sound and video of its own that do not
# depend on the two 32-bit DLLs the game shipped with.  Nothing here is written for x86 any more.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Configure for x64: cmake -A x64")
endif()

set(WW ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/WWVegas)
set(COMP ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/Compression)

# Libraries/Include holds the handful of headers that belong to no library and are wanted by all
# of them: Lib/BaseType.h, Lib/Trig.h and, since B2, Lib/Clock.h.  Thirteen targets read the clock
# - the five WWVegas libraries, the four dx11 ones and three of their tests - and naming each of
# them here is a list that goes stale the first time somebody adds a target and forgets.
#
# This is a directory-wide include path and nothing else.  The warning further down about not
# defining things globally is about compile *definitions*, where a mismatch between libraries is a
# link error or a differently-shaped struct; an extra /I carries no such risk, and the three
# headers behind it are included by name and by nobody who does not want them.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)

# The third-party sources EA stripped out, which this repository does not carry either.  build.bat
# fetches them before it configures; say so here rather than let the generator report a missing
# source file per library.
set(VENDORED
    ${COMP}/ZLib/deflate.c
    ${COMP}/LZHCompress/CompLibSource/Lzhl.cpp
    # litehtml and nanosvg are common rather than Windows-only: gameengine links litehtml and
    # compiles nanosvg into HtmlOverlay.cpp, and gameengine is what the macOS build is for.
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/litehtml/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/nanosvg/nanosvgrast.h
    # The GameSpy SDK is built everywhere since B6 (see "gamespy" below).
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/GameSpy/CMakeLists.txt)
# The DirectX 8 headers are only read by targets this file does not define on macOS, and A2 decides
# what a Mac fetch pulls down.  Demanding them here would stop a Mac configure over files nothing
# portable includes.
if(ZH_PLATFORM_WINDOWS)
  list(APPEND VENDORED
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/DirectX/Include/d3d8.h)
  set(VENDOR_HINT "Run Tools\\vendor.ps1 (build.bat does it for you).")
else()
  # SDL3 and miniaudio are the platform layer everywhere but Windows (decision 3).
  list(APPEND VENDORED
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/SDL3/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/miniaudio/miniaudio.c)
  set(VENDOR_HINT "Run Tools/vendor.sh (build-macos.sh and build-linux.sh do it for you).")
endif()
foreach(vendored ${VENDORED})
  if(NOT EXISTS ${vendored})
    message(FATAL_ERROR "${vendored} is missing. ${VENDOR_HINT}")
  endif()
endforeach()
# SDL3's Metal patch (Libraries/Source/sdl3-metal-windowless.patch, decision 10).  A tree fetched before
# it and never given a vendor.sh run since still builds, and -offscreen then fails on a host with no
# window server with SDL's own "no supported SDL_GPU backend", which names nothing.  Said here instead.
# Apple only: the patch is to the Metal backend.
if(APPLE)
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/SDL3/src/gpu/metal/SDL_gpu_metal.m _zh_sdl3_patched
       REGEX "ZH_SDL_GPU_METAL_WINDOWLESS")
  if(NOT _zh_sdl3_patched)
    message(FATAL_ERROR "Libraries/Source/SDL3 lacks sdl3-metal-windowless.patch. ${VENDOR_HINT}")
  endif()
endif()

# LZH-Light's one patch (Libraries/Source/lzhl-clear-history.patch), on every platform: vendor.sh and
# vendor.ps1 both apply it.  A tree fetched before it builds without it and compresses from whatever the
# allocator left in the history buffer, so it is said here instead.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/Compression/LZHCompress/CompLibHeader/_lz.h _zh_lzhl_patched
     REGEX "Zero Hour Reforged: altered")
if(NOT _zh_lzhl_patched)
  message(FATAL_ERROR "LZH-Light lacks lzhl-clear-history.patch. ${VENDOR_HINT}")
endif()

# The _CRT_ pair only tells MSVC's own headers to stop deprecating the C library this code is
# written against.  They mean nothing to any other compiler and cost nothing there, so they stay
# unconditional.  WIN32 and _WINDOWS are statements about the operating system, and most of the
# tree reads them as exactly that, so they move into the Windows branch: defining them on a Mac
# would tell several hundred #ifdefs the wrong thing.  A few places read WIN32 to mean "not
# 16-bit" instead; those are source fixes for tracks B and C, not a reason to define it here.
add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
if(ZH_PLATFORM_WINDOWS)
  add_compile_definitions(WIN32 _WINDOWS)
endif()

# EA's own switch for exactly this situation ("Good for startup problems", Debug.h):
# it turns DEBUG_LOG back on in a Release build so the hundreds of existing log
# calls in GameEngine::init land in Run/generals.exe-<machine>.log.  Without it a
# quiet failure in init leaves nothing behind at all - GameEngine::init's
# catch (ErrorCode) swallows everything that is not ERROR_INVALID_D3D, and WinMain's
# outer catch (...) swallows the rest, so the exe just returns 0.  Applied per target
# at the bottom of this file, not globally: WWVegas' wwdebug.h includes GameEngine's
# Debug.h and routes WWDEBUG_SAY into DEBUG_LOG, so a global define makes wwmath and
# friends call DebugLog(), which only exists in gameengine - every leaf test exe then
# fails to link.  The WW libraries' own chatter is not what we are after anyway.
option(RELEASE_DEBUG_LOGGING "Write the startup debug log in Release builds" ON)
if(MSVC)
  # /permissive- is off on purpose: this is 2003-era code and templates here rely on
  # pre-two-phase-lookup name binding (simplevec.h et al).
  # /EHa, not /EHsc: WinMain installs _set_se_translator(DumpExceptionInfo) to turn an
  # access violation into a C++ exception and write ReleaseCrashInfo.txt on the way.  A
  # translator only runs in code compiled with asynchronous EH, so under /EHsc the hook
  # is dead (MSVC says so: "warning C4535: _set_se_translator() requires /EHa") and a
  # faulting frame just kills the process with 0xC0000005 and no log at all.
  add_compile_options(/W3 /permissive /EHa)
  # There is no debugger on the porting machine, so ReleaseCrashInfo.txt's stack
  # dump is the only stack we get.  StackDump.cpp walks the EBP chain by hand and
  # /O2 implies /Oy (no frame pointer), which leaves that dump empty; /Zi + /DEBUG
  # then give the frames names instead of bare addresses.  Costs a PDB, buys Phase 5.
  # /FS goes with /Zi: MSBuild compiles a project's sources in parallel and they all write
  # the one PDB.  Without it the writes go through mspdbsrv.exe, and if that dies or is
  # killed - which happens when a build is interrupted - every following compile fails with
  # C1041, or worse, only some of them do and the library is left half-built against an old
  # header.  /FS serializes the writes in the compilers themselves instead.
  add_compile_options(/Oy- /Zi /FS)
  # /MP: build a project's own sources in parallel too, not just one project at a
  # time.  MSBuild already runs projects concurrently, but gameengine alone is ~580
  # sources in a single project and was the long pole.  /FS above is what makes this
  # safe - the PDB writes are serialized in the compilers.
  add_compile_options(/MP)
  # There is no /arch:SSE2 here and none is needed: x64 has no x87 code generation at all, so
  # every float is computed at its declared width and every machine gets the same bits.  That is
  # what the 32-bit build had to ask for explicitly, and why it pinned the x87 precision control.
  # See dettrig.h and FPUControl.h.
  add_link_options(/DEBUG)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  # Keyed on the COMPILER, not the OS.  These flags are what a GCC or Clang build needs to compute
  # the simulation the way MSVC does, wherever it runs.  It used to read elseif(ZH_PLATFORM_MACOS), so
  # a Linux build would have configured cleanly with none of them - and GCC's default is
  # -ffp-contract=fast, and plain char is UNSIGNED on aarch64 Linux.  Both silent desyncs.
  # The clang half of the same decisions.  None of the switches above transfers: /EHa is there for
  # _set_se_translator, which is a Windows structured-exception mechanism; /Oy- and /Zi are there
  # for StackDump.cpp's hand-written EBP walk, which no arm64 build has ever had; /FS and /MP are
  # about one MSBuild project writing one PDB.  Those are Windows reasons and they do not cross.
  # What is left is the short list of places where clang's defaults differ from cl.exe's in a way
  # this code notices.
  #
  # -fexceptions: the engine throws from the bottom and catches at the top - GameEngine::init's
  # catch (ErrorCode) and WinMain's outer catch (...) are structural, not decoration.  clang turns
  # them on for C++ by itself; saying so keeps a -fno-exceptions in some future preset from
  # deleting the error handling without a diagnostic.
  add_compile_options(-fexceptions)
  # -ffp-contract=off: the most important line in this file, and not optional.  clang on arm64
  # contracts a*b+c into one fused multiply-add, which rounds once where two operations round
  # twice.  MSVC on x64 never does it.  The simulation is full of multiply-adds, so a single
  # contracted instruction inside it makes this build compute different numbers from the Windows
  # build - and that surfaces as a replay that diverges and a network game that desyncs, not as a
  # compile error.  dettrig.h went to integer arithmetic over a committed table for exactly this
  # reason; this is the other half of the same promise, and E1 is the test that holds it.
  add_compile_options(-ffp-contract=off)
  # -fno-strict-aliasing: 2003 C++.  The .big and .w3d readers cast a byte buffer to a struct
  # pointer and read through it, which the standard does not allow and an optimiser is entitled to
  # act on.  MSVC does no type-based alias analysis, so this code has never been compiled by
  # anything that would.
  add_compile_options(-fno-strict-aliasing)
  # -fsigned-char: MSVC's plain char is signed and code here compares one against zero.  Darwin on
  # arm64 already signs it, unlike the ARM EABI, so this pins a default rather than changing one -
  # which is the point, because the default is the kind of thing that is cheap to state and
  # expensive to discover.
  add_compile_options(-fsigned-char)
  # -Wno-unknown-pragmas: the tree's #pragma warning lines (67, B4) are MSVC's diagnostics controls,
  # and clang has none of those warnings to control.  Off now so that a future -Wall is usable rather
  # than 19,000 lines of them.  B4 checked every pragma for meaning first: none that clang ignores
  # changes code.
  add_compile_options(-Wno-unknown-pragmas)
  # -g for the reason /Zi is there: there is no debugger on the porting machine, so a symbolised
  # stack is the only stack anyone gets (atos reads the frames C5's crash handler writes).  No
  # -fno-omit-frame-pointer: Darwin's ABI keeps frame pointers, which the crash handler walks from the
  # signal's context, and on Linux it takes backtrace(), which unwinds by the .eh_frame -fexceptions
  # keeps, not by frame pointers.
  add_compile_options(-g)
  # ZH_SANITIZE: a sanitizer build, for finding bugs, never for playing, timing or E1 evidence.  A list
  # for -fsanitize= (address, thread, undefined, or e.g. address,undefined).  Empty by default, and then
  # nothing here applies: a normal build's command lines are what they were.  Set, it adds the flags
  # everywhere and defines ZH_SANITIZER_BUILD, under which GameMemory's global new and delete take
  # zeroed blocks from calloc and give them to free, which the sanitizer intercepts, rather than from
  # its own allocator (a system framework, Apple's Metal driver, otherwise mixes the two; GameMemory.cpp
  # says why).  Not for MSVC.
  set(ZH_SANITIZE "" CACHE STRING "-fsanitize= list for a sanitizer build (empty: a normal build)")
  if(ZH_SANITIZE)
    add_compile_options(-fsanitize=${ZH_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${ZH_SANITIZE})
    add_compile_definitions(ZH_SANITIZER_BUILD)
    message(STATUS "ZH_SANITIZE=${ZH_SANITIZE}: a sanitizer build; GameMemory's global operators use calloc and free")
  endif()
else()
  message(FATAL_ERROR
    "No determinism flags are set for compiler '${CMAKE_CXX_COMPILER_ID}'.  The simulation must be "
    "compiled without floating-point contraction and with a signed char, or it desyncs; add this "
    "compiler's spelling of -ffp-contract=off and -fsigned-char before building with it.")
endif()

# ---- wwdebug --------------------------------------------------------------
# Libraries/Include is on the WWVegas targets from B3 onward: Platform/MSVCCompat.h lives
# there, always.h includes it, and every WWVegas library reaches always.h.  Nothing in that
# directory can shadow a WWVegas header - its only headers are under Lib/, rts/ and Platform/.
add_library(wwdebug STATIC
  ${WW}/WWDebug/wwdebug.cpp
  ${WW}/WWDebug/wwmemlog.cpp
  ${WW}/WWDebug/wwprofile.cpp)
# GameEngine/Include for Common/EarlyCommandLine.h alone (-headless, which no box may wait on), as debuglib has it.
target_include_directories(wwdebug PUBLIC ${WW}/WWDebug
  PRIVATE ${WW}/WWLib ${WW}/Wwutil ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
          ${CMAKE_CURRENT_SOURCE_DIR}/GameEngine/Include)

# ---- wwutil ---------------------------------------------------------------
add_library(wwutil STATIC
  ${WW}/Wwutil/mathutil.cpp
  ${WW}/Wwutil/miscutil.cpp)
target_include_directories(wwutil PUBLIC ${WW}/Wwutil
  PRIVATE ${WW}/WWLib ${WW}/WWMath ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)
target_link_libraries(wwutil PRIVATE wwdebug)

# ---- wwmath ---------------------------------------------------------------
add_library(wwmath STATIC
  ${WW}/WWMath/aabox.cpp          ${WW}/WWMath/aabtreecull.cpp
  ${WW}/WWMath/cardinalspline.cpp ${WW}/WWMath/catmullromspline.cpp
  ${WW}/WWMath/colmath.cpp        ${WW}/WWMath/colmathaabox.cpp
  ${WW}/WWMath/colmathaabtri.cpp  ${WW}/WWMath/colmathfrustum.cpp
  ${WW}/WWMath/colmathline.cpp    ${WW}/WWMath/colmathobbobb.cpp
  ${WW}/WWMath/colmathobbox.cpp   ${WW}/WWMath/colmathobbtri.cpp
  ${WW}/WWMath/colmathplane.cpp   ${WW}/WWMath/colmathsphere.cpp
  ${WW}/WWMath/cullsys.cpp        ${WW}/WWMath/curve.cpp
  ${WW}/WWMath/dettrig.cpp
  ${WW}/WWMath/euler.cpp          ${WW}/WWMath/frustum.cpp
  ${WW}/WWMath/gridcull.cpp       ${WW}/WWMath/hermitespline.cpp
  ${WW}/WWMath/lineseg.cpp        ${WW}/WWMath/lookuptable.cpp
  ${WW}/WWMath/matrix3.cpp        ${WW}/WWMath/matrix3d.cpp
  ${WW}/WWMath/matrix4.cpp        ${WW}/WWMath/obbox.cpp
  ${WW}/WWMath/ode.cpp            ${WW}/WWMath/pot.cpp
  ${WW}/WWMath/quat.cpp           ${WW}/WWMath/tcbspline.cpp
  ${WW}/WWMath/tri.cpp            ${WW}/WWMath/v3_rnd.cpp
  ${WW}/WWMath/vehiclecurve.cpp   ${WW}/WWMath/vp.cpp
  ${WW}/WWMath/wwmath.cpp)
target_include_directories(wwmath PUBLIC ${WW}/WWMath
  PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)
target_link_libraries(wwmath PRIVATE wwdebug)

# ---- compression ----------------------------------------------------------
# zlib 1.1.4 (vendored, per README) minus maketree.c - that one is a table
# generator with its own main().  Lzhl_tcp.cpp is also skipped (socket layer,
# nothing in the game calls it).
add_library(compression STATIC
  ${COMP}/CompressionManager.cpp
  ${COMP}/EAC/btreeabout.cpp ${COMP}/EAC/btreedecode.cpp ${COMP}/EAC/btreeencode.cpp
  ${COMP}/EAC/huffabout.cpp  ${COMP}/EAC/huffdecode.cpp  ${COMP}/EAC/huffencode.cpp
  ${COMP}/EAC/refabout.cpp   ${COMP}/EAC/refdecode.cpp   ${COMP}/EAC/refencode.cpp
  ${COMP}/LZHCompress/NoxCompress.cpp
  ${COMP}/LZHCompress/CompLibSource/Huff.cpp
  ${COMP}/LZHCompress/CompLibSource/Lz.cpp
  ${COMP}/LZHCompress/CompLibSource/Lzhl.cpp
  ${COMP}/ZLib/adler32.c  ${COMP}/ZLib/compress.c ${COMP}/ZLib/crc32.c
  ${COMP}/ZLib/deflate.c  ${COMP}/ZLib/gzio.c     ${COMP}/ZLib/infblock.c
  ${COMP}/ZLib/infcodes.c ${COMP}/ZLib/inffast.c  ${COMP}/ZLib/inflate.c
  ${COMP}/ZLib/inftrees.c ${COMP}/ZLib/infutil.c  ${COMP}/ZLib/trees.c
  ${COMP}/ZLib/uncompr.c  ${COMP}/ZLib/zutil.c)
target_include_directories(compression
  PUBLIC ${COMP} ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
  PRIVATE ${COMP}/LZHCompress/CompLibHeader)
# CompressionManager calls z_compress2/z_uncompress and BaseType.h owns 'Byte',
# so zlib must be built with its Z_PREFIX renames on.
target_compile_definitions(compression PRIVATE Z_PREFIX)
# LZH-Light's _lzhl.h says `#define UINT32 unsigned long`, which is 32 bits under Windows' LLP64
# and 64 under Darwin's LP64.  LZHASH and LZPOS are both UINT32, so at 64 bits the hash index runs
# off the end of its table and LZHLCompressor::compress walks into unmapped memory - a SIGBUS on the
# first buffer it is handed.  The header asks first (#ifndef UINT32), so saying it here fixes the
# width without patching a vendored file that A2 re-fetches.  These widths reach the compressed
# bytes that save games and network packets are made of, so this is a parity fix, not a tidy-up.
#
# Not on Windows, and the reason is not caution.  UINT32 and UINT16 are Windows SDK *typedefs* -
# basetsd.h has `typedef unsigned int UINT32, *PUINT32;` - and _lzhl.h's guard tests for a *macro*,
# so on Windows the guard is true either way and the header already defines `unsigned long`, which
# is already 32 bits there.  The define would buy nothing and cost something: NoxCompress.cpp
# includes Lib/BaseType.h, which includes <windef.h>, so a command-line UINT32 macro is live when
# the SDK's typedef is preprocessed and turns it into `typedef unsigned int unsigned int, *PUINT32;`.
# clang only warns at that and limps on; MSVC is stricter about duplicate declaration specifiers and
# neither machine here can tell us how much stricter.  Found in review before it was merged.
if(ZH_PLATFORM_POSIX)
  target_compile_definitions(compression PRIVATE UINT32=unsigned\ int UINT16=unsigned\ short)
endif()

# Round-trip check over every codec CompressionManager exposes.
add_executable(compression_selfcheck ${COMP}/test_compression.cpp)
target_link_libraries(compression_selfcheck PRIVATE compression)

# DetRound::To_Long has to round half to even on every architecture, because its callers were written
# against an instruction that does.  Those callers are WW3D2's animation (hrawanim's floor idiom is
# arithmetic built on round-to-nearest) and WWMath - not GameLogic, which has none; an earlier
# version of this comment said "the simulation's callers" and was wrong.  Lib/DetRound.h is
# header-only and this links nothing, so it runs before any library compiles.  It checks DetRound
# against a hand-computed tie table on ONE architecture; it is not cross-architecture evidence.
# E3 (arch_differential) is.  See Tests/test_detround.cpp.
add_executable(detround_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_detround.cpp)
target_include_directories(detround_selfcheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)

# d3dxportable.h reproduces d3dx9_43.dll's D3DXVec4Transform bit for bit, because DumbProjectile-
# Behavior flies a shell along it and the flight is in the CRC.  Its expected values are outputs of
# Microsoft's own machine code (Tests/d3dx_golden.h), so this is not a self-consistency check.
# Header-only and links nothing, like detround_selfcheck.  See Tests/test_d3dxportable.cpp.
add_executable(d3dxportable_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_d3dxportable.cpp)
target_include_directories(d3dxportable_selfcheck PRIVATE ${WW}/WW3D2 ${CMAKE_CURRENT_SOURCE_DIR}/Tests)

# Guards the hand-rolled Matrix3D::Get_Inverse that replaced D3DXMatrixInverse.  It checks that the
# inverse is right, including one exact answer; it is NOT determinism evidence, and its header says
# why.  E3 is the cross-architecture evidence for DetTrig.
#
# wwlib and wwsaveload on every platform.  test_inverse.cpp needs only Matrix3D, but the link does
# not stop there: quat.cpp needs _FastAcosTable, which pulls in wwmath.cpp, and wwmath.cpp's
# WWMath::Init and Do_Force_Links reach LookupTableMgrClass (wwlib's file factory, strings, chunk
# I/O) and every spline's persist factory (wwsaveload).  ld -why_load shows the chain.  A macOS line
# with wwdebug alone was here once, on the belief that wwmath resolved against it; it had never been
# linked, and it does not.  So on macOS this waits for wwlib and wwsaveload to build.
add_executable(wwmath_selfcheck ${WW}/WWMath/test_inverse.cpp)
target_include_directories(wwmath_selfcheck PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)
target_link_libraries(wwmath_selfcheck PRIVATE wwmath wwsaveload wwlib wwdebug)

# ---- wwlib ----------------------------------------------------------------
set(WWLIB_SOURCES
  ${WW}/WWLib/_convert.cpp   ${WW}/WWLib/_mono.cpp      ${WW}/WWLib/argv.cpp
  ${WW}/WWLib/b64pipe.cpp    ${WW}/WWLib/b64straw.cpp   ${WW}/WWLib/base64.cpp
  ${WW}/WWLib/blit.cpp       ${WW}/WWLib/blowfish.cpp   ${WW}/WWLib/buff.cpp
  ${WW}/WWLib/bufffile.cpp   ${WW}/WWLib/chunkio.cpp    ${WW}/WWLib/convert.cpp
  ${WW}/WWLib/cpudetect.cpp  ${WW}/WWLib/crc.cpp        ${WW}/WWLib/cstraw.cpp
  ${WW}/WWLib/data.cpp       ${WW}/WWLib/ddraw.cpp      ${WW}/WWLib/dsurface.cpp
  ${WW}/WWLib/Except.cpp     ${WW}/WWLib/FastAllocator.cpp
  ${WW}/WWLib/ffactory.cpp   ${WW}/WWLib/fixed.cpp      ${WW}/WWLib/md5.cpp
  ${WW}/WWLib/gcd_lcm.cpp    ${WW}/WWLib/hash.cpp
  ${WW}/WWLib/hsv.cpp        ${WW}/WWLib/ini.cpp        ${WW}/WWLib/int.cpp
  ${WW}/WWLib/jshell.cpp     ${WW}/WWLib/keyboard.cpp   ${WW}/WWLib/LaunchWeb.cpp
  ${WW}/WWLib/lcw.cpp        ${WW}/WWLib/load.cpp       ${WW}/WWLib/mixfile.cpp
  ${WW}/WWLib/mono.cpp       ${WW}/WWLib/mpmath.cpp     ${WW}/WWLib/mpu.cpp
  ${WW}/WWLib/msgloop.cpp    ${WW}/WWLib/multilist.cpp  ${WW}/WWLib/mutex.cpp
  ${WW}/WWLib/nstrdup.cpp    ${WW}/WWLib/palette.cpp    ${WW}/WWLib/pcx.cpp
  ${WW}/WWLib/pipe.cpp       ${WW}/WWLib/pk.cpp         ${WW}/WWLib/ramfile.cpp
  ${WW}/WWLib/random.cpp     ${WW}/WWLib/rawfile.cpp    ${WW}/WWLib/rc4.cpp
  ${WW}/WWLib/rcfile.cpp     ${WW}/WWLib/readline.cpp   ${WW}/WWLib/realcrc.cpp
  ${WW}/WWLib/refcount.cpp   ${WW}/WWLib/registry.cpp   ${WW}/WWLib/rgb.cpp
  ${WW}/WWLib/rle.cpp        ${WW}/WWLib/rndstrng.cpp   ${WW}/WWLib/sampler.cpp
  ${WW}/WWLib/sha.cpp        ${WW}/WWLib/slnode.cpp     ${WW}/WWLib/srandom.cpp
  ${WW}/WWLib/stimer.cpp     ${WW}/WWLib/straw.cpp      ${WW}/WWLib/strtok_r.cpp
  ${WW}/WWLib/systimer.cpp   ${WW}/WWLib/tagblock.cpp   ${WW}/WWLib/TARGA.CPP
  ${WW}/WWLib/textfile.cpp   ${WW}/WWLib/tgatodxt.cpp   ${WW}/WWLib/thread.cpp
  ${WW}/WWLib/trim.cpp       ${WW}/WWLib/vector.cpp     ${WW}/WWLib/verchk.cpp
  ${WW}/WWLib/widestring.cpp ${WW}/WWLib/win.cpp        ${WW}/WWLib/WWCOMUtil.cpp
  ${WW}/WWLib/wwfile.cpp     ${WW}/WWLib/wwfont.cpp     ${WW}/WWLib/wwstring.cpp
  ${WW}/WWLib/xpipe.cpp      ${WW}/WWLib/xstraw.cpp     ${WW}/WWLib/xsurface.cpp)
# Windows on Arm leaves mpu.cpp out, as the build off Windows does (the POSIX list below says why nothing
# needs it): its four functions read the x86 time-stamp counter, which ARM64 does not have.
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
  list(REMOVE_ITEM WWLIB_SOURCES ${WW}/WWLib/mpu.cpp)
endif()
# Win32 subsystem code, left out of wwlib everywhere but Windows.  Each of these is a whole file of
# one Windows API rather than a type leaking out of one, so there is nothing to port short of
# reimplementing the API.  On Windows this list is not read, and wwlib gets the same sources in the
# same order as before.  Put one back only with a caller that needs it.
#
# How "no caller" was established, because the first way was wrong: B5 searched for includers of
# each header, and that cannot see a function-local extern - verchk.cpp's, below.  Re-audited
# 2026-09-25 by symbol: every function, class and global each file defines, searched as a whole word
# through every tracked source outside Tools/, and each hit read.  Apart from RegistryClass (callers
# named below) and verchk (fixed), every hit was a comment, a string, a commented-out block, a type
# named only by pointer or reference, or code under a macro nothing defines (Except.cpp's
# DEMO_TIME_OUT).  What that cannot see: a name built by token pasting, and vendored code, which is
# not tracked and does not use WWLib.
if(ZH_PLATFORM_POSIX)
  list(REMOVE_ITEM WWLIB_SOURCES
    # DirectDraw, the pre-Direct3D 2D surface API.  The renderer never reaches it; DSurface is named
    # nowhere outside WWLib, and draw.cpp, the fourth file of the set, was already out.  ConvertClass
    # survives as a type: wwfont.cpp uses only its inline Convert_Pixel, WW3D2's txt*.cpp only
    # references to it.
    ${WW}/WWLib/convert.cpp    ${WW}/WWLib/ddraw.cpp      ${WW}/WWLib/dsurface.cpp
    # The \\.\MONO device driver for a second, monochrome debug monitor.  WW3D2's rendobj.cpp
    # includes _mono.h and names nothing from it.
    ${WW}/WWLib/_mono.cpp      ${WW}/WWLib/mono.cpp
    # Win32 resources, the message pump and its keyboard, ShellExecute, COM IDispatch and version
    # resources.  No callers at all.
    ${WW}/WWLib/data.cpp       ${WW}/WWLib/rcfile.cpp     ${WW}/WWLib/msgloop.cpp
    ${WW}/WWLib/keyboard.cpp   ${WW}/WWLib/LaunchWeb.cpp  ${WW}/WWLib/WWCOMUtil.cpp
    # verchk.cpp had one caller after all: Wwutil's Get_File_Id_String declares its
    # Get_Image_File_Header extern inside the function, so a search for verchk.h misses it.  That
    # call is Windows-only now; off Windows the timestamp is 0.
    ${WW}/WWLib/verchk.cpp
    # The HINSTANCE/HWND globals, which win.h declares only under _WINDOWS.
    ${WW}/WWLib/win.cpp
    # RegistryClass is HKEY throughout.  Its callers are dx8wrapper.cpp, W3DDisplay.cpp and
    # WWAudio.cpp, which persist device and audio settings; where those live on macOS is theirs
    # to decide, and registry.h says so to anyone who includes it.
    ${WW}/WWLib/registry.cpp
    # SecureRandomClass has no caller, and its only non-Windows seed is the _UNIX arm, which the
    # plan forbids switching on.
    ${WW}/WWLib/srandom.cpp
    # Get_CPU_Rate, Get_CPU_Clock, Get_RDTSC_CPU_Speed and RDTSC: an x86 time-stamp counter timed
    # against QueryPerformanceCounter.  Nothing calls any of the four - cpudetect.cpp and Except.cpp
    # include mpu.h and use nothing from it, and cpudetect reads __rdtsc itself.
    ${WW}/WWLib/mpu.cpp)
  # And one POSIX-only file: the engine's paths resolved against a POSIX file system, and the zh_*
  # forwarders built on it (C1, decisions D1 and D4).  WWLib because RawFileClass and GameEngine both
  # link it, and the resolver has to work before any engine subsystem exists.
  list(APPEND WWLIB_SOURCES ${WW}/WWLib/posixpath.cpp)
endif()
add_library(wwlib STATIC ${ZH_NOT_YET_PORTABLE} ${WWLIB_SOURCES})
# GameEngine/Include for Common/EarlyCommandLine.h alone, as wwdebug has it.
target_include_directories(wwlib PUBLIC ${WW}/WWLib
  PRIVATE ${WW}/WWMath ${WW}/Wwutil ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
          ${CMAKE_CURRENT_SOURCE_DIR}/GameEngine/Include)
target_link_libraries(wwlib PRIVATE wwdebug)
if(ZH_PLATFORM_WINDOWS)
  # winmm: timeGetTime/timeBeginPeriod.  version: verchk.cpp's GetFileVersionInfo,
  # which Wwutil drags in for Get_File_Id_String.  Both are Windows import libraries; B2 replaces
  # the first with a platform clock and B3 the second.
  target_link_libraries(wwlib PUBLIC winmm version)
else()
  # thread.cpp and mutex.cpp call pthreads, which before glibc 2.34 (the Steam Runtime's 2.31 among
  # them) live in libpthread rather than libc: without this, a program that links wwlib and nothing
  # else that carries threads, such as wwmath_selfcheck, cannot link there.
  find_package(Threads REQUIRED)
  target_link_libraries(wwlib PUBLIC Threads::Threads)
endif()

# ---- wwsaveload -----------------------------------------------------------
add_library(wwsaveload STATIC ${ZH_NOT_YET_PORTABLE}
  ${WW}/WWSaveLoad/definition.cpp          ${WW}/WWSaveLoad/definitionfactory.cpp
  ${WW}/WWSaveLoad/definitionfactorymgr.cpp ${WW}/WWSaveLoad/definitionmgr.cpp
  ${WW}/WWSaveLoad/parameter.cpp           ${WW}/WWSaveLoad/persistfactory.cpp
  ${WW}/WWSaveLoad/pointerremap.cpp        ${WW}/WWSaveLoad/saveload.cpp
  ${WW}/WWSaveLoad/saveloadstatus.cpp      ${WW}/WWSaveLoad/saveloadsubsystem.cpp
  ${WW}/WWSaveLoad/twiddler.cpp            ${WW}/WWSaveLoad/wwsaveload.cpp)
target_include_directories(wwsaveload PUBLIC ${WW}/WWSaveLoad
  PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/Wwutil ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)
target_link_libraries(wwsaveload PRIVATE wwdebug wwlib wwmath)

# ---- WW3D2's sources, on every platform ------------------------------------
# The renderer core is the same files on Windows and off it (decision 7: off Windows it speaks D3D9 to
# a device of our own).  Three are Windows's alone, and its block adds them back: FramGrab.cpp (AVI
# capture through vfw32), dx8webbrowser.cpp (the embedded browser, an MSVC #import) and dx11runtime.cpp
# (the Direct3D 11 backend's switchboard; dx11runtime_posix.cpp answers for it off Windows).
set(ZH_WW3D2_SOURCES
  ${WW}/WW3D2/aabtree.cpp ${WW}/WW3D2/aabtreebuilder.cpp ${WW}/WW3D2/agg_def.cpp
  ${WW}/WW3D2/animatedsoundmgr.cpp ${WW}/WW3D2/animobj.cpp ${WW}/WW3D2/assetmgr.cpp
  ${WW}/WW3D2/assetstatus.cpp ${WW}/WW3D2/bitmaphandler.cpp ${WW}/WW3D2/bmp2d.cpp
  ${WW}/WW3D2/boxrobj.cpp ${WW}/WW3D2/bwrender.cpp ${WW}/WW3D2/camera.cpp
  ${WW}/WW3D2/collect.cpp ${WW}/WW3D2/coltest.cpp ${WW}/WW3D2/composite.cpp
  ${WW}/WW3D2/dazzle.cpp ${WW}/WW3D2/ddsfile.cpp ${WW}/WW3D2/decalmsh.cpp
  ${WW}/WW3D2/decalsys.cpp ${WW}/WW3D2/distlod.cpp ${WW}/WW3D2/dx8caps.cpp
  ${WW}/WW3D2/dx8fvf.cpp ${WW}/WW3D2/dx8indexbuffer.cpp ${WW}/WW3D2/dx8polygonrenderer.cpp
  ${WW}/WW3D2/dx8renderer.cpp ${WW}/WW3D2/dx8rendererdebugger.cpp ${WW}/WW3D2/dx8texman.cpp
  ${WW}/WW3D2/dx8vertexbuffer.cpp ${WW}/WW3D2/dx8wrapper.cpp ${WW}/WW3D2/dynamesh.cpp
  ${WW}/WW3D2/ffprobe.cpp ${WW}/WW3D2/ffshadercache.cpp ${WW}/WW3D2/font3d.cpp
  ${WW}/WW3D2/formconv.cpp ${WW}/WW3D2/hanim.cpp ${WW}/WW3D2/hanimmgr.cpp
  ${WW}/WW3D2/hcanim.cpp ${WW}/WW3D2/hlod.cpp ${WW}/WW3D2/hmdldef.cpp
  ${WW}/WW3D2/hmorphanim.cpp ${WW}/WW3D2/hrawanim.cpp ${WW}/WW3D2/htree.cpp
  ${WW}/WW3D2/htreemgr.cpp ${WW}/WW3D2/intersec.cpp ${WW}/WW3D2/layer.cpp
  ${WW}/WW3D2/light.cpp ${WW}/WW3D2/lightenvironment.cpp ${WW}/WW3D2/line3d.cpp
  ${WW}/WW3D2/linegrp.cpp ${WW}/WW3D2/mapper.cpp ${WW}/WW3D2/matinfo.cpp
  ${WW}/WW3D2/matpass.cpp ${WW}/WW3D2/matrixmapper.cpp ${WW}/WW3D2/mesh.cpp
  ${WW}/WW3D2/meshbuild.cpp ${WW}/WW3D2/meshdam.cpp ${WW}/WW3D2/meshgeometry.cpp
  ${WW}/WW3D2/meshmatdesc.cpp ${WW}/WW3D2/meshmdl.cpp ${WW}/WW3D2/meshmdlio.cpp
  ${WW}/WW3D2/metalmap.cpp ${WW}/WW3D2/missingtexture.cpp ${WW}/WW3D2/motchan.cpp
  ${WW}/WW3D2/nullrobj.cpp ${WW}/WW3D2/part_buf.cpp ${WW}/WW3D2/part_emt.cpp
  ${WW}/WW3D2/part_ldr.cpp ${WW}/WW3D2/pivot.cpp ${WW}/WW3D2/pointgr.cpp
  ${WW}/WW3D2/polyinfo.cpp ${WW}/WW3D2/predlod.cpp ${WW}/WW3D2/prim_anim.cpp
  ${WW}/WW3D2/projector.cpp ${WW}/WW3D2/proto.cpp ${WW}/WW3D2/render2d.cpp
  ${WW}/WW3D2/render2dsentence.cpp ${WW}/WW3D2/renderobjectrecycler.cpp ${WW}/WW3D2/rendobj.cpp
  ${WW}/WW3D2/rinfo.cpp ${WW}/WW3D2/ringobj.cpp ${WW}/WW3D2/scene.cpp
  ${WW}/WW3D2/segline.cpp ${WW}/WW3D2/seglinerenderer.cpp ${WW}/WW3D2/shader.cpp
  ${WW}/WW3D2/shattersystem.cpp ${WW}/WW3D2/snappts.cpp ${WW}/WW3D2/sortingrenderer.cpp
  ${WW}/WW3D2/soundrobj.cpp ${WW}/WW3D2/sphereobj.cpp ${WW}/WW3D2/static_sort_list.cpp
  ${WW}/WW3D2/statistics.cpp ${WW}/WW3D2/streak.cpp ${WW}/WW3D2/streakRender.cpp
  ${WW}/WW3D2/stripoptimizer.cpp ${WW}/WW3D2/surfaceclass.cpp ${WW}/WW3D2/texfcach.cpp
  ${WW}/WW3D2/texproject.cpp ${WW}/WW3D2/texture.cpp ${WW}/WW3D2/texturefilter.cpp
  ${WW}/WW3D2/textureloader.cpp ${WW}/WW3D2/texturethumbnail.cpp ${WW}/WW3D2/txt.cpp
  ${WW}/WW3D2/vertmaterial.cpp ${WW}/WW3D2/visrasterizer.cpp ${WW}/WW3D2/w3d_dep.cpp
  ${WW}/WW3D2/w3d_util.cpp ${WW}/WW3D2/w3dexclusionlist.cpp ${WW}/WW3D2/ww3d.cpp
  ${WW}/WW3D2/ww3dformat.cpp
)

# ===========================================================================
# Windows only, from here down to dx9_smoke: every target in this block links d3d9, d3d11 or dxgi,
# or is built out of something that does.  Nothing is removed and nothing is stubbed - tracks D2
# through D5 abstract the backend interface and add a Metal one behind the same seam, and they
# bring these back one at a time.  Left unindented so the diff that added the guard is the guard.
# ===========================================================================
if(ZH_PLATFORM_WINDOWS)

# ---- d3dx9runtime ---------------------------------------------------------
# The D3DX9 entry points the renderer binds at run time, in a library of their own
# because GameEngine needs the math half without the renderer: BezierSegment carries
# D3DXMATRIX and DumbProjectileBehavior steers a shell with it.
# d3dx9portable.cpp is D3DX's arithmetic and d3dx9portable_texture.cpp its texture helpers, over the macOS and
# Linux surface code (PosixImage, PosixImageOps, PosixPixelCodec), which Windows on ARM64 binds in place of the
# DLL it does not have (and x64 with ZH_D3DX_PORTABLE=1); test_d3dx9portable_oracle holds both against the DLL.
set(ZH_POSIX_RENDER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render)
add_library(d3dx9runtime STATIC
  ${WW}/WW3D2/d3dx9runtime.cpp ${WW}/WW3D2/d3d8shadertranslate.cpp ${WW}/WW3D2/d3dx9portable.cpp
  ${WW}/WW3D2/d3dx9portable_texture.cpp ${ZH_POSIX_RENDER_DIR}/PosixImage.cpp
  ${ZH_POSIX_RENDER_DIR}/PosixImageOps.cpp ${ZH_POSIX_RENDER_DIR}/PosixPixelCodec.cpp)
set_target_properties(d3dx9runtime PROPERTIES CXX_STANDARD 17)
target_include_directories(d3dx9runtime PUBLIC ${WW}/WW3D2 PRIVATE ${ZH_POSIX_RENDER_DIR})
target_link_libraries(d3dx9runtime PUBLIC d3d9)

# ---- ffshader -------------------------------------------------------------
# The fixed-function texture stages written out as HLSL, phase 2's replacement for
# combiners D3D11 does not have.  Its own library because it is string building over
# a description and nothing else: the test links this alone rather than all of WW3D2.
add_library(ffshader STATIC ${WW}/WW3D2/ffshader.cpp)
set_target_properties(ffshader PROPERTIES CXX_STANDARD 17)
target_include_directories(ffshader PUBLIC ${WW}/WW3D2)
target_link_libraries(ffshader PUBLIC d3d9)

# ---- ffvertex -------------------------------------------------------------
# The other half of the same job: the fixed-function vertex pipeline as HLSL.  It
# generates for both profiles so the D3D9 form can be compared against the pipeline
# it replaces before the D3D11 one is trusted.
add_library(ffvertex STATIC ${WW}/WW3D2/ffvertex.cpp)
set_target_properties(ffvertex PROPERTIES CXX_STANDARD 17)
target_include_directories(ffvertex PUBLIC ${WW}/WW3D2)
target_link_libraries(ffvertex PUBLIC d3d9)

# ---- engineshader ---------------------------------------------------------
# The shipped .vso and .pso files transcribed as HLSL, for the draws that bind one
# of the engine's own shaders and set no fixed-function state a generator could read.
# Its own library for the same reason ffvertex is: string building over an enum, and
# the test compiles what it writes without a device anywhere near it.
add_library(engineshader STATIC ${WW}/WW3D2/engineshader.cpp)
set_target_properties(engineshader PROPERTIES CXX_STANDARD 17)
target_include_directories(engineshader PUBLIC ${WW}/WW3D2)
target_link_libraries(engineshader PUBLIC ffshader)

# ---- dx11device -----------------------------------------------------------
# The Direct3D 11 device, its swap chain and the views on it: the bottom of phase 2.
# Its own library for the same reason ffshader is, and for one more - it links d3d11
# and dxgi, and nothing else in the tree does yet.  The test creates a real device.
add_library(dx11device STATIC ${WW}/WW3D2/dx11device.cpp)
set_target_properties(dx11device PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11device PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11device PUBLIC d3d11 dxgi)

# ---- dx11state ------------------------------------------------------------
# D3D9's render states turned into D3D11's three state objects.  Pure translation,
# no device, so the whole table is checked by a test rather than looked at on screen.
add_library(dx11state STATIC ${WW}/WW3D2/dx11state.cpp)
set_target_properties(dx11state PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11state PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11state PUBLIC d3d9 d3d11)

# ---- dx11layout -----------------------------------------------------------
# A D3D9 flexible vertex format read out as a D3D11 input layout.  The vertex buffers
# keep the bytes they have; this is what tells D3D11 where the fields are.
add_library(dx11layout STATIC ${WW}/WW3D2/dx11layout.cpp)
set_target_properties(dx11layout PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11layout PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11layout PUBLIC d3d9 d3d11)

# ---- dx11resource ---------------------------------------------------------
# The buffers and textures: D3D9's formats, pools and lock flags read as D3D11's
# formats, usages and map types.  Half a table and half what the device will accept,
# so the test checks the table and then hands the results to a real device.
add_library(dx11resource STATIC ${WW}/WW3D2/dx11resource.cpp)
set_target_properties(dx11resource PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11resource PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11resource PUBLIC d3d9 d3d11)

# ---- dx11twin -------------------------------------------------------------
# The D3D11 copy of a vertex or index buffer and the heap block both copies are
# written through.  A D3D9 buffer is write-only, so the copy cannot be built after
# the fact and the lock has to be moved instead; the test proves the same bytes end
# up on both sides.
add_library(dx11twin STATIC ${WW}/WW3D2/dx11twin.cpp)
set_target_properties(dx11twin PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11twin PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11twin PUBLIC dx11resource d3d11)

# ---- dx11texture ----------------------------------------------------------
# The D3D11 copy of a texture the engine already built in D3D9, read back off the
# managed original at the first bind and kept on it as private data.
add_library(dx11texture STATIC ${WW}/WW3D2/dx11texture.cpp)
set_target_properties(dx11texture PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11texture PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11texture PUBLIC dx11resource d3d9 d3d11 dxguid)

# ---- dx11post -------------------------------------------------------------
# The offscreen render target between the scene and the swap chain, and the full
# screen passes over it.  Depends on the device and on nothing else: it reads a
# texture and writes a render target, and knows nothing about the state
# translation the rest of the backend is.
add_library(dx11post STATIC ${WW}/WW3D2/dx11post.cpp)
set_target_properties(dx11post PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11post PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11post PUBLIC dx11device d3d11)

# ---- dx11backend ----------------------------------------------------------
# The renderer itself: the engine's D3D9-shaped calls taken as they are and resolved
# into a D3D11 pipeline at the moment of the draw.  Ties the five pieces above
# together, which is why it is the last of them and the one with a drawing test.
add_library(dx11backend STATIC ${WW}/WW3D2/dx11backend.cpp)
set_target_properties(dx11backend PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11backend PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11backend PUBLIC dx11device dx11state dx11layout dx11resource
  engineshader ffshader ffvertex)

# ---- ww3d2 (renderer core) ------------------------------------------------
# wwshade is header-only here on purpose: no .dsp links wwshade.lib and its
# shd*bump* sources need shdpp/NVASM-generated headers.  Revisit at exe link.
# dx8webbrowser.cpp is the embedded browser: its header sets ENABLE_EMBEDDED_BROWSER to 1, so it is
# live on Windows (the POSIX build leaves it out).
add_library(ww3d2 STATIC
  ${WW}/WW3D2/FramGrab.cpp ${WW}/WW3D2/dx8webbrowser.cpp ${WW}/WW3D2/dx11runtime.cpp
  ${ZH_WW3D2_SOURCES}
)
target_include_directories(ww3d2
  PUBLIC ${WW}/WW3D2
  PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil
          ${WW}/wwshade ${WW}/WWAudio ${WW}/Miles6/include)
# RENDERER-ROADMAP.md phase 1: d3d9.h and d3d9.lib come out of the Windows SDK, so
# the vendored DirectX 8 headers are off the include path entirely - a d3d8.h left
# reachable is a file that compiles against the wrong runtime without saying so.
# D3DX9 has no import library anywhere and is bound at runtime instead: see
# WW3D2/d3dx9runtime.h.  vfw32 is FramGrab.cpp writing AVIs.
target_link_libraries(ww3d2
  PUBLIC d3d9 dxguid vfw32 d3dx9runtime ffshader dx11backend dx11post dx11twin dx11texture
  PRIVATE wwlib wwmath wwdebug wwsaveload)
# dx8webbrowser.cpp is the one file /MP cannot have: MSVC rejects #import under
# multi-process compilation (C2813) and finds the directive by a lexical scan, so
# even though ENABLE_EMBEDDED_BROWSER's block is what holds it, disabling the block
# would not help.  /MP1 puts just this file back on a single process.
set_source_files_properties(${WW}/WW3D2/dx8webbrowser.cpp PROPERTIES COMPILE_OPTIONS /MP1)

# Phase 8: 1 kHz EIP sampler that symbolizes against the linker map - the
# machine has no debugger, so this is how hotspots get names.
add_executable(eip_sampler ${CMAKE_CURRENT_SOURCE_DIR}/Tools/ProfileSampler/eip_sampler.cpp)
set_target_properties(eip_sampler PROPERTIES CXX_STANDARD 17)

# RENDERER-ROADMAP.md phase 1 checkpoint: a real device, with no translating dll
# anywhere.  It replaces dx8_smoke, which proved the same thing about the vendored
# DirectX 8 SDK and the d3d8.dll the renderer used to load; that path is gone.
# d3d9.h and d3d9.lib come from the Windows SDK, so this target never sees
# Libraries/DirectX/Include.
add_executable(dx9_smoke ${WW}/WW3D2/test_dx9_smoke.cpp)
target_link_libraries(dx9_smoke PRIVATE d3dx9runtime user32)
# Tests/ for d3dx_golden.h: dx9_smoke prints what this machine's d3dx9_43.dll computes for
# B17's golden rows, and which of its CPU-dispatched bodies ran.
target_include_directories(dx9_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests)

# d3dx9portable.cpp against d3dx9_43.dll itself (W-ARM64): what ARM64, macOS and Linux use in the DLL's place,
# compared with the DLL where it binds (x64): the arithmetic, and on a real Direct3D 9 device the texture helpers
# and the shader tools ARM64 binds.  Where the DLL does not bind (ARM64, or no DirectX redistributable), there
# is nothing to compare, and the test exits 77, which ctest reports as skipped rather than passed.
add_executable(test_d3dx9portable_oracle ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_d3dx9portable_oracle.cpp)
target_include_directories(test_d3dx9portable_oracle PRIVATE ${ZH_POSIX_RENDER_DIR})
target_link_libraries(test_d3dx9portable_oracle PRIVATE d3dx9runtime user32)
add_test(NAME test_d3dx9portable_oracle COMMAND test_d3dx9portable_oracle)
set_tests_properties(test_d3dx9portable_oracle PROPERTIES SKIP_RETURN_CODE 77)
endif()  # ZH_PLATFORM_WINDOWS - renderer, Direct3D 9/11 backends, dx9_smoke

# ===========================================================================
# Phase 2 - remaining support libs.  None of these touch DX8.
# ===========================================================================
set(LIBSRC ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source)

# The next three link Windows import libraries and nothing else stands in for them yet: debug wants
# dbghelp and comctl32, profile is built on debug, and wwdownload is FTP over winsock.  C5 replaces
# the crash reporting and B6 decides what wwdownload becomes on macOS.
if(ZH_PLATFORM_WINDOWS)
# ---- debug ----------------------------------------------------------------
# EA's in-repo debug/logging lib (RTS.dsp links it as debug.lib).  Self-contained
# apart from the Win32 SDK.  The test1-6/, netserv/ and debug_dlg/ subdirs are
# separate .dsp projects, not part of the lib.
add_library(debug STATIC
  ${LIBSRC}/debug/debug.cpp        ${LIBSRC}/debug/debug_cmd.cpp
  ${LIBSRC}/debug/debug_debug.cpp  ${LIBSRC}/debug/debug_except.cpp
  ${LIBSRC}/debug/debug_getdefaultcommands.cpp
  ${LIBSRC}/debug/debug_internal.cpp
  ${LIBSRC}/debug/debug_io_con.cpp ${LIBSRC}/debug/debug_io_flat.cpp
  ${LIBSRC}/debug/debug_io_net.cpp ${LIBSRC}/debug/debug_io_ods.cpp
  ${LIBSRC}/debug/debug_purecall.cpp
  ${LIBSRC}/debug/debug_stack.cpp)
target_include_directories(debug PUBLIC ${LIBSRC}/debug)
# Common/EarlyCommandLine.h, header-only: the crash box stands aside under -headless (W2)
target_include_directories(debug PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngine/Include)
target_link_libraries(debug PUBLIC dbghelp comctl32 ws2_32)
# "debug" is also a target_link_libraries config keyword, so it can never be
# passed as a plain library name.  Link this alias instead.
add_library(debuglib ALIAS debug)
# debug.h asks the linker for this library by the build's name (#pragma comment(lib, "debugdebug.lib") under
# _DEBUG), so a Debug build names it that way.  Release keeps debug.lib, the name it asks for there.
set_target_properties(debug PROPERTIES OUTPUT_NAME_DEBUG debugdebug)

# ---- profile --------------------------------------------------------------
# EA's in-repo sampling/function profiler.  GameEngine includes profile.h, whose
# #pragma comment(lib, "profile.lib") makes this a hard link dependency even
# though almost nothing calls into it.
add_library(profile STATIC
  ${LIBSRC}/profile/profile.cpp           ${LIBSRC}/profile/profile_cmd.cpp
  ${LIBSRC}/profile/profile_funclevel.cpp ${LIBSRC}/profile/profile_highlevel.cpp
  ${LIBSRC}/profile/profile_result.cpp)
target_include_directories(profile PUBLIC ${LIBSRC}/profile)
target_link_libraries(profile PUBLIC debuglib)
# profile.h asks for it by the build's name too (profiledebug.lib under _DEBUG), as debug.h does.
set_target_properties(profile PROPERTIES OUTPUT_NAME_DEBUG profiledebug)

# ---- wwdownload -----------------------------------------------------------
# FTP patch downloader.  Its wlib/resource.h includes are all commented out in
# the shipped source, so it needs nothing but winsock.
add_library(wwdownload STATIC
  ${WW}/WWDownload/Download.cpp ${WW}/WWDownload/FTP.CPP
  ${WW}/WWDownload/registry.cpp ${WW}/WWDownload/urlBuilder.cpp)
target_include_directories(wwdownload PUBLIC ${WW}/WWDownload ${WW})
target_link_libraries(wwdownload PUBLIC wsock32 winmm)
endif()  # ZH_PLATFORM_WINDOWS - debug, profile, wwdownload

# ---- benchmark ------------------------------------------------------------
# BYTEmark sources are missing; benchmark_stub.c stands in for them.  See the
# file header for why a constant is enough and when to revisit.
add_library(benchmark STATIC ${LIBSRC}/Benchmark/benchmark_stub.c)
target_include_directories(benchmark PUBLIC ${LIBSRC}/Benchmark)

# ---- eabrowserdispatch ----------------------------------------------------
# MIDL is a Windows SDK tool and this whole library is its output, so there is nothing to generate
# on a Mac.  B6 puts a null WebBrowser in Stubs/ for the macOS side.
if(ZH_PLATFORM_WINDOWS)
# Only BrowserDispatch.idl shipped; the .h/_i.c that GameEngine's WebBrowser.h
# expects are MIDL output.  Generate them at build time (MSBuild puts cl.exe,
# MIDL's preprocessor, on PATH) into a dir laid out so the shipped
# #include "EABrowserDispatch/BrowserDispatch.h" resolves unchanged.
set(EABD_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/EABrowserDispatch)
file(MAKE_DIRECTORY ${EABD_GEN})
# midl.exe ships with the Windows SDK, not on PATH at configure time.
file(GLOB MIDL_HINTS "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x86")
find_program(MIDL_EXECUTABLE midl HINTS ${MIDL_HINTS} REQUIRED)
add_custom_command(
  OUTPUT ${EABD_GEN}/BrowserDispatch.h ${EABD_GEN}/BrowserDispatch_i.c
  COMMAND ${MIDL_EXECUTABLE} /nologo /env win32 /win32
          /h ${EABD_GEN}/BrowserDispatch.h
          /iid ${EABD_GEN}/BrowserDispatch_i.c
          /proxy ${EABD_GEN}/BrowserDispatch_p.c
          /dlldata ${EABD_GEN}/dlldata.c
          /tlb ${EABD_GEN}/BrowserDispatch.tlb
          ${LIBSRC}/EABrowserDispatch/BrowserDispatch.idl
  DEPENDS ${LIBSRC}/EABrowserDispatch/BrowserDispatch.idl
  COMMENT "MIDL BrowserDispatch.idl")
add_library(eabrowserdispatch STATIC
  ${EABD_GEN}/BrowserDispatch_i.c ${EABD_GEN}/BrowserDispatch.h)
target_include_directories(eabrowserdispatch
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
endif()  # ZH_PLATFORM_WINDOWS - MIDL-generated browser dispatch

# ---- gamespy --------------------------------------------------------------
# Every platform (B6): the SDK is CMake-based and portable, and gameengine's online code - PeerThread,
# BuddyThread, PersistentStorageThread, MainMenuUtils - calls 89 of its entry points.  Built for real
# rather than stubbed, with Tools/vendor.sh's two patches applied (gamespy-strlwr-c-only.patch, and
# gamespy-gsi-unix.patch, which renames its _UNIX to GSI_UNIX so no engine file sees _UNIX).  Nothing
# in it runs until a player opens the online menus, and the services it would reach are gone.
# PLAN.md called for hand-written stubs behind the five GameSpy* libs, but the
# real SDK is open source (TheSuperHackers/GamespySDK, itself CMake-based), so
# vendor and build it instead: correct signatures, structs and enum values
# rather than 73 guessed prototypes.  Multiplayer stays out of scope - this only
# has to compile and link; nothing needs to reach a live GameSpy server.
# OpenSSL/HTTPS off so the SDK does not FetchContent OpenSSL for services that
# have been dead since 2014; voice and tests off for the same reason.
set(GS_OPENSSL          OFF CACHE BOOL "" FORCE)
set(GS_SECURESECONNECT  OFF CACHE BOOL "" FORCE)
set(GS_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(GS_INCLUDE_VOICE    OFF CACHE BOOL "" FORCE)
add_subdirectory(${LIBSRC}/GameSpy ${CMAKE_CURRENT_BINARY_DIR}/gamespy)

# ---- litehtml -------------------------------------------------------------
# The HTML and CSS layout engine behind the pages drawn over the battlefield (GameClient/
# HtmlOverlay.cpp).  It lays a page out and calls back to draw each box and each run of text, so
# the drawing stays the game's own.  Its own CMakeLists sets /permissive- and C++17 for itself
# alone, and brings the gumbo HTML parser with it.
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${LIBSRC}/litehtml ${CMAKE_CURRENT_BINARY_DIR}/litehtml)

# ---- SDL3 and miniaudio: the platform layer off Windows (decision 3) -------------------------------
# Both are fetched by Tools/vendor.sh and neither is fetched on Windows, which keeps Win32Device and
# Miles.  Nothing links either yet: C2/C3 take SDL3's window and events, D4 its GPU API, C4 miniaudio.
# They are built by default all the same, so that a break shows up in the build that caused it
# rather than in the first task to link them.
if(ZH_PLATFORM_POSIX)
  # SDL3 static, and only the subsystems decision 3 uses.  On: video (window, events, message boxes),
  # GPU (the renderer - Metal on macOS, Vulkan on Linux) and dialog (the first launch's folder chooser,
  # PosixMain.cpp: NSOpenPanel on macOS, the portal or zenity on Linux; with it off, SDL answers every
  # dialog at once with "not built with dialog support", and the chooser quit before showing anything).
  # Off: audio (miniaudio has that job), render (SDL's 2D renderer; D4 uses the GPU API directly),
  # camera, joystick, haptic, hidapi, power, sensor and tray, and the OpenGL/GLES video drivers, which
  # the GPU API does not use.  Tests, examples and install are off.
  foreach(_sdl_off SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL SDL_UNINSTALL
                   SDL_AUDIO SDL_RENDER SDL_CAMERA SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_POWER
                   SDL_SENSOR SDL_TRAY SDL_OPENGL SDL_OPENGLES)
    set(${_sdl_off} OFF CACHE BOOL "" FORCE)
  endforeach()
  foreach(_sdl_on SDL_STATIC SDL_VIDEO SDL_GPU SDL_DIALOG)
    set(${_sdl_on} ON CACHE BOOL "" FORCE)
  endforeach()
  add_subdirectory(${LIBSRC}/SDL3 ${CMAKE_CURRENT_BINARY_DIR}/SDL3)

  # miniaudio: one C file.  Its own CMakeLists builds extras and examples this does not want.
  # It opens the platform's audio libraries at run time by default, so the static library needs
  # nothing at link beyond threads, libm and libdl, which are its consumers' to carry.
  find_package(Threads REQUIRED)
  add_library(miniaudio STATIC ${LIBSRC}/miniaudio/miniaudio.c)
  target_include_directories(miniaudio PUBLIC ${LIBSRC}/miniaudio)
  # libm on every POSIX system: separate from libc on Linux and the BSDs, part of libSystem on macOS,
  # where -lm is accepted and does nothing.  Not keyed on Linux, so the next platform needs no edit.
  target_link_libraries(miniaudio INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)

  # ---- The shader generators and what compiles their SDL3 GPU target (decision 4, D3) -----------
  # The generators are string building over a description; since ffstate.h they need nothing from
  # the DirectX SDK, so off Windows they are the same three libraries without d3d9.
  add_library(ffshader STATIC ${WW}/WW3D2/ffshader.cpp)
  target_include_directories(ffshader PUBLIC ${WW}/WW3D2)
  add_library(ffvertex STATIC ${WW}/WW3D2/ffvertex.cpp)
  target_include_directories(ffvertex PUBLIC ${WW}/WW3D2)
  add_library(engineshader STATIC ${WW}/WW3D2/engineshader.cpp)
  target_include_directories(engineshader PUBLIC ${WW}/WW3D2)
  target_link_libraries(engineshader PUBLIC ffshader)

  # ---- Text off Windows (decision 6, D6) -----------------------------------------------------------
  # FreeType 2.14.3, fetched and pinned by vendor.sh, with none of its optional dependencies: the game's
  # fonts are plain TrueType files, and zlib, bzip2, PNG, HarfBuzz and Brotli only matter to compressed,
  # colour or shaped fonts.  Its gzip module uses its own zlib copy.  EXCLUDE_FROM_ALL, like the shader
  # compilers: what links it is what builds it.
  function(zh_add_freetype)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS OFF)
    foreach(_ft_dep ZLIB BZIP2 PNG HARFBUZZ BROTLI)
      set(FT_DISABLE_${_ft_dep} ON)
    endforeach()
    set(SKIP_INSTALL_ALL ON)
    add_subdirectory(${LIBSRC}/freetype ${CMAKE_CURRENT_BINARY_DIR}/freetype EXCLUDE_FROM_ALL)
  endfunction()
  zh_add_freetype()

  # What FontCharsClass asks GDI for, from FreeType: the fonts, their metrics, the glyphs.  Its own
  # library, outside ww3d2's list, so it builds and is tested before WW3D2 does off Windows (D6a).
  # Linux finds Liberation through fontconfig; macOS reads fixed files and needs nothing more.
  add_library(glyphrasteriser STATIC ${WW}/WW3D2/glyphrasteriser.cpp)
  target_include_directories(glyphrasteriser PUBLIC ${WW}/WW3D2)
  target_link_libraries(glyphrasteriser PRIVATE freetype)
  if(ZH_PLATFORM_LINUX)
    find_package(Fontconfig REQUIRED)
    target_link_libraries(glyphrasteriser PRIVATE Fontconfig::Fontconfig)
  endif()

  # The Miles 6.5 surface on miniaudio (C4): the same library name as the XAudio2 one on Windows, so
  # that gameenginedevice, once it builds off Windows, links milesaudio on either and
  # MilesAudioManager.cpp compiles unchanged against it.  Today only the tests below link it here.
  add_library(milesaudio STATIC ${WW}/Miles6/miniaudio/miles_miniaudio.cpp)
  target_include_directories(milesaudio PRIVATE ${WW}/Miles6/include ${WW}/WWLib)
  target_link_libraries(milesaudio PRIVATE miniaudio wwlib PUBLIC Threads::Threads)

  # What SDL3's GPU API can do on this machine: the evidence D2 and D5 need before the backend
  # interface is designed.  Needs a GPU and a display, so it is built but not registered with ctest.
  add_executable(sdl_gpu_probe ${CMAKE_CURRENT_SOURCE_DIR}/Tests/sdl_gpu_probe.cpp)
  target_link_libraries(sdl_gpu_probe PRIVATE SDL3::SDL3-static)

  # The D-spike: one real model and its DDS textures, read from the game's archives, drawn through
  # SDL3's GPU API.  Parses W3D with WWLib's chunkio and w3d_file.h; WW3D2 itself is not built.  Needs
  # a GPU and the game's data, so it is built but not registered with ctest.  Its shaders are
  # generated ahead of time by Tests/w3d_view/shaders/build_shaders.sh and checked in, so building it
  # needs no shader compiler.
  add_executable(w3d_view
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_view/w3d_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_view/w3d_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_view/dds_image.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_view/big_archive.cpp)
  target_include_directories(w3d_view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_view
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
    ${WW}/WW3D2 ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug)
  # --shaders generated draws with the game's own generated programs, compiled as the SDL3 backend
  # will compile them (D3).
  target_link_libraries(w3d_view PRIVATE SDL3::SDL3-static wwmath wwlib wwsaveload wwdebug
    ffvertex engineshader sdl3shadercompile)
endif()

# ---- The shader compilers for the SDL3 GPU target (decision 4, D3) ------------------------------------
# They compile the generators' SDL3 GPU target for Metal and Vulkan.
if(ZH_PLATFORM_POSIX)
  # glslang (HLSL to SPIR-V) and SPIRV-Cross (SPIR-V to MSL), fetched and pinned by vendor.sh, with
  # only the parts decision 4 uses.  Their options are set inside a function so they stay out of the
  # rest of this file, and both are EXCLUDE_FROM_ALL: what gets built is what links against them.
  function(zh_add_shader_compilers)
    # Both read options with the old CMP0077 behaviour, under which option() ignores a normal
    # variable its parent set.  Without this, every setting below would silently not apply.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS OFF)
    set(ENABLE_HLSL ON)
    set(ENABLE_OPT OFF)
    set(ENABLE_SPIRV ON)
    set(ENABLE_GLSLANG_BINARIES OFF)
    set(ENABLE_PCH OFF)
    set(GLSLANG_TESTS OFF)
    set(GLSLANG_ENABLE_INSTALL OFF)
    set(BUILD_EXTERNAL OFF)
    add_subdirectory(${LIBSRC}/glslang ${CMAKE_CURRENT_BINARY_DIR}/glslang EXCLUDE_FROM_ALL)
    set(SPIRV_CROSS_CLI OFF)
    set(SPIRV_CROSS_ENABLE_TESTS OFF)
    set(SPIRV_CROSS_SHARED OFF)
    set(SPIRV_CROSS_STATIC ON)
    set(SPIRV_CROSS_ENABLE_HLSL OFF)
    set(SPIRV_CROSS_ENABLE_CPP OFF)
    set(SPIRV_CROSS_ENABLE_REFLECT OFF)
    set(SPIRV_CROSS_ENABLE_UTIL OFF)
    set(SPIRV_CROSS_ENABLE_MSL ON)
    set(SPIRV_CROSS_ENABLE_GLSL ON)
    set(SPIRV_CROSS_ENABLE_C_API ON)
    set(SPIRV_CROSS_SKIP_INSTALL ON)
    add_subdirectory(${LIBSRC}/SPIRV-Cross ${CMAKE_CURRENT_BINARY_DIR}/SPIRV-Cross EXCLUDE_FROM_ALL)
  endfunction()
  zh_add_shader_compilers()

  # SDL_shadercross is one C file, built here rather than through its own CMakeLists, whose vendored
  # mode insists on DXC's source even with DXC off.  No SDL_SHADERCROSS_DXC: it takes the SPIR-V glslang
  # wrote.  DXIL (and so DXC) is not used.
  add_library(sdl3_shadercross STATIC ${LIBSRC}/SDL_shadercross/src/SDL_shadercross.c)
  target_include_directories(sdl3_shadercross PUBLIC ${LIBSRC}/SDL_shadercross/include)
  target_link_libraries(sdl3_shadercross PUBLIC SDL3::SDL3-static PRIVATE spirv-cross-c)

  # HLSL to SPIR-V to what a device takes, behind the one function that names the HLSL front end.
  add_library(sdl3shadercompile STATIC ${WW}/WW3D2/sdl3shadercompile.cpp)
  target_include_directories(sdl3shadercompile PUBLIC ${WW}/WW3D2)
  target_link_libraries(sdl3shadercompile PUBLIC SDL3::SDL3-static
    PRIVATE sdl3_shadercross glslang::glslang glslang::glslang-default-resource-limits)

endif()

# d3d8to9 is no longer built.  It was a d3d8.dll next to generals.exe that translated
# every D3D8 call to Direct3D 9, which is how the renderer ran on a modern runtime
# without touching the ~5600 D3D8 call sites.  The renderer creates its own
# IDirect3DDevice9 now (RENDERER-ROADMAP.md phase 1), so nothing loads that dll.  The
# vendored source stays in Libraries/Source/d3d8to9: WW3D2/d3d8shadertranslate.cpp is
# lifted from it, and it is the reference for how a D3D8 call was being answered.

# ===========================================================================
# Phase 3 - GameEngine.  ~580 files of game logic; no DX8, no device code.
# ===========================================================================
set(GE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngine)

# The source tree is a frozen 2003 snapshot, so a glob cannot go stale the way
# it would in a live project - and 580 explicit paths would drown this file.
file(GLOB_RECURSE GAMEENGINE_SOURCES ${GE}/Source/*.cpp)
# On disk but absent from GameEngine.dsp, i.e. never in the shipping build:
# dead experiments (SimplePlayer, urlLaunch, VideoStream), an older copy of
# DrawableManager that GameClient/Drawable/ supersedes, and GameSpeech, whose
# hash uses std::hash<const char*> on strings that outlive nothing.
list(REMOVE_ITEM GAMEENGINE_SOURCES
  ${GE}/Source/Common/Audio/GameSpeech.cpp
  ${GE}/Source/Common/Audio/simpleplayer.cpp
  ${GE}/Source/Common/Audio/urllaunch.cpp
  ${GE}/Source/GameClient/DrawableManager.cpp
  ${GE}/Source/GameClient/VideoStream.cpp)
# GameNetwork/GameSpy.h and the two .cpp files that owned it were stripped from
# the release, and these two are all that include it.  Multiplayer is out of
# scope (PLAN.md), so they stay out until the exe link says otherwise.
list(REMOVE_ITEM GAMEENGINE_SOURCES
  ${GE}/Source/GameNetwork/GameSpyChat.cpp
  ${GE}/Source/GameNetwork/GameSpyGP.cpp)
# EA's own header says "#error this file is obsolete", and this .cpp is its only
# includer - the live code went to GameNetwork/GameSpy/ instead.
list(REMOVE_ITEM GAMEENGINE_SOURCES
  ${GE}/Source/GameNetwork/GameSpyGameInfo.cpp)
# Platform bodies of one interface: each side's file is left out of the other's build, so Windows
# gets exactly the sources it had.  B5, bucket (c).
#  - StackDump.cpp: dbghelp symbol lookup and a structured-exception context walk, Windows only.
#    StackDumpPosix.cpp is the same Common/StackDump.h interface from backtrace() and dladdr(), and
#    CrashHandlerPosix.cpp is what a crash signal writes (C5): ReleaseCrashInfo.txt, as Windows'
#    structured-exception path writes it.
#  - WebBrowser.cpp: an ATL/COM wrapper around Internet Explorer for the dead WOL service's pages.
#    Nothing ever creates TheWebBrowser, on Windows either (Win32GameEngine::createWebBrowser has
#    no caller), and every caller - INIWebpageURL, WOLLadderScreen, WOLLoginMenu - tests it for NULL
#    first.  WebBrowserPosix.cpp is the same interface without COM, TheWebBrowser NULL.
#  - DownloadManager.cpp: patch downloads over wwdownload (FTP over winsock; wwdownload itself is a
#    Windows-only target above) from a dead service.  Only DownloadMenuInit creates the manager,
#    and only on Windows; MainMenu and MainMenuUtils test TheDownloadManager for NULL first.
#    DownloadManagerPosix.cpp defines it NULL.  DownloadMenu.cpp itself is built everywhere, because
#    .wnd files bind its callbacks by name through FunctionLexicon, which must be the same on
#    every platform.
#  - WWDownloadRegistryPosix.cpp: WWDownload's registry functions and FormatURLFromRegistry over
#    Registry.ini.  On Windows they come from wwdownload itself.  B6.
#  - IMEManager.cpp: the IMM32 input method editor (Chinese, Japanese and Korean text entry).  Its
#    only exports are TheIMEManager and CreateIMEManagerInterface; GameClient::init handles a NULL
#    manager and every other caller tests for one, and no test calls it.  IMEManagerPosix.cpp is the
#    manager off Windows, over the device's text events (C3): every typed character reaches a text
#    field through the IME manager, on Windows too.  (IMECandidate.cpp, whose window callbacks .wnd files
#    name through FunctionLexicon, is a different file and is built everywhere.)
if(ZH_PLATFORM_WINDOWS)
  list(REMOVE_ITEM GAMEENGINE_SOURCES
    ${GE}/Source/Common/System/StackDumpPosix.cpp
    ${GE}/Source/Common/System/CrashHandlerPosix.cpp
    ${GE}/Source/GameNetwork/WOLBrowser/WebBrowserPosix.cpp
    ${GE}/Source/GameNetwork/DownloadManagerPosix.cpp
    ${GE}/Source/GameClient/GUI/IMEManagerPosix.cpp
    ${GE}/Source/Common/System/WWDownloadRegistryPosix.cpp)
else()
  list(REMOVE_ITEM GAMEENGINE_SOURCES
    ${GE}/Source/Common/System/StackDump.cpp
    ${GE}/Source/GameNetwork/WOLBrowser/WebBrowser.cpp
    ${GE}/Source/GameNetwork/DownloadManager.cpp
    ${GE}/Source/GameClient/GUI/IMEManager.cpp)
endif()

# ZH_NOT_YET_PORTABLE: defined on macOS, kept out of the default build there until B1-B6 land.
# The link list below is left exactly as it is on purpose - B6 owns it.
add_library(gameengine STATIC ${ZH_NOT_YET_PORTABLE} ${GAMEENGINE_SOURCES})
# GameMemory.cpp asks MSVC for the engine library by the build's name (#pragma comment(lib, "GameEngineDebug")
# under _DEBUG), so a Debug build names it that way.  Release keeps gameengine.lib, which "GameEngine" finds.
if(MSVC)
  set_target_properties(gameengine PROPERTIES OUTPUT_NAME_DEBUG GameEngineDebug)
endif()

# N1 (decision 5): the build fingerprint GlobalData.cpp folds into m_exeCRC in place of the executable's
# own bytes, on every platform, so two builds of the same source agree whatever compiled them.  CRC-32 over
# the files BuildFingerprint.manifest lists (the tracked sources: Tools/fingerprint-manifest.sh writes it,
# build_fingerprint_manifest checks it), line endings normalised.  The header is rewritten only when the
# value changes, so a rebuild with nothing changed runs nothing and recompiles nothing.
add_executable(build_fingerprint ${CMAKE_CURRENT_SOURCE_DIR}/Tools/fingerprint/build_fingerprint.cpp)
set(ZH_FINGERPRINT_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/BuildFingerprint.manifest)
set(ZH_FINGERPRINT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/fingerprint)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ZH_FINGERPRINT_MANIFEST})
file(STRINGS ${ZH_FINGERPRINT_MANIFEST} ZH_FINGERPRINT_FILES)
list(TRANSFORM ZH_FINGERPRINT_FILES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
add_custom_command(
  OUTPUT ${ZH_FINGERPRINT_DIR}/BuildFingerprint.stamp
  BYPRODUCTS ${ZH_FINGERPRINT_DIR}/BuildFingerprint.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${ZH_FINGERPRINT_DIR}
  COMMAND build_fingerprint ${CMAKE_CURRENT_SOURCE_DIR} ${ZH_FINGERPRINT_MANIFEST} ${ZH_FINGERPRINT_DIR}/BuildFingerprint.h
  COMMAND ${CMAKE_COMMAND} -E touch ${ZH_FINGERPRINT_DIR}/BuildFingerprint.stamp
  DEPENDS build_fingerprint ${ZH_FINGERPRINT_MANIFEST} ${ZH_FINGERPRINT_FILES}
  COMMENT "Build fingerprint over the tracked sources (N1)" VERBATIM)
add_custom_target(build_fingerprint_header DEPENDS ${ZH_FINGERPRINT_DIR}/BuildFingerprint.stamp)
add_dependencies(gameengine build_fingerprint_header)
target_include_directories(gameengine PRIVATE ${ZH_FINGERPRINT_DIR})
if(ZH_PLATFORM_POSIX)		# bash and perl: the value itself is the same tool on every platform
  # the fingerprint: LF and CRLF copies of the tree agree, one byte or a rename changes it, the header holds it
  add_test(NAME build_fingerprint_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/fingerprint-check.sh $<TARGET_FILE:build_fingerprint>
                   ${ZH_FINGERPRINT_DIR}/BuildFingerprint.h)
  # the manifest still lists exactly the tracked files (skipped without git: a tarball uses it as shipped)
  add_test(NAME build_fingerprint_manifest COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/fingerprint-manifest.sh --check)
  set_tests_properties(build_fingerprint_manifest PROPERTIES SKIP_RETURN_CODE 77)
  # the manifest script on itself, in a scratch repository: a line twice fails --check with its name, and
  # a merge stopped on a conflict (git ls-files then names a path once per stage) is refused
  add_test(NAME fingerprint_manifest_selfcheck COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_fingerprint_manifest.sh)
  set_tests_properties(fingerprint_manifest_selfcheck PROPERTIES SKIP_RETURN_CODE 77)
endif()
if(ZH_PLATFORM_POSIX)
  # GlobalLanguage registers Language.ini's LocalFontFile with the glyph rasteriser (D6b)
  target_link_libraries(gameengine PUBLIC glyphrasteriser)
endif()
target_include_directories(gameengine PUBLIC
  ${GE}/Include ${GE}/Include/Precompiled
  ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
  ${WW} ${WW}/WWLib
  # The game includes "gamespy/peer/peer.h", the SDK's own headers include each
  # other as "gscommon.h", so both levels have to be on the path.
  ${LIBSRC}/GameSpy/include ${LIBSRC}/GameSpy/include/gamespy ${COMP}
  # WW3D2 is here for d3dx9math.h alone: BezierSegment carries D3DXMATRIX, and
  # DumbProjectileBehavior steers a shell with it.  dinput.h now comes out of the
  # Windows SDK like d3d9.h does.
  ${WW}/WW3D2
  # nanosvg's two headers, compiled into HtmlOverlay.cpp alone: the SVG pictures a page names.
  ${LIBSRC}/nanosvg)
# _RELEASE/_DEBUG pick the game's own build flavour; IG_DEBUG_STACKTRACE and
# Z_PREFIX are what GameEngine.dsp sets.
target_compile_definitions(gameengine PUBLIC
  IG_DEBUG_STACKTRACE Z_PREFIX
  $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:_RELEASE>)
# C++17 for the std::hash_map -> unordered_map alias and string_view hashing in
# STLTypedefs.h.
set_target_properties(gameengine PROPERTIES CXX_STANDARD 17)

# setFPMode's POSIX branch must leave floating point as Windows' default MXCSR does: round to nearest,
# denormals kept, NaN payloads propagated.  Built from FPUControl.cpp alone - it lives in its own
# translation unit for exactly this - with the engine's includes and definitions, so it runs before
# gameengine links.  Armed: it first sets the state setFPMode must undo and requires every check to
# fail there.  POSIX only for now; nothing here has run it on Windows.  See Tests/test_fpucontrol.cpp.
if(ZH_PLATFORM_POSIX)
  add_executable(fpucontrol_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_fpucontrol.cpp
                                      ${GE}/Source/GameLogic/System/FPUControl.cpp
                                      ${CMAKE_CURRENT_SOURCE_DIR}/Tests/gcc_eager_vtable_stubs.cpp)
  target_include_directories(fpucontrol_selfcheck PRIVATE
    $<TARGET_PROPERTY:gameengine,INTERFACE_INCLUDE_DIRECTORIES>)
  target_compile_definitions(fpucontrol_selfcheck PRIVATE
    $<TARGET_PROPERTY:gameengine,INTERFACE_COMPILE_DEFINITIONS>
    ZH_EAGER_VTABLE_STUB_STRINGS)		# it links no AsciiString.cpp or UnicodeString.cpp
  set_target_properties(fpucontrol_selfcheck PROPERTIES CXX_STANDARD 17)
endif()
# Two names on this line are Windows-only, and expand to nothing elsewhere - in the same positions,
# so the Windows list is the same items in the same order it always was.  B6's first two drops:
#  - debuglib: no live GameEngine code calls it (every Debug::/DLOG/DASSERT hit is commented out).
#    It still reaches Windows through profile, which does call it.
#  - dinput8: GameEngine uses the 107 DIK_ key-code constants from <dinput.h> and no DirectInput
#    function.  It is load-bearing on Windows all the same: Win32DIKeyboard.cpp and Win32DIMouse.cpp
#    in gameenginedevice call DirectInput8Create and get dinput8 only through this PUBLIC line.
#
# And seven more that exist only on Windows, whose off-Windows parts gameengine gets elsewhere (B6).
# Off Windows none is a target, so each reached the linker as -l<name> and a POSIX link failed on it:
#  - ww3d2: gameengine's references are DX8Wrapper_PreserveFPU and DX8Wrapper_IsWindowed, which
#    PosixDevice defines, and d3dx9math.h's maths, which d3dxportable.h makes header-only (B17).
#  - wwdownload: its registry API and FormatURLFromRegistry, gameengine's own off Windows (B6).
#  - eabrowserdispatch, profile, wininet, imagehlp, imm32: Windows-only code, left out of the POSIX
#    build by B5 (WebBrowserPosix, StackDumpPosix, IMEManagerPosix, ChromaKeyboard's WinINet half).
# A link of gameengine with PosixDevice and the libraries below leaves no symbol any of them defines.
if(ZH_PLATFORM_WINDOWS)
  set(ZH_GAMEENGINE_DEBUGLIB debuglib)
  set(ZH_GAMEENGINE_DINPUT8 dinput8)
  set(ZH_GAMEENGINE_WW3D2 ww3d2)
  set(ZH_GAMEENGINE_WWDOWNLOAD wwdownload)
  set(ZH_GAMEENGINE_EABROWSERDISPATCH eabrowserdispatch)
  set(ZH_GAMEENGINE_PROFILE profile)
  set(ZH_GAMEENGINE_WIN32_SDK_LIBS wininet imagehlp imm32)
endif()
target_link_libraries(gameengine PUBLIC
  ${ZH_GAMEENGINE_WW3D2} wwlib wwmath wwsaveload wwdebug wwutil compression ${ZH_GAMEENGINE_DEBUGLIB} ${ZH_GAMEENGINE_WWDOWNLOAD}
  benchmark ${ZH_GAMEENGINE_EABROWSERDISPATCH} ${ZH_GAMEENGINE_PROFILE} gamespy litehtml
  ${ZH_GAMEENGINE_DINPUT8} ${ZH_GAMEENGINE_WIN32_SDK_LIBS})

# B1's gate: the bytes a UnicodeString puts into the replay and network CRC, before and after
# WideChar becomes char16_t.  Built from the REAL Xfer, XferCRC, UnicodeString, AsciiString and
# WideCharFns sources with gameengine's own include paths and definitions, and nothing else of
# gameengine, so it runs on every platform today rather than when gameengine links.  The
# -fshort-wchar twin gives wchar_t MSVC's two unsigned bytes, which is the Windows model on a
# Mac or Linux box; see Tests/test_widechar_crc_gate.cpp for what the pair proves and does not.
# The real sources both of B1's tests are built from, and the one stub file they share.
set(ZH_WIDECHAR_TEST_SUPPORT
  ${GE}/Source/Common/System/Xfer.cpp ${GE}/Source/Common/System/XferCRC.cpp
  ${GE}/Source/Common/System/UnicodeString.cpp ${GE}/Source/Common/System/AsciiString.cpp
  ${GE}/Source/Common/System/WideCharFns.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Tests/widechar_test_stubs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Tests/gcc_eager_vtable_stubs.cpp)
# XferCRC.cpp, MemoryInit.cpp and their kind open files through WWLib's zh_fopen, which off Windows is
# a function in wwlib (C1 (d)); on Windows it is fopen, inline, and needs nothing linked.
if(ZH_PLATFORM_POSIX)
  set(ZH_ZHIO_LIBS wwlib wwdebug)
endif()
# XferCRC.cpp's htonl is Winsock's on Windows; the game gets ws2_32 through gameengine, these tests
# build XferCRC.cpp themselves (W2).
if(ZH_PLATFORM_WINDOWS)
  set(ZH_XFERCRC_LIBS ws2_32)
endif()
function(zh_widechar_gate name)
  add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_widechar_crc_gate.cpp ${ZH_WIDECHAR_TEST_SUPPORT})
  target_include_directories(${name} PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
  set_target_properties(${name} PROPERTIES CXX_STANDARD 17)
  target_link_libraries(${name} PRIVATE ${ZH_ZHIO_LIBS} ${ZH_XFERCRC_LIBS})
  add_test(NAME ${name} COMMAND ${name})
endfunction()
zh_widechar_gate(widechar_crc_gate)
# The wide-format funnel against MSVC's legacy wide printf: %s wide, %S narrow.  Off Windows it
# checks the hand-written formatter; on Windows the same expected strings check the real
# _vsnwprintf.  See Tests/test_widechar_format.cpp.
add_executable(widechar_format_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_widechar_format.cpp
  ${ZH_WIDECHAR_TEST_SUPPORT})
target_include_directories(widechar_format_selfcheck PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(widechar_format_selfcheck PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(widechar_format_selfcheck PROPERTIES CXX_STANDARD 17)
target_link_libraries(widechar_format_selfcheck PRIVATE ${ZH_ZHIO_LIBS} ${ZH_XFERCRC_LIBS})
add_test(NAME widechar_format_selfcheck COMMAND widechar_format_selfcheck)
# The game's own .csf through the real GameText.cpp, RAMFile, File and memory manager: every string
# against an independent reading, some against their English text, and a golden hash for a Windows
# run to diff.  Needs ZH_GAME_DATA; without it the test is Skipped (77), not Passed.  Stubs are in
# the test file.  See Tests/test_gametext_csf.cpp.
add_executable(gametext_csf ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_gametext_csf.cpp
  ${GE}/Source/GameClient/GameText.cpp ${GE}/Source/Common/System/RAMFile.cpp
  ${GE}/Source/Common/System/File.cpp ${GE}/Source/Common/System/GameMemory.cpp
  ${GE}/Source/Common/System/MemoryInit.cpp ${GE}/Source/Common/System/UnicodeString.cpp
  ${GE}/Source/Common/System/AsciiString.cpp ${GE}/Source/Common/System/WideCharFns.cpp)
target_include_directories(gametext_csf PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(gametext_csf PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(gametext_csf PROPERTIES CXX_STANDARD 17)
target_link_libraries(gametext_csf PRIVATE ${ZH_ZHIO_LIBS})
add_test(NAME gametext_csf COMMAND gametext_csf)
# Strings built by a static constructor before anything starts the memory manager, as file-scope
# LogClass objects build them: the real strings and memory manager, each string type first in one of
# the two.  See Tests/test_premain_strings.cpp.
foreach(name test_premain_strings test_premain_unicode)
  add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_premain_strings.cpp
    ${GE}/Source/Common/System/GameMemory.cpp ${GE}/Source/Common/System/MemoryInit.cpp
    ${GE}/Source/Common/System/UnicodeString.cpp ${GE}/Source/Common/System/AsciiString.cpp
    ${GE}/Source/Common/System/WideCharFns.cpp)
  target_include_directories(${name} PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
  set_target_properties(${name} PROPERTIES CXX_STANDARD 17)
  target_link_libraries(${name} PRIVATE ${ZH_ZHIO_LIBS})
  add_test(NAME ${name} COMMAND ${name})
endforeach()
target_compile_definitions(test_premain_unicode PRIVATE ZH_PREMAIN_UNICODE_FIRST)
# GCC's eager vtables name ~AudioEventRTS from these three; they link the real memory manager and strings,
# so only the audio event is stubbed (Tests/gcc_eager_vtable_stubs.cpp).  POSIX only: MSVC names none of it.
if(ZH_PLATFORM_POSIX)
  foreach(name gametext_csf test_premain_strings test_premain_unicode)
    target_sources(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests/gcc_eager_vtable_stubs.cpp)
    target_compile_definitions(${name} PRIVATE ZH_EAGER_VTABLE_STUB_NO_POOL)
  endforeach()
endif()
# GameMemory.cpp asks MSVC for the game's own library by name (#pragma comment(lib, "GameEngine"), EA's
# way of linking its new and delete first).  These tests build GameMemory.cpp themselves and link no
# GameEngine.lib, so they drop that request (W2).
if(MSVC)
  foreach(name gametext_csf test_premain_strings test_premain_unicode)
    target_link_options(${name} PRIVATE /NODEFAULTLIB:GameEngine.lib $<$<CONFIG:Debug>:/NODEFAULTLIB:GameEngineDebug.lib>)
  endforeach()
endif()
set_tests_properties(gametext_csf PROPERTIES SKIP_RETURN_CODE 77)
# The global operator new against the alignment the ABI promises for it, on Linux, where it is also the
# one Mesa's libLLVM gets.  See Tests/test_global_new_alignment.cpp.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(test_global_new_alignment ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_global_new_alignment.cpp
    ${GE}/Source/Common/System/GameMemory.cpp ${GE}/Source/Common/System/MemoryInit.cpp
    ${GE}/Source/Common/System/UnicodeString.cpp ${GE}/Source/Common/System/AsciiString.cpp
    ${GE}/Source/Common/System/WideCharFns.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Tests/gcc_eager_vtable_stubs.cpp)
  target_include_directories(test_global_new_alignment PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(test_global_new_alignment PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>
    ZH_EAGER_VTABLE_STUB_NO_POOL)
  set_target_properties(test_global_new_alignment PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_global_new_alignment PRIVATE ${ZH_ZHIO_LIBS})
  add_test(NAME test_global_new_alignment COMMAND test_global_new_alignment)
endif()
if(ZH_GAME_DATA)
  set_tests_properties(gametext_csf PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
endif()

# The replay header's wide strings as byte calls, sharing a stream with fwrite and fread (C1, PR (g)).
# See Tests/test_widechar_file.cpp.
add_executable(widechar_file_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_widechar_file.cpp
  ${ZH_WIDECHAR_TEST_SUPPORT})
target_include_directories(widechar_file_selfcheck PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(widechar_file_selfcheck PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(widechar_file_selfcheck PROPERTIES CXX_STANDARD 17)
target_link_libraries(widechar_file_selfcheck PRIVATE test_harness ${ZH_ZHIO_LIBS} ${ZH_XFERCRC_LIBS})
add_test(NAME widechar_file_selfcheck COMMAND widechar_file_selfcheck)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  zh_widechar_gate(widechar_crc_gate_msvc_model)
  target_compile_options(widechar_crc_gate_msvc_model PRIVATE -fshort-wchar)
  # The five wcs* functions UnicodeString.cpp calls, over two-byte units as MSVC's CRT has them:
  # this C library was built with four-byte wchar_t and would overrun the heap.
  target_sources(widechar_crc_gate_msvc_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests/widechar_gate_msvc_crt.cpp)
endif()

# ===========================================================================
# PosixDevice - the POSIX platform layer beside Win32Device (C1), a file at a time
# ===========================================================================
# The Win32Device classes' POSIX counterparts, for macOS and Linux alike; what only macOS has goes
# in MacDevice.  Out of the default build on macOS for as long as gameengine is (ZH_NOT_YET_PORTABLE),
# so drive it with --target.
if(ZH_PLATFORM_POSIX)
  file(GLOB_RECURSE POSIXDEVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/*.cpp)
  # PosixDevice/Render is the Direct3D 9-shaped device (decision 7), a library of its own below: WW3D2
  # links it the way it links d3d9.lib on Windows, and posixdevice needs none of it.
  list(FILTER POSIXDEVICE_SOURCES EXCLUDE REGEX "/PosixDevice/Render/")
  # PosixDevice/Install is where the player's Zero Hour is (P1 step 4): plain logic over the file system
  # and Registry.ini, no engine state, so a library of its own that test_install_root links alone.
  list(FILTER POSIXDEVICE_SOURCES EXCLUDE REGEX "/PosixDevice/Install/")
  add_library(installroot STATIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Install/PosixInstallRoot.cpp)
  target_include_directories(installroot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngine/Include)
  set_target_properties(installroot PROPERTIES CXX_STANDARD 17)
  target_link_libraries(installroot PUBLIC wwlib)
  # The BIG archive reader is Win32Device's own, built here as it is (C1 (f)): it has no Win32 in it but
  # ntohl and one message box, both behind #if, and one copy keeps the mount order - which archive wins
  # which path, and so the INI CRC - the same on every platform.
  list(APPEND POSIXDEVICE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/Win32Device/Common/Win32BIGFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp)
  add_library(posixdevice STATIC ${ZH_NOT_YET_PORTABLE} ${POSIXDEVICE_SOURCES})
  target_include_directories(posixdevice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
  set_target_properties(posixdevice PROPERTIES CXX_STANDARD 17)
  target_link_libraries(posixdevice PUBLIC gameengine)
endif()

# ===========================================================================
# The renderer off Windows (decision 7): WW3D2 and W3DDevice against a Direct3D 9-shaped device
# ===========================================================================
# WW3D2 and W3DDevice compile unchanged in their D3D9 calls: <d3d9.h> is Platform/PosixD3D9's, which is
# Platform/D3D9Posix.h, and the device behind it is posixd3d9.  Phase A1 builds and links them; the
# device has no resources until A2 and no draw until A3.  All of it is in the default build, the link
# probe included, so a change that breaks the renderer off Windows fails the Mac build.
if(ZH_PLATFORM_POSIX)
  set(ZH_POSIX_D3D9_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include/Platform/PosixD3D9)

  # ---- d3dx9runtime: the same library name as Windows's, with d3dx9posix.cpp's bodies ---------------
  # d3dx9posix_texture.cpp is phase A2's texture helpers, d3dx9posix.cpp the rest.
  add_library(d3dx9runtime STATIC
    ${WW}/WW3D2/d3dx9posix.cpp ${WW}/WW3D2/d3dx9posix_texture.cpp ${WW}/WW3D2/d3d8shadertranslate.cpp
    ${WW}/WW3D2/d3dx9portable.cpp)
  set_target_properties(d3dx9runtime PROPERTIES CXX_STANDARD 17)
  target_include_directories(d3dx9runtime PUBLIC ${WW}/WW3D2 ${ZH_POSIX_D3D9_INCLUDE}
    # The texture helpers are CPU work over the device's own images (PosixImageOps), so they see
    # the device's headers - privately: nothing else outside the device does.
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render)
  target_link_libraries(d3dx9runtime PRIVATE posixd3d9)

  # The matrix functions and the FVF size against independent answers, and the binding's contract;
  # the header says what it does not prove.
  add_executable(d3dx9posix_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_d3dx9posix.cpp)
  set_target_properties(d3dx9posix_selfcheck PROPERTIES CXX_STANDARD 17)
  target_include_directories(d3dx9posix_selfcheck PRIVATE ${WW}/WWLib ${WW}/WWDebug ${WW}/WWMath)
  target_link_libraries(d3dx9posix_selfcheck PRIVATE d3dx9runtime)
  add_test(NAME d3dx9posix_selfcheck COMMAND d3dx9posix_selfcheck)
  # A3e-asm (b): the port's D3DXAssembleShader carries each water text to the device exactly (decision:
  # it is not an assembler).  Microsoft's tokens for the same texts: d3dx_assemble_oracle.
  add_executable(d3dx_assemble_stub_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_d3dx_assemble_stub.cpp)
  set_target_properties(d3dx_assemble_stub_selfcheck PROPERTIES CXX_STANDARD 17)
  target_include_directories(d3dx_assemble_stub_selfcheck PRIVATE ${WW}/WWLib ${WW}/WWDebug ${WW}/WWMath)
  target_compile_definitions(d3dx_assemble_stub_selfcheck PRIVATE ZH_CODE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(d3dx_assemble_stub_selfcheck PRIVATE d3dx9runtime)
  add_test(NAME d3dx_assemble_stub_selfcheck COMMAND d3dx_assemble_stub_selfcheck)

  # ---- posixd3d9: the device, in the place d3d9.lib has on Windows -----------------------------------
  file(GLOB_RECURSE POSIXD3D9_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render/*.cpp)
  add_library(posixd3d9 STATIC ${POSIXD3D9_SOURCES})
  set_target_properties(posixd3d9 PROPERTIES CXX_STANDARD 17)
  target_include_directories(posixd3d9 PUBLIC ${ZH_POSIX_D3D9_INCLUDE})
  # A3: with a window the device draws through SDL3 GPU, and compiles its own small programs (the gamma
  # pass) the way the generated ones are compiled.
  target_link_libraries(posixd3d9 PRIVATE SDL3::SDL3-static sdl3shadercompile engineshader ffshader ffvertex)

  # A3a's frame: a clear read back, and the gamma pass through a picture that shows any flip.  Needs a GPU
  # but no window; exits 77 (skipped, and ctest says so) where SDL has no GPU device to give.
  add_executable(sdl_gpu_frame_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_sdl_gpu_frame.cpp)
  set_target_properties(sdl_gpu_frame_selfcheck PROPERTIES CXX_STANDARD 17)
  target_include_directories(sdl_gpu_frame_selfcheck PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render)
  target_link_libraries(sdl_gpu_frame_selfcheck PRIVATE posixd3d9 SDL3::SDL3-static)
  add_test(NAME sdl_gpu_frame_selfcheck COMMAND sdl_gpu_frame_selfcheck)

  # A3c's resolve: the device's state into the generators' descriptions and the constants their
  # programs read, on a headless device.  No GPU.
  add_executable(posix_draw_resolve_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_posix_draw_resolve.cpp)
  set_target_properties(posix_draw_resolve_selfcheck PROPERTIES CXX_STANDARD 17)
  target_include_directories(posix_draw_resolve_selfcheck PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render)
  target_link_libraries(posix_draw_resolve_selfcheck PRIVATE posixd3d9 ffshader ffvertex)
  add_test(NAME posix_draw_resolve_selfcheck COMMAND posix_draw_resolve_selfcheck)

  # A3c's program cache: generated programs compiled for this machine's GPU, every combiner operation
  # the generator implements and every light type.  Exits 77 (skipped) with no GPU.
  add_executable(sdl_program_cache_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_sdl_program_cache.cpp)
  set_target_properties(sdl_program_cache_selfcheck PROPERTIES CXX_STANDARD 17)
  target_include_directories(sdl_program_cache_selfcheck PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render)
  target_link_libraries(sdl_program_cache_selfcheck PRIVATE posixd3d9 ffshader ffvertex SDL3::SDL3-static)
  add_test(NAME sdl_program_cache_selfcheck COMMAND sdl_program_cache_selfcheck)
  set_tests_properties(sdl_program_cache_selfcheck PROPERTIES SKIP_RETURN_CODE 77)

  # A3c's pipelines and samplers: the D3D9 state translations, with every engine vertex format's stride
  # against D3DX's own size; then pipelines and samplers on this machine's GPU (77 without one).
  add_executable(sdl_pipeline_state_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_sdl_pipeline_state.cpp)
  set_target_properties(sdl_pipeline_state_selfcheck PROPERTIES CXX_STANDARD 17)
  target_include_directories(sdl_pipeline_state_selfcheck PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render ${WW}/WWLib ${WW}/WWDebug ${WW}/WWMath)
  target_link_libraries(sdl_pipeline_state_selfcheck PRIVATE posixd3d9 d3dx9runtime ffshader ffvertex SDL3::SDL3-static)
  add_test(NAME sdl_pipeline_state_selfcheck COMMAND sdl_pipeline_state_selfcheck)
  set_tests_properties(sdl_pipeline_state_selfcheck PROPERTIES SKIP_RETURN_CODE 77)

  # A3c's draws: through the device's D3D9 calls onto this machine's GPU, read back from the back buffer
  # (77 without a GPU).
  add_executable(posix_gpu_draw_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_posix_gpu_draw.cpp)
  set_target_properties(posix_gpu_draw_selfcheck PROPERTIES CXX_STANDARD 17)
  target_include_directories(posix_gpu_draw_selfcheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render)
  target_link_libraries(posix_gpu_draw_selfcheck PRIVATE posixd3d9 SDL3::SDL3-static)
  add_test(NAME posix_gpu_draw_selfcheck COMMAND posix_gpu_draw_selfcheck)
  set_tests_properties(posix_gpu_draw_selfcheck PROPERTIES SKIP_RETURN_CODE 77)

  # A3b's harness: the device's draws on this machine's GPU against FFReference, scenario by scenario,
  # with two armed controls that must fail (77 without a GPU).
  add_executable(ffref_gpu_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_ffref_gpu.cpp)
  set_target_properties(ffref_gpu_selfcheck PROPERTIES CXX_STANDARD 17)
  target_include_directories(ffref_gpu_selfcheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render)
  target_link_libraries(ffref_gpu_selfcheck PRIVATE posixd3d9 ffreference SDL3::SDL3-static)
  add_test(NAME ffref_gpu_selfcheck COMMAND ffref_gpu_selfcheck)
  set_tests_properties(ffref_gpu_selfcheck PROPERTIES SKIP_RETURN_CODE 77)

  # The game's own draws against FFReference: ZH_GPU_CAPTURE's captures, replayed one by one (77 without
  # ZH_FFREF_CAPTURE_DIR; the captures come from the user's install and stay out of the repository).
  add_executable(ffref_capture_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_ffref_capture.cpp)
  set_target_properties(ffref_capture_selfcheck PROPERTIES CXX_STANDARD 17)
  target_include_directories(ffref_capture_selfcheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render)
  target_link_libraries(ffref_capture_selfcheck PRIVATE posixd3d9 ffreference SDL3::SDL3-static)
  add_test(NAME ffref_capture_selfcheck COMMAND ffref_capture_selfcheck)
  set_tests_properties(ffref_capture_selfcheck PROPERTIES SKIP_RETURN_CODE 77)
  set_tests_properties(sdl_gpu_frame_selfcheck PROPERTIES SKIP_RETURN_CODE 77)

  # A2's half of the device: the resources, the pixel codec, the image operations, the device's
  # resource methods and Clear, the caps, and D3DX's texture helpers, against D3D9's and D3DX's rules.  See Tests/test_posix_resources.cpp.
  add_executable(test_posix_resources ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_posix_resources.cpp)
  target_include_directories(test_posix_resources PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render)
  set_target_properties(test_posix_resources PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_posix_resources PRIVATE posixd3d9 d3dx9runtime ffshader test_harness)
  add_test(NAME test_posix_resources COMMAND test_posix_resources)

  # ---- ww3d2 -----------------------------------------------------------------------------------------
  add_library(ww3d2 STATIC ${ZH_WW3D2_SOURCES} ${WW}/WW3D2/dx11runtime_posix.cpp)
  set_target_properties(ww3d2 PROPERTIES CXX_STANDARD 17)
  # FontCharsClass draws its glyphs with FreeType through it (D6b)
  target_link_libraries(ww3d2 PRIVATE glyphrasteriser)
  target_include_directories(ww3d2
    PUBLIC ${WW}/WW3D2 ${ZH_POSIX_D3D9_INCLUDE}
    PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil
            ${WW}/wwshade ${WW}/WWAudio ${WW}/Miles6/include)
  target_link_libraries(ww3d2 PUBLIC posixd3d9 d3dx9runtime ffshader
    PRIVATE wwlib wwmath wwdebug wwsaveload)

  # ---- w3ddevice: GameEngineDevice's W3DDevice, gameenginedevice's renderer half ---------------------
  file(GLOB_RECURSE W3DDEVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/W3DDevice/*.cpp)
  # W3DMouse sits on SDL3's mouse off Windows (C3b), and W3DGameClient makes it and SDL3's keyboard, so
  # w3ddevice takes sdlinput: the input half of the SDL layer, apart from sdldevice so that sdldevice can
  # take w3ddevice for the engine's W3D factories without the two needing each other.
  # W3DWebBrowser is the embedded browser as a COM object, which only Win32GameEngine makes; GameEngine
  # has its own WebBrowser off Windows (B5).
  list(FILTER W3DDEVICE_SOURCES EXCLUDE REGEX "/W3DDevice/GameClient/W3DWebBrowser\\.cpp$")

  # ---- the movies (V1): Windows' Bink player and its Bink API on FFmpeg, the same two files -------------
  # FFmpeg is built from the tarball Tools/vendor.sh keeps, into the build tree, with the Bink demuxer
  # and decoders only; the script deletes its 110MB of unpacked source afterwards (it says why).  It
  # reruns when the tarball or the script changes, and takes ~30s.
  set(FFMPEG_TARBALL ${LIBSRC}/FFmpeg/ffmpeg-8.1.2.tar.xz)
  if(NOT EXISTS ${FFMPEG_TARBALL})
    message(FATAL_ERROR "The FFmpeg source tarball is missing from Libraries/Source/FFmpeg - run Tools/vendor.sh.")
  endif()
  set(FFMPEG_POSIX ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg)
  # Link order: each before what it uses
  set(FFMPEG_POSIX_LIBS ${FFMPEG_POSIX}/lib/libavformat.a ${FFMPEG_POSIX}/lib/libavcodec.a
    ${FFMPEG_POSIX}/lib/libswscale.a ${FFMPEG_POSIX}/lib/libswresample.a ${FFMPEG_POSIX}/lib/libavutil.a)
  set(FFMPEG_POSIX_CFLAGS "")
  if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
    set(FFMPEG_POSIX_CFLAGS "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
  # The SDK CMake gives our own targets.  Only /usr/bin/cc, the xcrun shim, finds one by itself; CMake may
  # choose the toolchain's own cc (/Library/Developer/CommandLineTools/usr/bin/cc, as on one build Mac here),
  # and FFmpeg's configure then cannot link its test program ("library 'System' not found") nor compile its
  # host tools ("ctype.h not found").  SDKROOT reaches every compiler call configure makes - target and
  # host alike - where an -isysroot flag would reach only the target's (and -arch must not reach the host).
  set(FFMPEG_POSIX_ENV "")
  if(APPLE AND CMAKE_OSX_SYSROOT)
    set(FFMPEG_POSIX_ENV "SDKROOT=${CMAKE_OSX_SYSROOT}")
  endif()
  # FFmpeg's own configure picks the host's architecture, so a build for another one (x86_64 on Apple
  # Silicon, run under Rosetta: E1's architecture axis) names it.  One at a time: configure builds one.
  set(FFMPEG_POSIX_ARCH "")
  if(APPLE AND CMAKE_OSX_ARCHITECTURES)
    list(LENGTH CMAKE_OSX_ARCHITECTURES FFMPEG_ARCH_COUNT)
    if(NOT FFMPEG_ARCH_COUNT EQUAL 1)
      message(FATAL_ERROR "FFmpeg builds one architecture at a time; CMAKE_OSX_ARCHITECTURES is '${CMAKE_OSX_ARCHITECTURES}'.")
    endif()
    string(STRIP "${FFMPEG_POSIX_CFLAGS} -arch ${CMAKE_OSX_ARCHITECTURES}" FFMPEG_POSIX_CFLAGS)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
      set(FFMPEG_POSIX_ARCH aarch64)
    else()
      set(FFMPEG_POSIX_ARCH ${CMAKE_OSX_ARCHITECTURES})
    endif()
  endif()
  add_custom_command(
    OUTPUT ${FFMPEG_POSIX_LIBS} ${FFMPEG_POSIX}/include/libavcodec/avcodec.h
    COMMAND ${CMAKE_COMMAND} -E env ${FFMPEG_POSIX_ENV}
            ${CMAKE_CURRENT_SOURCE_DIR}/Tools/ffmpeg-build-posix.sh ${FFMPEG_TARBALL}
            ${CMAKE_CURRENT_BINARY_DIR}/ffmpeg-work ${FFMPEG_POSIX} ${CMAKE_C_COMPILER} "${FFMPEG_POSIX_CFLAGS}"
            "${FFMPEG_POSIX_ARCH}"
    DEPENDS ${FFMPEG_TARBALL} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/ffmpeg-build-posix.sh
    COMMENT "Building FFmpeg 8.1.2's Bink decoders for the movies (V1)"
    VERBATIM)
  add_custom_target(ffmpeg_posix DEPENDS ${FFMPEG_POSIX_LIBS} ${FFMPEG_POSIX}/include/libavcodec/avcodec.h)

  # bink_ffmpeg.cpp: the XAudio2 half is Windows'; off Windows the sound is a voice in C4's mix
  # (miles_miniaudio's mss_ex_pcm.h), so this links milesaudio where Windows links xaudio2.
  add_library(binkvideo STATIC ${WW}/Bink/ffmpeg/bink_ffmpeg.cpp)
  add_dependencies(binkvideo ffmpeg_posix)
  target_include_directories(binkvideo PUBLIC ${WW}/Bink/include
    PRIVATE ${FFMPEG_POSIX}/include ${WW}/Miles6/include ${WW}/WWLib)
  target_link_libraries(binkvideo PRIVATE ${FFMPEG_POSIX_LIBS} milesaudio wwlib m Threads::Threads)

  # BinkVideoPlayer.cpp, unchanged: W3DGameClient makes it on every platform, so it lives with w3ddevice
  list(APPEND W3DDEVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/VideoDevice/Bink/BinkVideoPlayer.cpp)
  add_library(w3ddevice STATIC ${W3DDEVICE_SOURCES})
  set_target_properties(w3ddevice PROPERTIES CXX_STANDARD 17)
  target_include_directories(w3ddevice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Stubs ${CMAKE_CURRENT_SOURCE_DIR}/Main
            ${WW}/Miles6/include ${WW}/Bink/include)
  target_link_libraries(w3ddevice PUBLIC gameengine ww3d2 sdlinput binkvideo)

  # A1's link checkpoint: all of w3ddevice and ww3d2, whole archive, against what the POSIX game links.
  add_executable(w3d_link_probe ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_link_probe.cpp)
  set_target_properties(w3d_link_probe PROPERTIES CXX_STANDARD 17)
  target_link_libraries(w3d_link_probe PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,w3ddevice,ww3d2>" posixdevice gameengine)

  # A2's checkpoint: every texture of the install through WW3D2's loader into the POSIX device on a
  # null-window (-headless) WW3D, read back and checked against the file.  Needs ZH_GAME_DATA, else Skipped.
  # The .wak loader's rules: a wave type from the wave table, and no more tracks than the file holds.
  add_executable(test_water_tracks ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_water_tracks.cpp)
  set_target_properties(test_water_tracks PROPERTIES CXX_STANDARD 17)
  target_include_directories(test_water_tracks PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug)
  target_link_libraries(test_water_tracks PRIVATE w3ddevice ww3d2 posixdevice gameengine test_harness)
  add_test(NAME test_water_tracks COMMAND test_water_tracks)

  # W3DRoadBuffer's column count: at least two, never longer than the map.  See Tests/test_road_buffer.cpp.
  add_executable(test_road_buffer ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_road_buffer.cpp)
  set_target_properties(test_road_buffer PROPERTIES CXX_STANDARD 17)
  target_include_directories(test_road_buffer PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug)
  target_link_libraries(test_road_buffer PRIVATE w3ddevice ww3d2 posixdevice gameengine test_harness)
  add_test(NAME test_road_buffer COMMAND test_road_buffer)

  add_executable(test_install_textures ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_install_textures.cpp)
  set_target_properties(test_install_textures PROPERTIES CXX_STANDARD 17)
  target_include_directories(test_install_textures PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug)
  target_link_libraries(test_install_textures PRIVATE w3ddevice ww3d2 posixdevice gameengine test_harness)
  add_test(NAME test_install_textures COMMAND test_install_textures)
  set_tests_properties(test_install_textures PROPERTIES SKIP_RETURN_CODE 77)
  if(ZH_GAME_DATA)
    set_tests_properties(test_install_textures PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()

  # The renderer's hooks gameengine calls, W3DDevice's own off Windows as on it, and decision 2 (B19 (c))
  # in W3DShaderManager's testMinimumRequirements, checked against the shipped GameLODPresets.ini when
  # ZH_GAME_DATA is set.  See Tests/test_render_hooks.cpp.
  add_executable(test_render_hooks ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_render_hooks.cpp)
  set_target_properties(test_render_hooks PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_render_hooks PRIVATE w3ddevice ww3d2 posixdevice gameengine)
  add_test(NAME test_render_hooks COMMAND test_render_hooks)
  if(ZH_GAME_DATA)
    set_tests_properties(test_render_hooks PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
endif()

# ===========================================================================
# Phase 4 - GameEngineDevice + Main -> generals.exe
# ===========================================================================
# All of Phase 4 is Windows: Win32Device is the platform layer, the audio and video backends are
# XAudio2 and a Windows FFmpeg build linked from .lib files, and generals.exe itself is a WIN32
# subsystem binary started from WinMain.  C1 through C4 build the macOS equivalents, one at a
# time, and this guard is what they open up.  The block is left unindented so that the diff which
# added the guard is the guard.
# ---- The version headers, for generals on every platform ---------------------
# BuildVersion.h and GeneratedVersion.h are normally written by Tools/
# buildVersionUpdate and Tools/versionUpdate as a pre-build step, from a build
# counter in the source tree.  Both are three-line headers, so write them at
# configure time instead - it keeps two more VC6 tools out of the port, and
# Windows wants UAC elevation for exe names containing "version" anyway.
set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(WRITE ${GENERATED}/BuildVersion.h
"// Generated by CMake, standing in for Tools/buildVersionUpdate.\n"
"#define VERSION_STRING \"v2.6.2\"\n"
"#define VERSION_MAJOR 2\n"
"#define VERSION_MINOR 6\n"
"#define VERSION_BUILDNUM 2\n")
file(WRITE ${GENERATED}/GeneratedVersion.h
"// Generated by CMake, standing in for Tools/versionUpdate.\n"
"#define LOCAL_BUILD_STRING \"0\"\n"
"#define VERSION_LOCALBUILDNUM 0\n"
"#define VERSION_BUILDUSER \"cmake\"\n"
"#define VERSION_BUILDLOC \"${CMAKE_HOST_SYSTEM_NAME}\"\n")

if(ZH_PLATFORM_WINDOWS)
set(GED ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice)
set(STUBS ${CMAKE_CURRENT_SOURCE_DIR}/Stubs)

file(GLOB_RECURSE GAMEENGINEDEVICE_SOURCES ${GED}/Source/*.cpp)
# On disk but absent from GameEngineDevice.dsp: the DirectInput mouse (the
# shipping build uses the Win32 message mouse instead).
list(REMOVE_ITEM GAMEENGINEDEVICE_SOURCES
  ${GED}/Source/Win32Device/GameClient/Win32DIMouse.cpp)
# PosixDevice is the POSIX platform layer (C1), built below as posixdevice, never on Windows.
list(FILTER GAMEENGINEDEVICE_SOURCES EXCLUDE REGEX "/Source/PosixDevice/")
# SdlDevice is the same for SDL3 (C2, C3), built below as sdldevice, never on Windows either; the glob
# above took it in, and W2's first MSVC build found no SDL3 to compile it against.
list(FILTER GAMEENGINEDEVICE_SOURCES EXCLUDE REGEX "/Source/SdlDevice/")
# Sound and video are the game's own.  Miles and Bink were binary-only, and the DLLs the retail
# game ships are 32-bit, so there is nothing for a 64-bit exe to bind to: what it links instead is
# those two APIs written over XAudio2 and FFmpeg.  FFmpeg decodes binkvideo and binkaudio natively,
# so the .bik files on disk play as they are, and swscale converts each frame into whatever surface
# format the video player asks for.  MilesAudioManager.cpp and BinkVideoPlayer.cpp are unchanged and
# still call AIL_* and Bink*; the reconstructed headers in Miles6/include and Bink/include are still
# the declarations.  miles_smoke and bink_smoke are the checks.
# Windows on ARM64 links its own build, dist-arm64: Tools/ffmpeg-build.bat arm64 makes it.
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
  set(FFMPEG_DIR ${LIBSRC}/FFmpeg/dist-arm64)
else()
  set(FFMPEG_DIR ${LIBSRC}/FFmpeg/dist)
endif()
if(NOT EXISTS ${FFMPEG_DIR}/include)
  message(FATAL_ERROR
    "FFmpeg is missing from ${FFMPEG_DIR} - see the vendored-libs rule (ARM64: Tools/ffmpeg-build.bat arm64).")
endif()

set(FFMPEG_CORE
  ${FFMPEG_DIR}/lib/avformat.lib ${FFMPEG_DIR}/lib/avcodec.lib ${FFMPEG_DIR}/lib/avutil.lib
  ${FFMPEG_DIR}/lib/swresample.lib)

add_library(binkvideo STATIC ${WW}/Bink/ffmpeg/bink_ffmpeg.cpp)
target_include_directories(binkvideo PRIVATE ${WW}/Bink/include ${FFMPEG_DIR}/include)
target_link_libraries(binkvideo PRIVATE ${FFMPEG_CORE} ${FFMPEG_DIR}/lib/swscale.lib xaudio2)

add_library(milesaudio STATIC ${WW}/Miles6/xaudio2/miles_xaudio2.cpp)
target_include_directories(milesaudio PRIVATE ${WW}/Miles6/include ${FFMPEG_DIR}/include)
target_link_libraries(milesaudio PRIVATE ${FFMPEG_CORE} xaudio2)

add_library(gameenginedevice STATIC ${GAMEENGINEDEVICE_SOURCES})

target_include_directories(gameenginedevice PUBLIC
  ${GED}/Include ${STUBS} ${CMAKE_CURRENT_SOURCE_DIR}/Main
  ${WW}/Miles6/include ${WW}/Bink/include)
set_target_properties(gameenginedevice PROPERTIES CXX_STANDARD 17)
target_link_libraries(gameenginedevice PUBLIC gameengine ww3d2 milesaudio binkvideo)

# ---- generals.exe ---------------------------------------------------------
add_executable(generals WIN32 ${CMAKE_CURRENT_SOURCE_DIR}/Main/WinMain.cpp
                             ${CMAKE_CURRENT_SOURCE_DIR}/Main/RTS.RC)
target_include_directories(generals PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Main ${GENERATED})
# A .map turns the "fault offset" in the Windows Application Error event into a
# function name without a debugger: find the last entry whose RVA is <= the offset.
target_link_options(generals PRIVATE /MAP)
# /LARGEADDRESSAWARE used to be here, asking 64-bit Windows for the whole 4GB a 32-bit process can
# address.  An eight-player Merciless match on Twilight Flame was already at 978MB of it.  The
# ceiling it was working against does not exist in this build.
set_target_properties(generals PROPERTIES CXX_STANDARD 17)
target_link_libraries(generals PRIVATE gameenginedevice
  winmm dsound wsock32 ole32 oleaut32 uuid)
# The game reads its data relative to the working directory, so it has to sit
# beside Run/'s .big files to find anything (Phase 5).
add_custom_command(TARGET generals POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:generals> ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # The PDB has to sit next to the exe: SymInitialize searches the exe's directory,
  # and without it the crash log's stack is addresses with no names.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_PDB_FILE:generals> ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # The fork's INI files: the key bindings and their labels, and the detonation
  # light loaded after EA's FXList.ini.  Run/ is gitignored, so these used to be
  # hand-made files that silently went stale - the tracked copies under Code/Data
  # are the masters and this puts them where the game reads them.  A loose file
  # beats every .big and the game never writes one back, so overwriting them on
  # each build is safe.  The whole directory, so a new master is copied without a
  # line here: anything in it lands in the multiplayer INI CRC, and package.bat
  # ships the same directory and refuses a Run/ that disagrees with it.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/INI
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/INI
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Patch.str
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/
  # The skirmish AI's scripts with the USA Hard opening rewritten by Tools/ScbTool/opening.py.  The
  # file is in the multiplayer checksum (GlobalData.cpp), so it ships with the exe like the INIs.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Scripts
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/Scripts
  # The translations the Language option lays over the English strings, one directory per
  # language.  Strings are not in the multiplayer INI checksum: two players can read one match in
  # two languages.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Turkish
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/Turkish
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/German
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/German
  # The startup splash.  WinMain loads it from a loose file next to the exe, so
  # the fork's own artwork is a tracked master here rather than a hand-made file
  # in gitignored Run/.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Install_Final.bmp
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # The three command bar plates per side, the smoke signals' ground marks and the
  # SupW uplink's pink laser strip (Tools/pink_laser.py) and the attack, hold and move cameos
  # (Tools/order_cameos.py).  A loose
  # texture beats every .big, so these land in Run/Art/Textures where the W3D file
  # system looks first.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Art/Textures
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Art/Textures
  # The fork's window layouts, generated from the shipped ones by Tools/*.py and
  # tracked here.  They live outside Data/ in the archive (Window/Menus/...), and
  # a loose file wins over WindowZH.big.  Layouts are not in the multiplayer INI
  # checksum, so an out of date copy costs a wrong looking screen, not a refused
  # join.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Window
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Window
  # -scenario reads these. They are a measuring tool rather than game data: outside Data/ so they
  # cannot collide with an archive path, and outside the multiplayer INI checksum because nothing
  # loads them with &xferCRC. They are not in package.bat for the same reason - a player has no use
  # for one, and a scenario run cannot be replayed anyway.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Scenarios
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Scenarios
  # -cinema reads these shot lists. Client-side only, never checksummed, and a tool for making
  # footage rather than anything a player needs, so they stay out of package.bat too.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Cinema
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Cinema)

# The audio and video backends decode with FFmpeg, so its DLLs have to sit next to the exe.  These
# are the five the two backends link against; avdevice and avfilter are not used.
set(FFMPEG_RUNTIME
  ${FFMPEG_DIR}/bin/avcodec-62.dll ${FFMPEG_DIR}/bin/avformat-62.dll ${FFMPEG_DIR}/bin/avutil-60.dll
  ${FFMPEG_DIR}/bin/swresample-6.dll ${FFMPEG_DIR}/bin/swscale-9.dll)
add_custom_command(TARGET generals POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${FFMPEG_RUNTIME} ${CMAKE_CURRENT_SOURCE_DIR}/../Run/)
# The Direct3D 11 programs that ship with the game, compiled by Microsoft's d3dcompiler_47 when they were
# recorded (Tools/record-dx11-shaders.ps1): next to the exe, where the backend reads them before compiling
# anything.  Without them a first start compiles every program, and under Wine that took minutes (X2).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Data/dx11shaders.shipped)
  add_custom_command(TARGET generals POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/Data/dx11shaders.shipped ${CMAKE_CURRENT_SOURCE_DIR}/../Run/)
endif()
endif()  # ZH_PLATFORM_WINDOWS - GameEngineDevice, the backends and generals.exe

# ===========================================================================
# The game off Windows (C2): SdlDevice and the POSIX entry point
# ===========================================================================
# SdlGameEngine is PosixGameEngine (C1) with SDL3's window and events under it; Main/PosixMain.cpp is
# WinMain's start-up sequence without the Windows.  Its factories are Win32GameEngine's W3D classes
# (decision 8), so sdldevice takes w3ddevice as gameenginedevice holds W3DDevice on Windows.
if(ZH_PLATFORM_POSIX)
  # Windows' audio manager, unchanged but for its guarded non-AIL remainder, over the Miles surface on
  # miniaudio (C4's upper half): gameenginedevice's audio half, as A1's w3ddevice is its renderer half.
  # w3ddevice's glob is W3DDevice/ only, so this file is compiled here and nowhere else off Windows.
  add_library(milesaudiomanager STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/MilesAudioDevice/MilesAudioManager.cpp)
  target_include_directories(milesaudiomanager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include
    ${WW}/Miles6/include)
  set_target_properties(milesaudiomanager PROPERTIES CXX_STANDARD 17)
  target_link_libraries(milesaudiomanager PUBLIC gameengine milesaudio)

  # SDL3's input (C3): the keyboard, the mouse, the text input's hooks, the .ANI cursors.  Its own
  # library because w3ddevice's W3DGameClient and W3DMouse need it, and sdldevice needs w3ddevice.
  file(GLOB SDLINPUT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/SdlDevice/GameClient/*.cpp)
  add_library(sdlinput STATIC ${SDLINPUT_SOURCES})
  target_include_directories(sdlinput PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
  set_target_properties(sdlinput PROPERTIES CXX_STANDARD 17)
  target_link_libraries(sdlinput PUBLIC gameengine SDL3::SDL3-static)

  file(GLOB_RECURSE SDLDEVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/SdlDevice/*.cpp)
  list(FILTER SDLDEVICE_SOURCES EXCLUDE REGEX "/SdlDevice/GameClient/")
  add_library(sdldevice STATIC ${SDLDEVICE_SOURCES})
  target_include_directories(sdldevice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Stubs)
  set_target_properties(sdldevice PROPERTIES CXX_STANDARD 17)
  target_link_libraries(sdldevice PUBLIC posixdevice w3ddevice SDL3::SDL3-static milesaudiomanager sdlinput)
  if(APPLE)
    target_link_libraries(sdldevice PRIVATE "-framework CoreGraphics")		# SdlDisplays' panel size
  endif()

  add_executable(generals ${CMAKE_CURRENT_SOURCE_DIR}/Main/PosixMain.cpp)
  target_include_directories(generals PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Main ${GENERATED})
  set_target_properties(generals PROPERTIES CXX_STANDARD 17)
  target_link_libraries(generals PRIVATE sdldevice installroot)
  # C2's SDL side of Monitors.h and MessageBoxWrapper, on SDL's offscreen video driver.
  # test_gameengine_stubs.cpp: the names the executable supplies, which GCC's larger pull from sdldevice
  # reaches (ld.lld, L1b); clang's does not, and they cost nothing there.
  add_executable(test_sdl_platform ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_sdl_platform.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_gameengine_stubs.cpp)
  target_include_directories(test_sdl_platform PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(test_sdl_platform PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
  set_target_properties(test_sdl_platform PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_sdl_platform PRIVATE sdldevice test_harness)
  add_test(NAME test_sdl_platform COMMAND test_sdl_platform)

  # C4's upper half: Windows' MilesAudioManager over the Miles surface on miniaudio's null backend, fed
  # the install's own INIs and sounds through a read-only farm of its archives (rule 9), heard through
  # the mix tap.  Needs ZH_GAME_DATA; Skipped (77) without it.
  add_executable(test_milesaudiomanager ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_milesaudiomanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_gameengine_stubs.cpp)
  target_include_directories(test_milesaudiomanager PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(test_milesaudiomanager PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
  set_target_properties(test_milesaudiomanager PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_milesaudiomanager PRIVATE sdldevice test_harness)
  add_test(NAME test_milesaudiomanager COMMAND test_milesaudiomanager)
  set_tests_properties(test_milesaudiomanager PROPERTIES SKIP_RETURN_CODE 77)
  if(ZH_GAME_DATA)
    set_tests_properties(test_milesaudiomanager PROPERTIES ENVIRONMENT "ZH_AUDIO_BACKEND=null;ZH_DATA_DIR=${ZH_GAME_DATA}")
  else()
    set_tests_properties(test_milesaudiomanager PROPERTIES ENVIRONMENT "ZH_AUDIO_BACKEND=null")
  endif()

  # C3a: input on SDL3's offscreen driver - the scancode table against DIKeyCodes.h, keys into the
  # engine's key state, text into the IME manager, the mouse, and the install's .ANI cursors (read only,
  # ZH_DATA_DIR; said when absent).
  # test_gameengine_stubs.cpp: the names the executable supplies (gAppPrefix, the .csf/.str paths).
  add_executable(test_sdl_input ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_sdl_input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_gameengine_stubs.cpp)
  target_include_directories(test_sdl_input PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(test_sdl_input PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>
    DIKEYCODES_H="${CMAKE_CURRENT_SOURCE_DIR}/GameEngine/Include/GameClient/DIKeyCodes.h")
  set_target_properties(test_sdl_input PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_sdl_input PRIVATE sdldevice test_harness)
  add_test(NAME test_sdl_input COMMAND test_sdl_input)
  if(ZH_GAME_DATA)
    set_tests_properties(test_sdl_input PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()

  # The entry sequence without game data: see the script.
  add_test(NAME generals_smoke
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/run_generals_smoke.sh $<TARGET_FILE:generals>)
  # E1's POSIX harness on itself, with the game's data: a recording, its playback and a second run agree,
  # and an edited replay is reported DIVERGED.  Same-machine determinism only: see the script.
  add_test(NAME replay_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/run_replay_check.sh $<TARGET_FILE:generals>)
  # Four 12000-frame matches since port defect 34.  The cap is there for a hang, not for speed: replay_check takes 279-392 s
  # alone, and up to 991 s inside a full suite on a loaded 8-core Linux worker, where 1200 s had begun to cut it off.
  set_tests_properties(replay_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 2400)
  if(ZH_GAME_DATA)
    set_tests_properties(replay_check PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
  # Port defect 33: no override leaves a unit without the locomotors EA gave it (a ReplaceModule of its AI
  # module discards them unless the block re-states them).  One short run of the game on its data.
  add_test(NAME locomotor_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/run_locomotor_check.sh $<TARGET_FILE:generals>)
  set_tests_properties(locomotor_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
  if(ZH_GAME_DATA)
    set_tests_properties(locomotor_check PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
  # The game's data going away under it (a drive disconnected or asleep): the game must say so and stop
  # with exit status 3, never crash; the control, a rename, plays on.  macOS only for now (hdiutil); skipped
  # elsewhere and without game data.  See the script.
  add_test(NAME data_gone_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/run_data_gone_check.sh $<TARGET_FILE:generals>)
  set_tests_properties(data_gone_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
  if(ZH_GAME_DATA)
    set_tests_properties(data_gone_check PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
  # L1's POSIX harness on itself: two headless copies on this machine play one seeded LAN match over
  # 127.0.0.1 and a second local address, agree to frame 1800, and each replay plays back to it; a
  # second copy on another seed is reported FAILED.  One machine, one compiler: see the script.  Skipped
  # without game data, without a second address, or with the macOS application firewall on.
  add_test(NAME net_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/run_net_check.sh $<TARGET_FILE:generals>)
  set_tests_properties(net_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
  if(ZH_GAME_DATA)
    set_tests_properties(net_check PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
  # P1 step 3: the staged overlay, <build>/overlay - what the app bundle will copy verbatim and what the
  # harnesses pass with -overlay - and <build>/overlay-dev, the -scenario and -cinema folders that never
  # ship.  Tools/stage-overlay.sh is the one place the list lives.  Small (10 MB; the art is linked).
  file(GLOB_RECURSE ZH_OVERLAY_INPUTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Data/*)
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/overlay.staged
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/stage-overlay.sh ${CMAKE_CURRENT_SOURCE_DIR}/Data
            ${CMAKE_CURRENT_SOURCE_DIR}/../Run ${CMAKE_BINARY_DIR}/overlay
    DEPENDS ${ZH_OVERLAY_INPUTS} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/stage-overlay.sh
    COMMENT "Staging the fork's overlay (P1)" VERBATIM)
  add_custom_target(zh_overlay ALL DEPENDS ${CMAKE_BINARY_DIR}/overlay.staged)
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/overlay-dev.staged
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/stage-overlay.sh ${CMAKE_CURRENT_SOURCE_DIR}/Data ""
            ${CMAKE_BINARY_DIR}/overlay-dev --dev
    DEPENDS ${ZH_OVERLAY_INPUTS} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/stage-overlay.sh
    COMMENT "Staging the development-only overlay (P1)" VERBATIM)
  add_custom_target(zh_overlay_dev ALL DEPENDS ${CMAKE_BINARY_DIR}/overlay-dev.staged)

  # P1 step 5: the macOS app, "Zero Hour Reforged.app" beside the executable, its dSYM beside it
  # (Tools/make-macos-app.sh: Info.plist, the icon, the staged overlay without the art, which the game reads from
  # the user data folder's ReforgedArt when something has put it there, the licences checked against generals'
  # link line, the HUD directive, an ad-hoc signature, verified).  Outside ALL: build it with `ninja macos_app`.
  # The bundle id belongs to whoever ships; the default is the project's home (the README's).
  if(APPLE)
    set(ZH_APP_BUNDLE_ID "io.github.olcayseygan.zero-hour-reforged" CACHE STRING "CFBundleIdentifier of the macOS app")
    set(ZH_APP_ARGS --generals $<TARGET_FILE:generals> --overlay ${CMAKE_BINARY_DIR}/overlay
        --build ${CMAKE_BINARY_DIR} --out "${CMAKE_BINARY_DIR}/Zero Hour Reforged.app" --bundle-id ${ZH_APP_BUNDLE_ID}
        --min-macos ${CMAKE_OSX_DEPLOYMENT_TARGET})
    # P2 universal2, option A (release time, E2): an x86_64 build's generals, lipo'd into the bundle's
    set(ZH_X86_64_GENERALS "" CACHE FILEPATH "An x86_64 build's generals, to make the macOS app universal2 (P2)")
    if(ZH_X86_64_GENERALS)
      list(APPEND ZH_APP_ARGS --x86-64-generals ${ZH_X86_64_GENERALS})
    endif()
    add_custom_target(macos_app
      COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/make-macos-app.sh ${ZH_APP_ARGS}
      DEPENDS generals zh_overlay
      COMMENT "Building Zero Hour Reforged.app (P1 step 5)" VERBATIM)
    # the bundle script on itself: a bundle from this build's inputs (no art) holds what it should and
    # verifies, and a HUD-off INI, an unlicensed library and a write after signing are each caught
    add_test(NAME macos_app_check
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_macos_app.sh $<TARGET_FILE:generals>
                     ${CMAKE_BINARY_DIR}/overlay ${CMAKE_BINARY_DIR} ${CMAKE_OSX_DEPLOYMENT_TARGET})
    set_tests_properties(macos_app_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
    # E1 on the bundle itself, "tests what ships": no -root, no -overlay, the seal checked after.  It needs
    # `ninja macos_app` first (outside ALL), and says so when it skips.
    add_test(NAME replay_check_app
             COMMAND bash -c "if [ ! -x \"$1/Contents/MacOS/generals\" ]; then echo \"skip: no bundle at $1 - build it with ninja macos_app\"; exit 77; fi; exec \"$0\" --app \"$1\" --seeds 0 --maxframes 1200"
                     ${CMAKE_CURRENT_SOURCE_DIR}/Tools/replay-check.sh "${CMAKE_BINARY_DIR}/Zero Hour Reforged.app")
    set_tests_properties(replay_check_app PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
    if(ZH_GAME_DATA)
      set_tests_properties(replay_check_app PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
    endif()
  endif()

  # The first launch's folder question, headless (no test run may need a human): the real generals
  # through PosixMain's test stand-in for the dialog (ZH_TEST_CHOOSER_ANSWERS) - refusals and their reasons,
  # asking again, accepting and remembering, cancelling, and the second start asking nothing.  Needs ZH_GAME_DATA.
  add_test(NAME first_launch_chooser_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_first_launch_chooser.sh $<TARGET_FILE:generals> ${CMAKE_BINARY_DIR}/overlay)
  set_tests_properties(first_launch_chooser_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
  if(ZH_GAME_DATA)
    set_tests_properties(first_launch_chooser_check PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()

  # P3: Tools/linux-portable.sh's folder for the Steam Deck, built inside the Steam Runtime's SDK, and E1 through
  # its own launcher in the Steam Runtime's platform image.  Skipped (77) without docker, the SDK image, or a
  # portable CMake (ZH_PORTABLE_CMAKE: 3.29 or later, running inside the container - Kitware's release tarball).
  if(ZH_PLATFORM_LINUX)
    set(ZH_PORTABLE_CMAKE "" CACHE FILEPATH "A Linux CMake (3.29+, Kitware's release) for linux_portable_check's container build (P3)")
    add_test(NAME linux_portable_check
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_linux_portable.sh ${CMAKE_BINARY_DIR})
    set(_zh_portable_env "ZH_PORTABLE_CMAKE=${ZH_PORTABLE_CMAKE}")
    if(ZH_GAME_DATA)
      list(APPEND _zh_portable_env "ZH_DATA_DIR=${ZH_GAME_DATA}")
    endif()
    set_tests_properties(linux_portable_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 7200 ENVIRONMENT "${_zh_portable_env}")
  endif()

  # P1 step 1: the fork's overlay as a read root gives the game what Windows' one folder gives it - the
  # EXE CRC, the INI CRC and the world state agree between the two layouts - with two armed controls.
  add_test(NAME overlay_crc_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/overlay-crc-check.sh --generals $<TARGET_FILE:generals>)
  set_tests_properties(overlay_crc_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
  if(ZH_GAME_DATA)
    set_tests_properties(overlay_crc_check PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
  # The INI masters' line ends: the overlay staged from an LF and from a CRLF checkout holds the same CRLF
  # bytes, and with the game's data the INI CRC agrees from both, while LF files (the armed control)
  # give another - the CRC a Windows build has, or none of its joins (.gitattributes, stage-overlay.sh).
  add_test(NAME ini_crlf_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/run_ini_crlf_check.sh $<TARGET_FILE:generals>)
  set_tests_properties(ini_crlf_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
  if(ZH_GAME_DATA)
    set_tests_properties(ini_crlf_check PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
  # P1 step 2: the roots are read-only - GameEngine::init's INIZH.big delete is refused, the farm and the
  # install are unchanged after a skirmish; -writableRoot in a root with no links does delete; and a run
  # from inside an app bundle logs to the user data's Logs, not into the bundle.
  # P1 step 3: every path the game can open resolves the same in Windows' one-folder shape and in the
  # package's root plus -overlay (the staged overlay), path by path, with an archive-order control.
  add_test(NAME packaging_resolution_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/packaging-resolution-check.sh --generals $<TARGET_FILE:generals>)
  set_tests_properties(packaging_resolution_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
  if(ZH_GAME_DATA)
    set_tests_properties(packaging_resolution_check PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
  # Their install check (Tools/install-guard.sh) on a made-up install: a change, and a check that could not
  # be made, are told apart.  No game data.
  # The stager keeps the HUD overlay's default the build's (off in Release): it refuses a staged ShowHudOverlay =
  # Yes, loose or in an archive.
  add_test(NAME stage_overlay_hud_check COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_stage_overlay_hud.sh)
  add_test(NAME install_guard_check COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_install_guard.sh)
  # Every harness stops when its work folder cannot be made, instead of going on with the name empty
  # (Tests/test_workfolder_guard.sh): a lint of every tracked script, and the farm harnesses run with
  # TMPDIR=/nonexistent.
  add_test(NAME workfolder_guard_check COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_workfolder_guard.sh)
  # Decision 2 for the GPU: a first launch chooses the top detail preset (High), a later one keeps it, and
  # -noshaders (a Voodoo2, below every preset) chooses Low.  Read from Options.ini after real runs.
  add_test(NAME lod_first_run_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/lod-first-run-check.sh --generals $<TARGET_FILE:generals>)
  set_tests_properties(lod_first_run_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
  if(ZH_GAME_DATA)
    set_tests_properties(lod_first_run_check PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
  add_test(NAME root_readonly_check
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/root-readonly-check.sh --generals $<TARGET_FILE:generals>)
  set_tests_properties(root_readonly_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
  if(ZH_GAME_DATA)
    set_tests_properties(root_readonly_check PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
endif()

# ===========================================================================
# Tests.  Run with:  ctest --test-dir build -C Release --output-on-failure
# ===========================================================================
enable_testing()

set(TESTS ${CMAKE_CURRENT_SOURCE_DIR}/Tests)

# Shared runner for the micro-harness in Tests/test_harness.h; every test
# binary links it instead of writing its own main().
add_library(test_harness STATIC ${TESTS}/test_main.cpp)
target_include_directories(test_harness PUBLIC ${TESTS})
# Off Windows, every binary that links the harness - its own main() or the runner's - refuses to start
# without ZH_USER_DATA_DIR, which otherwise leaves the engine to use this user's real data directory.
# An INTERFACE source so it is compiled into each executable, where a static library's object would be
# dropped by any binary that does not reference it.  ctest gives each test its folder at the end of
# this file.
if(ZH_PLATFORM_POSIX)
  target_sources(test_harness INTERFACE ${TESTS}/test_user_data_guard.cpp)
endif()

# add_lib_test(<name> [PENDING_MACOS] <libs...>) -> Tests/<name>.cpp, built and registered.
#
# PENDING_MACOS says the test's libraries are ones the macOS port has not brought up yet.  On a Mac
# the executable is still defined - tasks B1-B6 need something to compile - but it stays out of the
# default build and out of ctest, because a test that cannot be linked is not a failing test, it is
# a test that does not exist here yet.  On Windows the keyword changes nothing at all.
function(add_lib_test name)
  # add_lib_test(<name> <libs...> [ARGS <arguments for the ctest entry...>])
  set(libs ${ARGN})
  set(test_args "")
  list(FIND libs ARGS _zh_args_at)
  if(NOT _zh_args_at EQUAL -1)
    list(SUBLIST libs ${_zh_args_at} -1 test_args)
    list(REMOVE_AT test_args 0)
    list(SUBLIST libs 0 ${_zh_args_at} libs)
  endif()
  set(exclude "")
  if("PENDING_MACOS" IN_LIST libs)
    list(REMOVE_ITEM libs PENDING_MACOS)
    set(exclude "${ZH_NOT_YET_PORTABLE}")
  endif()
  add_executable(${name} ${exclude} ${TESTS}/${name}.cpp)
  target_link_libraries(${name} PRIVATE test_harness ${libs})
  add_test(NAME ${name} COMMAND ${name} ${test_args})
  if(NOT exclude STREQUAL "")
    # PENDING_MACOS used to skip add_test() altogether, so `ctest` on macOS
    # listed 11 tests where Windows lists 16 and said nothing about the other
    # five.  An absent test reads exactly like a passing one, and "ctest is
    # green" was being taken as evidence of progress across the whole port.
    # Registering it DISABLED keeps it out of the run - the binary is
    # EXCLUDE_FROM_ALL and would not link - and puts it in the output, which is
    # where a test somebody still owes belongs.
    set_tests_properties(${name} PROPERTIES DISABLED TRUE)
    set_property(GLOBAL APPEND PROPERTY ZH_DEFERRED_TESTS ${name})
  endif()
endfunction()

# Platform/MsvcFloatCasts.h: float-to-unsigned conversions as MSVC computes them.  Built on every
# platform; on Windows it also holds MSVC's own cast to the same bytes.
add_lib_test(test_msvc_float_casts)
target_include_directories(test_msvc_float_casts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)

add_lib_test(test_wwmath wwmath wwlib wwsaveload wwdebug)
target_include_directories(test_wwmath PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)
# R1, smooth motion: W3DSmoothMotion.h's blend and verdicts, header-only over WWMath.
add_lib_test(test_smooth_motion wwmath wwlib wwsaveload wwdebug)
target_include_directories(test_smooth_motion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include ${WW}/WWLib)

add_lib_test(test_wwlib wwlib wwmath wwsaveload wwdebug)
target_include_directories(test_wwlib PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWSaveLoad ${WW}/Wwutil)
# generals.exe is linked /LARGEADDRESSAWARE, and that flag changes what
# GlobalMemoryStatus saturates its 32-bit fields to (0xFFFFFFFF, not 0x7FFFFFFF).
# cpudetect_memory_fits_in_the_signed_int_its_callers_use only means anything in a
# binary that sees the same value the game does.
# A cl.exe linker switch, so it asks the compiler rather than the platform.
if(MSVC)
  target_link_options(test_wwlib PRIVATE /LARGEADDRESSAWARE)
endif()

add_lib_test(test_wwsaveload wwsaveload wwlib wwmath wwdebug)
target_include_directories(test_wwsaveload PRIVATE ${WW}/WWSaveLoad ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/Wwutil)

add_lib_test(test_wwutil wwutil wwmath wwlib wwdebug)
target_include_directories(test_wwutil PRIVATE ${WW}/Wwutil ${WW}/WWLib ${WW}/WWMath)

# Lib/Clock.h is header-only and links nothing, so this runs on both platforms from day one -
# which matters, because the macOS half of that header is the half nobody has ever executed.
add_lib_test(test_clock)
if(ZH_PLATFORM_WINDOWS)
  target_link_libraries(test_clock PRIVATE winmm)		# Clock.h's Windows half: timeGetTime, timeBeginPeriod (W2)
endif()

add_lib_test(test_compression compression)

# .w3d on-disk layout pins.  Deliberately OUTSIDE the ZH_PLATFORM_WINDOWS guard
# below: it is header-only, asserts sizeof/offsetof and links nothing but the
# harness, so it builds on a platform where ww3d2 itself does not compile yet -
# which is the whole of M1 and the only time these asserts can still catch
# something.
add_lib_test(test_w3dlayout)
target_include_directories(test_w3dlayout PRIVATE
  # Libraries/Include is here because always.h reaches Platform/MSVCCompat.h, which B3 added after
  # B7 wrote this target.  Neither branch could see the other; it only fails once both are merged.
  ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
  ${WW}/WW3D2 ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil)

# The vertex program generator is shared (ffvertex builds everywhere), so its unit test runs everywhere:
# registered on Windows only, it went stale through port defects 23 and 26, which changed the generator
# off Windows (W2).
add_lib_test(test_ffvertex ffvertex)

# Every test from here to test_pixelcentre links the renderer or a Direct3D backend, so they exist
# only where those targets do.  D2 through D5 bring the Metal side back with tests of its own.
if(ZH_PLATFORM_WINDOWS)
add_lib_test(test_ww3d2 ww3d2 wwsaveload wwlib wwmath wwdebug)
target_include_directories(test_ww3d2 PRIVATE
  ${WW}/WW3D2 ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil)


add_lib_test(test_ffshader ffshader)
add_lib_test(test_dx11device dx11device)
add_lib_test(test_dx11state dx11state)
add_lib_test(test_dx11layout dx11layout)
add_lib_test(test_ffvertexcompile ffvertex ffshader)
# The generated vertex program run against the fixed-function pipeline it replaces, on a real
# Direct3D 9 device.  It skips itself on a machine with no device rather than failing.
add_lib_test(test_ffvertexd3d9 ffvertex d3d9 user32)
add_lib_test(test_ffshadercompile ffshader)
add_lib_test(test_dx11pipeline dx11device dx11state dx11layout ffshader ffvertex)
add_lib_test(test_dx11resource dx11resource dx11device)
add_lib_test(test_engineshader engineshader)
add_lib_test(test_dx11backend dx11backend)
add_lib_test(test_dx11post dx11post dx11device user32)
add_lib_test(test_dx11twin dx11twin dx11device)
add_lib_test(test_dx11texture dx11texture dx11device user32)
# Both runtimes in one binary, so where a triangle edge lands is a measurement rather than a reading
# of two sets of documentation that describe the same thing in different words.
add_lib_test(test_pixelcentre dx11backend dx11resource dx11device d3d9 user32)
endif()  # ZH_PLATFORM_WINDOWS - renderer and Direct3D backend tests

# wwdownload, debug and eabrowserdispatch are Windows-only libraries above, so their tests are too.
if(ZH_PLATFORM_WINDOWS)
add_lib_test(test_wwdownload wwdownload)
target_include_directories(test_wwdownload PRIVATE ${WW}/WWDownload)
# A Debug wwdownload logs through DebugLog, which the engine defines; the test links no engine.
target_sources(test_wwdownload PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_wwdownload_stubs.cpp)

add_lib_test(test_debug debuglib)

add_lib_test(test_support benchmark eabrowserdispatch)
endif()  # ZH_PLATFORM_WINDOWS - Windows-only support library tests

# W3: in a Debug build WWVegas and GameMemory.cpp call the engine's debug entry points (DebugCrash, the
# file-and-line operator new, the stack dumper, ...), and these tests link no engine.  One stub file gives
# each what it lacks; outside _DEBUG it compiles to nothing, so a Release link is unchanged.
if(MSVC)
  foreach(name test_wwlib test_wwmath test_smooth_motion wwmath_selfcheck test_wwsaveload test_wwutil
               test_ww3d2 gametext_csf test_premain_strings test_premain_unicode)
    if(TARGET ${name})
      target_sources(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests/debug_build_stubs.cpp)
    endif()
  endforeach()
  foreach(name test_wwlib test_wwmath test_smooth_motion wwmath_selfcheck test_wwsaveload test_wwutil
               test_ww3d2)
    if(TARGET ${name})
      target_compile_definitions(${name} PRIVATE ZH_DEBUG_STUBS_WWVEGAS)
    endif()
  endforeach()
  foreach(name widechar_crc_gate widechar_file_selfcheck widechar_format_selfcheck)
    target_sources(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests/debug_build_stubs.cpp)
  endforeach()
  foreach(name widechar_crc_gate widechar_file_selfcheck widechar_format_selfcheck gametext_csf
               test_premain_strings test_premain_unicode)
    target_compile_definitions(${name} PRIVATE ZH_DEBUG_STUBS_CRASH)
  endforeach()
  target_compile_definitions(gametext_csf PRIVATE ZH_DEBUG_STUBS_GAMEMEMORY)
  target_compile_definitions(test_premain_strings PRIVATE ZH_DEBUG_STUBS_GAMEMEMORY ZH_DEBUG_STUBS_GLOBALDATA)
  target_compile_definitions(test_premain_unicode PRIVATE ZH_DEBUG_STUBS_GAMEMEMORY ZH_DEBUG_STUBS_GLOBALDATA)
endif()

# Phase 3 checkpoint: boots the memory manager, name keys and file system, then
# runs a real INI file through the parser.
# The money sweep (every_start_reaches_its_money_and_has_two_ways_out, 62 generated maps) was 125 s of
# test_gameengine's 162 s on one Linux host: it is three cases, one per player count, each its own entry
# below, so ctest -j runs them beside each other and beside the rest.  Only they are split out: the rest
# stays one process, in one order, so a failure that depends on what ran before still shows.
add_lib_test(test_gameengine gameengine ARGS --exclude=every_start_reaches_its_money_and_has_two_ways_out_)
foreach(_zh_players 2 4 8)
  add_test(NAME test_gameengine_money_sweep_${_zh_players}_players
           COMMAND test_gameengine every_start_reaches_its_money_and_has_two_ways_out_${_zh_players}_players)
  set_tests_properties(test_gameengine_money_sweep_${_zh_players}_players PROPERTIES TIMEOUT 2700)
endforeach()
# T1's terrain golden against the moved code (GameLogic/WorldHeightMapData, TerrainHeightSampling):
# nine shipped maps out of the install's archives, which must sample exactly as Tests/terrain_golden.txt
# says the original W3D code did.  Needs ZH_GAME_DATA; Skipped (77) without it.
# Port defect 29: the LAN lobby's broadcast listener off Windows, through the real Transport and UDP, in the
# layout two copies of the game on one host build (see the test's header).  POSIX only: the listener is.
if(ZH_PLATFORM_POSIX)
  add_lib_test(test_lan_broadcast gameengine)
  # the exe's names and the device layer, as test_terrain_golden links them
  target_sources(test_lan_broadcast PRIVATE ${TESTS}/test_gameengine_stubs.cpp)
  target_link_libraries(test_lan_broadcast PRIVATE posixdevice w3ddevice)
  # W1: the same sockets between two hosts, driven by hand from each (a tool, not a test: it needs a peer)
  add_executable(lan_broadcast_probe ${TESTS}/lan_broadcast_probe.cpp ${TESTS}/test_gameengine_stubs.cpp)
  target_link_libraries(lan_broadcast_probe PRIVATE gameengine posixdevice w3ddevice)
endif()
add_lib_test(test_terrain_golden gameengine)
target_compile_definitions(test_terrain_golden PRIVATE
  TERRAIN_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/terrain_golden.txt")
set_target_properties(test_terrain_golden PROPERTIES CXX_STANDARD 17)
set_tests_properties(test_terrain_golden PROPERTIES SKIP_RETURN_CODE 77)
# test_gameengine's stubs: the exe's names everywhere, and on Windows the device layer's stand-ins,
# which off Windows PosixDevice (the CD manager) and W3DDevice (the renderer's hooks) have for real.
target_sources(test_terrain_golden PRIVATE ${TESTS}/test_gameengine_stubs.cpp)
if(ZH_PLATFORM_POSIX)
  target_link_libraries(test_terrain_golden PRIVATE posixdevice w3ddevice)
endif()
if(ZH_GAME_DATA)
  set_tests_properties(test_terrain_golden PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
endif()
# T1c (defect 17): the water grid's step, moved to the top of each logic frame, against the original
# code's output under EA's one-pass-per-frame loop (Tests/water_grid_oracle.cpp), under catch-up too.
add_lib_test(test_water_grid gameengine)
target_sources(test_water_grid PRIVATE ${TESTS}/test_gameengine_stubs.cpp)
set_target_properties(test_water_grid PROPERTIES CXX_STANDARD 17)
if(ZH_PLATFORM_POSIX)
  target_link_libraries(test_water_grid PRIVATE posixdevice w3ddevice)
endif()
# Off Windows the device layer's hooks are the game's own: the renderer's from W3DDevice, the CD
# manager from PosixDevice.  On Windows the test's own stubs stand in for gameenginedevice, which it
# does not link (B6).
if(ZH_PLATFORM_POSIX)
  target_link_libraries(test_gameengine PRIVATE posixdevice w3ddevice)
endif()
# the well-known Dict keys and a handful of device/exe entry points live in
# GameEngineDevice, which Phase 4 ports; stand-ins until then.
target_sources(test_gameengine PRIVATE ${TESTS}/test_gameengine_stubs.cpp)
set_target_properties(test_gameengine PROPERTIES CXX_STANDARD 17)
# 454 cases in 98 s in Release; a Debug build runs them far slower (W3), so its ceiling is its own.
set_tests_properties(test_gameengine PROPERTIES TIMEOUT 2700)
# simulation_uses_no_runtime_trig reads the engine's own sources back off disk to prove nothing in
# GameLogic or Common has gone back to calling sinf().
# fxlist_reforged_ini_parses_and_keeps_its_light runs the fork's own FXList file through the real
# INI parser, so it needs the tracked master rather than whatever sits in Run/.
# the_options_menu_buttons_all_wear_one_font reads the tracked layout master, not Run/'s copy.
# the_spectator_page_names_only_on_off_options reads the tracked page master the same way, and
# the_command_grid_keys_are_the_places_and_n_sells_and_b_rallies the tracked command map.
target_compile_definitions(test_gameengine PRIVATE
  GAMEENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/GameEngine"
  FXLIST_REFORGED_INI="${CMAKE_CURRENT_SOURCE_DIR}/Data/INI/FXListReforged.ini"
  OPTIONS_MENU_WND="${CMAKE_CURRENT_SOURCE_DIR}/Data/Window/Menus/OptionsMenu.wnd"
  SPECTATOR_HTML="${CMAKE_CURRENT_SOURCE_DIR}/Data/Window/Html/Spectator.html"
  COMMAND_MAP_REFORGED_INI="${CMAKE_CURRENT_SOURCE_DIR}/Data/INI/CommandMapReforged.ini"
  PLATE_TEXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Data/Art/Textures")

# W3: these three link the engine without its device and WinMain, whose Debug-only names it reads
# (Tests/debug_build_stubs.cpp; empty outside _DEBUG).
if(MSVC)
  foreach(name test_gameengine test_terrain_golden test_water_grid)
    target_sources(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests/debug_build_stubs.cpp)
    target_compile_definitions(${name} PRIVATE ZH_DEBUG_STUBS_ENGINE)
  endforeach()
endif()

# The three hand-written self-checks predate the harness; run them from ctest
# too so one command covers everything.
add_test(NAME detround_selfcheck COMMAND detround_selfcheck)
if(ZH_PLATFORM_POSIX)
  add_test(NAME fpucontrol_selfcheck COMMAND fpucontrol_selfcheck)
endif()
add_test(NAME d3dxportable_selfcheck COMMAND d3dxportable_selfcheck)
add_test(NAME wwmath_selfcheck COMMAND wwmath_selfcheck)
add_test(NAME compression_selfcheck COMMAND compression_selfcheck)
# The device and codec smoke tests below belong to targets only a Windows configuration defines.
if(ZH_PLATFORM_WINDOWS)
# dx9_smoke creates a device the way the renderer does, with nothing next to it to
# translate anything, and puts the D3D8 calls that have no D3D9 counterpart through their
# replacements.  "-msaa 4" adds the multisampled back buffer and the plain render target
# with a depth buffer of its own that DX8Wrapper::_Get_Non_MultiSampled_Depth_Buffer pairs
# for the screen filters.
add_test(NAME dx9_smoke COMMAND dx9_smoke)
add_test(NAME dx9_smoke_msaa COMMAND dx9_smoke -msaa 4)
# 77 is ARM64's: no d3dx9_43.dll exists for it, and dx9_smoke says so rather than failing.
set_tests_properties(dx9_smoke dx9_smoke_msaa PROPERTIES SKIP_RETURN_CODE 77)

# bink_smoke decodes a real .bik with the player this build links and compares what comes out
# against the values read straight from the file's own header, so a wrong field offset or a wrong
# surface constant shows up as a mismatch.  It runs from Run/, where the movies and the FFmpeg DLLs
# are; without game data it prints "skip".
add_executable(bink_smoke ${TESTS}/bink_smoke.c)
target_include_directories(bink_smoke PRIVATE ${WW}/Bink/include)
target_compile_definitions(bink_smoke PRIVATE BINK_LINKED_PLAYER)
target_link_libraries(bink_smoke PRIVATE binkvideo)
add_test(NAME bink_smoke COMMAND bink_smoke Data/Movies/GC_Background.bik
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Run)

# miles_smoke plays a tone through the audio backend and waits for the end-of-sample callback,
# which only arrives once a voice has played the buffer out.
add_executable(miles_smoke ${TESTS}/miles_smoke.cpp)
target_include_directories(miles_smoke PRIVATE ${WW}/Miles6/include)
target_link_libraries(miles_smoke PRIVATE milesaudio)
# From Run/, where the FFmpeg DLLs the backend decodes with are.
add_test(NAME miles_smoke COMMAND miles_smoke
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Run)
endif()  # ZH_PLATFORM_WINDOWS - device and codec smoke tests

if(ZH_PLATFORM_POSIX)
# The audio backend's checks off Windows, on miniaudio's null backend so they run with no audio
# hardware: a device that still calls the mix in real time.  miles_smoke is the same file Windows
# runs.  test_miles_miniaudio listens to the mix through AIL_ex_start_capture and measures what
# miles_xaudio2 would have produced; the half of it that needs the game's .big files reads them from
# ZH_GAME_DATA (a folder holding generals/ and zerohour/, read only) and prints "skip" without it.
set(ZH_GAME_DATA "" CACHE PATH "Folder with a Zero Hour install's generals/ and zerohour/, for tests that read game data")
add_executable(miles_smoke ${TESTS}/miles_smoke.cpp)
target_include_directories(miles_smoke PRIVATE ${WW}/Miles6/include)
target_link_libraries(miles_smoke PRIVATE milesaudio)
add_test(NAME miles_smoke COMMAND miles_smoke)
set_tests_properties(miles_smoke PROPERTIES ENVIRONMENT "ZH_AUDIO_BACKEND=null")

add_executable(test_miles_miniaudio ${TESTS}/test_miles_miniaudio.cpp)
target_include_directories(test_miles_miniaudio PRIVATE ${WW}/Miles6/include ${LIBSRC}/miniaudio)
target_link_libraries(test_miles_miniaudio PRIVATE test_harness milesaudio miniaudio)
add_test(NAME test_miles_miniaudio COMMAND test_miles_miniaudio)
# ZH_DATA_DIR is set only when ZH_GAME_DATA was configured.  Setting it unconditionally overwrote a
# ZH_DATA_DIR from the caller's environment with an empty string, and the data half skipped silently.
if(ZH_GAME_DATA)
  set_tests_properties(test_miles_miniaudio PROPERTIES
    ENVIRONMENT "ZH_AUDIO_BACKEND=null;ZH_DATA_DIR=${ZH_GAME_DATA}")
else()
  set_tests_properties(test_miles_miniaudio PROPERTIES ENVIRONMENT "ZH_AUDIO_BACKEND=null")
endif()

# For human ears, not ctest: plays one sound from a .big archive (or a loose file) on the real
# device, through the same API, at a chosen volume and pan.
add_executable(miles_listen ${TESTS}/miles_listen.cpp)
target_include_directories(miles_listen PRIVATE ${WW}/Miles6/include)
target_link_libraries(miles_listen PRIVATE milesaudio)
endif()  # ZH_PLATFORM_POSIX - audio backend checks

if(ZH_PLATFORM_POSIX)
# The engine's paths against a POSIX file system (C1): the default volume, and on macOS a
# case-sensitive APFS image it creates, since neither macOS's default volume nor the Steam installs'
# exFAT tells case apart.  Linux's runs are case-sensitive already.
add_lib_test(test_posixpath wwlib wwdebug)
# P1 step 4: where the player's Zero Hour is - -root, Registry.ini, the known places, the chooser (a stub
# here: the real one, SDL's folder dialog, needs a person), and every validation message.
add_lib_test(test_install_root installroot wwlib wwdebug)
target_include_directories(test_posixpath PRIVATE ${WW}/WWLib ${WW}/WWDebug)

# PosixLocalFileSystem through the engine's own interface (C1): built from the real memory manager,
# LocalFile, File and PosixDevice sources with gameengine's include paths and definitions, because
# gameengine does not link on macOS yet.  The few names those sources reach and the test does not are
# in its _stubs.cpp, which says why each one is there.  Under GCC they also name ~AudioEventRTS and
# UnicodeString::releaseBuffer (eager vtables): UnicodeString.cpp is the real one, and the destructor
# is stubbed by Tests/gcc_eager_vtable_stubs.cpp (L1b; they were DISABLED on Linux until then).
set(ZH_POSIXLFS_SOURCES_NO_MAIN
  ${GE}/Source/Common/System/GameMemory.cpp ${GE}/Source/Common/System/MemoryInit.cpp
  ${GE}/Source/Common/System/CriticalSection.cpp ${GE}/Source/Common/System/JobSystem.cpp
  ${GE}/Source/GameLogic/System/FPUControl.cpp ${GE}/Source/Common/System/ExecutableDirectory.cpp
  ${GE}/Source/Common/System/SubsystemInterface.cpp ${GE}/Source/Common/System/AsciiString.cpp
  ${GE}/Source/Common/System/WideCharFns.cpp ${GE}/Source/Common/System/File.cpp
  ${GE}/Source/Common/System/LocalFile.cpp ${GE}/Source/Common/System/LocalFileSystem.cpp
  ${GE}/Source/Common/System/RAMFile.cpp ${GE}/Source/Common/System/registry.cpp
  ${GE}/Source/Common/System/UnicodeString.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Tests/gcc_eager_vtable_stubs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Common/PosixLocalFileSystem.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Common/PosixLocalFile.cpp)
# The same list with the test's own two files, as test_registryfile and test_wwdownload_registry take it.
set(ZH_POSIXLFS_SOURCES
  ${TESTS}/test_posixlocalfilesystem.cpp ${TESTS}/test_posixlocalfilesystem_stubs.cpp ${ZH_POSIXLFS_SOURCES_NO_MAIN})
add_executable(test_posixlocalfilesystem ${ZH_POSIXLFS_SOURCES})
target_include_directories(test_posixlocalfilesystem PRIVATE
  $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES> ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
target_compile_definitions(test_posixlocalfilesystem PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>
  ZH_EAGER_VTABLE_STUB_NO_POOL)
set_target_properties(test_posixlocalfilesystem PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_posixlocalfilesystem PRIVATE test_harness wwlib wwdebug)
add_test(NAME test_posixlocalfilesystem COMMAND test_posixlocalfilesystem)

# Registry.ini's reader and writer (Common/RegistryFile.h, registry.cpp's POSIX half), from the same
# engine sources as test_posixlocalfilesystem, for the same reason, and not on Linux for the same one.
set(ZH_REGISTRYFILE_SOURCES ${ZH_POSIXLFS_SOURCES})
list(REMOVE_ITEM ZH_REGISTRYFILE_SOURCES ${TESTS}/test_posixlocalfilesystem.cpp)
list(APPEND ZH_REGISTRYFILE_SOURCES ${TESTS}/test_registryfile.cpp)
add_executable(test_registryfile ${ZH_REGISTRYFILE_SOURCES})
target_include_directories(test_registryfile PRIVATE
  $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES> ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
target_compile_definitions(test_registryfile PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>
  ZH_EAGER_VTABLE_STUB_NO_POOL)
set_target_properties(test_registryfile PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_registryfile PRIVATE test_harness wwlib wwdebug)
add_test(NAME test_registryfile COMMAND test_registryfile)

# WWDownload's registry functions and FormatURLFromRegistry off Windows (WWDownloadRegistryPosix.cpp)
# over this user's Registry.ini, read back through registry.cpp's own getters too (B6).  Writes only
# into a folder in the build tree.  POSIX only: on Windows they are wwdownload's, over the registry.
if(ZH_PLATFORM_POSIX)
  set(ZH_WWDOWNLOAD_REGISTRY_SOURCES ${ZH_REGISTRYFILE_SOURCES})
  list(REMOVE_ITEM ZH_WWDOWNLOAD_REGISTRY_SOURCES ${TESTS}/test_registryfile.cpp)
  list(APPEND ZH_WWDOWNLOAD_REGISTRY_SOURCES ${TESTS}/test_wwdownload_registry.cpp
    ${GE}/Source/Common/System/WWDownloadRegistryPosix.cpp)
  add_executable(test_wwdownload_registry ${ZH_WWDOWNLOAD_REGISTRY_SOURCES})
  target_include_directories(test_wwdownload_registry PRIVATE
    $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES> ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
  target_compile_definitions(test_wwdownload_registry PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>
    ZH_EAGER_VTABLE_STUB_NO_POOL)
  set_target_properties(test_wwdownload_registry PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_wwdownload_registry PRIVATE test_harness wwlib wwdebug)
  add_test(NAME test_wwdownload_registry COMMAND test_wwdownload_registry)
endif()

# The user's folders off Windows (C1 (e)): the user data directory and the Desktop, EarlyOptions.h's
# POSIX half.  Writes only under $TMPDIR.
add_executable(test_userfolders ${TESTS}/test_userfolders.cpp)
target_include_directories(test_userfolders PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(test_userfolders PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(test_userfolders PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_userfolders PRIVATE test_harness wwlib wwdebug)
add_test(NAME test_userfolders COMMAND test_userfolders)

# EarlyCommandLine.h off Windows: the options read before the parser exists, from the process's own
# argv, before main and after (C2).  The arguments are the test's input; see the test file.
add_executable(test_early_command_line ${TESTS}/test_early_command_line.cpp ${TESTS}/gcc_eager_vtable_stubs.cpp)
target_include_directories(test_early_command_line PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(test_early_command_line PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>
  ZH_EAGER_VTABLE_STUB_STRINGS)	# GCC's eager vtables; see Tests/gcc_eager_vtable_stubs.cpp
set_target_properties(test_early_command_line PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_early_command_line PRIVATE test_harness wwlib wwdebug)
# test_harness takes the first argument as its name filter; the options follow it.
add_test(NAME test_early_command_line COMMAND test_early_command_line early_command_line
  -logPrefix pre -headless -root "/tmp/zh_ä" -jobthreads 3 -last)

# C5: the crash handler from the real CrashHandlerPosix.cpp, in a child process per way of crashing;
# the report, the exit signal, and two armed controls.  See the test file.
add_executable(test_crash_reporting ${TESTS}/test_crash_reporting.cpp ${GE}/Source/Common/System/CrashHandlerPosix.cpp
  ${TESTS}/gcc_eager_vtable_stubs.cpp)
target_include_directories(test_crash_reporting PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(test_crash_reporting PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>
  ZH_EAGER_VTABLE_STUB_STRINGS)	# GCC's eager vtables; see Tests/gcc_eager_vtable_stubs.cpp
set_target_properties(test_crash_reporting PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_crash_reporting PRIVATE wwlib wwdebug ${CMAKE_DL_LIBS})	# dladdr: libdl before glibc 2.34
add_test(NAME test_crash_reporting COMMAND test_crash_reporting)
# It crashes on purpose (a null write, an abort, raised SIGFPE and SIGBUS) and checks the crash handler's report.  Under
# ZH_SANITIZE the sanitizer's own SEGV handler reports and exits first, which proves nothing about ours.
if(ZH_SANITIZE)
  set_tests_properties(test_crash_reporting PROPERTIES DISABLED TRUE)
endif()

# The BIG archive reader on POSIX, against the real game data under rule 9 (C1 (f)): every path's
# owning archive against an independent reading of the mount order, and the bytes of a sample against
# an independent parser.  Built from the real sources, as test_posixlocalfilesystem is.  Needs
# ZH_GAME_DATA; without it the test is Skipped (77).
add_executable(test_bigfilesystem ${TESTS}/test_bigfilesystem.cpp ${TESTS}/test_bigfilesystem_stubs.cpp
  ${ZH_POSIXLFS_SOURCES_NO_MAIN}
  ${GE}/Source/Common/System/ArchiveFileSystem.cpp ${GE}/Source/Common/System/ArchiveFile.cpp
  ${GE}/Source/Common/System/StreamingArchiveFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/Win32Device/Common/Win32BIGFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp)
target_include_directories(test_bigfilesystem PRIVATE
  $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES> ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
target_compile_definitions(test_bigfilesystem PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>
  ZH_EAGER_VTABLE_STUB_NO_POOL)
set_target_properties(test_bigfilesystem PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_bigfilesystem PRIVATE test_harness wwlib wwdebug)
add_test(NAME test_bigfilesystem COMMAND test_bigfilesystem)
set_tests_properties(test_bigfilesystem PROPERTIES SKIP_RETURN_CODE 77)
if(ZH_GAME_DATA)
  set_tests_properties(test_bigfilesystem PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
endif()

# The POSIX half of the Wine listing oracle (Tests/fs_oracle.cpp; its Windows half is built with
# mingw-w64 and run under Wine, as C1 did).
add_executable(fs_oracle ${TESTS}/fs_oracle.cpp)
target_include_directories(fs_oracle PRIVATE ${WW}/WWLib)
target_link_libraries(fs_oracle PRIVATE wwlib wwdebug)

# The POSIX twin of test_ffshadercompile and test_ffvertexcompile: every generated program under the
# SDL3 GPU target through glslang, spirv-val, SPIRV-Cross and, on macOS, the Metal driver (D3).
# spirv-val is a tool on the machine, not something this build makes; without one the test prints
# SKIPPED for that step and says how many it did not check.
find_program(ZH_SPIRV_VAL spirv-val)
add_lib_test(test_shader_sdl ffvertex engineshader sdl3shadercompile)
if(ZH_SPIRV_VAL)
  set_tests_properties(test_shader_sdl PROPERTIES ENVIRONMENT "ZH_SPIRV_VAL=${ZH_SPIRV_VAL}")
endif()

# D6a: the glyph rasteriser against the font files' own hdmx, VDMX and gasp, read by the test's own
# parser, plus a regression golden keyed to the files' versions.  macOS's fonts; Linux later.
add_lib_test(test_glyph_rasteriser glyphrasteriser)
# D6b: FontCharsClass, the game's own, drawing with FreeType off Windows: GDI's arithmetic around the
# rasteriser, through the class's public face.
add_lib_test(test_fontchars ww3d2 glyphrasteriser wwsaveload wwlib wwmath wwdebug)

# The golden dump behind D3's byte-identical proof: every program in Tests/shader_cases.h, every
# target, one file each.  A tool, not a test: two runs are compared with diff -r.
add_executable(ffshader_dump ${TESTS}/ffshader_dump.cpp)
target_include_directories(ffshader_dump PRIVATE ${TESTS})
target_link_libraries(ffshader_dump PRIVATE ffvertex engineshader)

# A3b: FFReference, D3D9's fixed-function pipeline in double precision from Microsoft's documentation
# alone - the oracle A3's harness compares the GPU's fixed-function draws against.  Independent of
# ffshader/ffvertex by construction: it shares no code or headers with them (see ffreference.h).
add_library(ffreference STATIC ${TESTS}/ffreference/ffreference.cpp ${TESTS}/ffreference/ffprogram.cpp
  ${TESTS}/ffreference/ffcapture.cpp)
target_include_directories(ffreference PUBLIC ${TESTS})
add_lib_test(test_ffreference ffreference)
add_lib_test(test_ffprogram ffreference)
# its assembler test reads the water's ps.1.1 text from the game's own source, so it cannot drift
target_compile_definitions(test_ffprogram PRIVATE ZH_CODE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
# The engine's own D3D8 declarations in capture v3 against the device's D3D9 elements.  A tool, not a
# test: captures are made on demand (ZH_GPU_CAPTURE) and never committed.  ffdecl_captures <dir>...
add_executable(ffdecl_captures ${TESTS}/ffreference/ffdecl_captures.cpp)
target_link_libraries(ffdecl_captures PRIVATE ffreference)
# L2's oracle side: FFReference's own replay of a capture set, from ffcapture.h's reader (written from
# the format's description, not from the device or its harness), judged against the GPU's pixels.  A tool,
# not a test, for the same reason.  ffref_judge <dir> [--gpu <dump dir>] [--only draw_NNNNN]
add_executable(ffref_judge ${TESTS}/ffreference/ffref_judge.cpp)
target_link_libraries(ffref_judge PRIVATE ffreference)
if(ZH_GAME_DATA)
  set_tests_properties(test_ffprogram PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
endif()
# A3e: the programmable stages' token values against MinGW-w64's d3d8types.h and d3d9types.h (a second
# source for the numbers the reference pages leave out).  Skipped without x86_64-w64-mingw32-g++.
if(ZH_PLATFORM_POSIX)		# a bash script, and the MinGW cross-compiler it drives
  add_test(NAME ffprogram_values_check COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tools/ffprogram-values-check.sh)
  set_tests_properties(ffprogram_values_check PROPERTIES SKIP_RETURN_CODE 77)
endif()

# V1's check: every movie of the install through the Bink API, against its header and a golden
# middle frame, and one movie's sound through C4's mix on the null backend.  Needs ZH_GAME_DATA.
add_lib_test(test_binkvideo binkvideo milesaudio)
target_include_directories(test_binkvideo PRIVATE ${WW}/Miles6/include ${TESTS})
set_tests_properties(test_binkvideo PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)
if(ZH_GAME_DATA)
  set_tests_properties(test_binkvideo PROPERTIES ENVIRONMENT "ZH_AUDIO_BACKEND=null;ZH_DATA_DIR=${ZH_GAME_DATA}")
else()
  set_tests_properties(test_binkvideo PROPERTIES ENVIRONMENT "ZH_AUDIO_BACKEND=null")
endif()
endif()  # ZH_PLATFORM_POSIX - shader generator checks

# Tools/bigfile.py reads and writes the game's BIG archives (that is how the shipped INIs
# come out for editing).  Its selfcheck round-trips synthetic files, so it needs no game
# data - but it does need an interpreter, so it is only registered when one is present.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  # Every #include in the POSIX build must name its file in the file's own case.  Windows and a
  # default macOS volume fold case and Linux does not, so a miscased include builds on the first two
  # and is a missing header on the third; the first Linux build found 298.  This catches them on a
  # Mac, from compile_commands.json, without Linux.  POSIX only: that is where the file is exported.
  # Nothing the POSIX build compiles may carry text as wchar_t - four bytes here, two on Windows -
  # and no literal anywhere in the engine may hold a raw non-ASCII byte, which MSVC and clang read as
  # different characters.  WideChar is char16_t (B1); this finds what the compiler cannot, such as an
  # L'x' that converts silently.  Exceptions, each with its reason, in Tools/widechar_check_allow.txt.
  if(ZH_PLATFORM_POSIX)
    add_test(NAME widechar_check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/widechar_check.py
                     ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  if(ZH_PLATFORM_POSIX)
    add_test(NAME include_case_check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/include_case_check.py
                     ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_CURRENT_SOURCE_DIR})
    # Nothing may define _UNIX: not a vendored header (GameSpy's did, until vendor.sh renamed it),
    # not an engine header, not a compile flag.  Scans the vendored sources too, which git and this
    # machine's grep both skip, and plants a definition first to prove it can see one.
    add_test(NAME unix_define_check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/unix_define_check.py
                     ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  add_test(NAME bigfile_selfcheck
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/bigfile.py selfcheck)
  # Tools/vfx_tune.py rewrites those INIs in place; its selfcheck runs both rule sets over a
  # synthetic one and checks the block parser did not eat an End or duplicate a key.
  add_test(NAME vfx_tune_selfcheck
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/vfx_tune.py selfcheck)
  # Tools/order_cameos.py paints the command bar's order key cameos: seven whole pictures in the
  # places the mapped images name, told apart by colour, the same every run.
  add_test(NAME order_cameos_selfcheck
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/run_with_modules.py numpy,PIL
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/order_cameos.py selfcheck)
  # numpy and Pillow are not on every machine (a Mac's system Python has neither): without them the
  # test is skipped and says which is missing, instead of failing on an ImportError
  set_tests_properties(order_cameos_selfcheck PROPERTIES SKIP_RETURN_CODE 77)
  # The options screen is three tracked files that have to agree: TheOptionCatalog names a widget,
  # Data/Window/Menus/OptionsMenu.wnd has to carry it, and Data/Patch.str has to carry its label,
  # its tooltip and every combo box entry.  Nothing at build time notices a typo in any of them -
  # the control is just never filled in, or the screen draws the key name - so it is checked here.
  add_test(NAME optionsmenu_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/optionsmenu_layout.py selfcheck)
  # The peace time combo box is the same three-file agreement, in the two network lobby layouts:
  # the menu code names ComboBoxPeaceTime, the layouts have to carry it, and Patch.str has to carry
  # the entry captions, the tooltip, the corner countdown and the announcements.  A missing control
  # is silent - the menu null checks it and the option just never appears - so it is checked here.
  add_test(NAME peacetime_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/peacetime_layout.py selfcheck)

  # The lobby settings tab moves the host's settings onto a page of their own in all three lobbies,
  # and takes EA's superweapon checkbox out.  Same three-file agreement, plus one more failure the
  # null checks make silent: a control left behind on the old row would be drawn under the chat log.
  add_test(NAME lobbysettings_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/lobbysettings_layout.py selfcheck)

  # The skirmish and LAN rooms are laid out on one grid by arithmetic, and the mistake arithmetic
  # makes is silent: a control drawn over its neighbour, or a button a few pixels off the edge the
  # rest of its column stands on.  Both are checked here, with every window the menu code names.
  add_test(NAME lobbyroom_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/lobbyroom_layout.py selfcheck)

  # A translation is a .str of thousands of lines, and one printf directive lost or reordered in any
  # of them reads the wrong argument the moment that line is formatted.  The check parses the tracked
  # Turkish and German masters the way GameText.cpp does, and against Run/EnglishZH.big when it is
  # there.
  add_test(NAME gametext_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/gametext.py selfcheck)
endif()

# The x86_64/arm64 differential harness.  Rosetta 2 runs an x86_64 binary on an Apple Silicon Mac
# and clang cross-compiles to it, so one machine can build the same source for both architectures
# and compare what they compute.  That recovers the architecture half of the determinism question
# without waiting for a Windows machine - and only that half.  It is clang on both sides, so it
# proves nothing about MSVC's codegen, its optimiser or its 32-bit long; Tests/arch_diff/
# run_arch_diff.sh says so at length and should be read before anyone calls a green run here
# "determinism verified".
#
# It skips rather than fails where the cross toolchain or Rosetta is absent, because it is a
# capability of a developer's machine and not a requirement of the build.  ctest reads 77 as a skip.
# Both oracles below are shell scripts driving a cross toolchain or Rosetta, so POSIX only: registered on
# Windows they were "Process not started" failures in W2's first Windows ctest.
if(ZH_PLATFORM_POSIX)
  add_test(NAME arch_differential
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/arch_diff/run_arch_diff.sh
                   ${CMAKE_CURRENT_SOURCE_DIR}
                   ${CMAKE_CURRENT_BINARY_DIR}/arch_diff)
  set_tests_properties(arch_differential PROPERTIES SKIP_RETURN_CODE 77)
endif()

# The D3DX oracle: runs Microsoft's own D3DXVec4Transform machine code under Rosetta and compares
# d3dxportable.h against it.  It needs d3dx9_43.dll, which this repository does not carry, so it
# reads ZH_D3DX9_X64 at run time (a Steam install's Jun2010_d3dx9_43_x64.cab, or the DLL) and skips
# without it.  A green run matches two of the DLL's three bodies, not "Windows";
# Tests/d3dx_oracle/run_d3dx_oracle.sh says why.
if(ZH_PLATFORM_POSIX)
  add_test(NAME d3dx_oracle
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/d3dx_oracle/run_d3dx_oracle.sh
                   ${CMAKE_CURRENT_SOURCE_DIR}
                   ${CMAKE_CURRENT_BINARY_DIR}/d3dx_oracle)
  set_tests_properties(d3dx_oracle PROPERTIES SKIP_RETURN_CODE 77)
endif()
# A3e-asm: Microsoft's own D3DXAssembleShader (d3dx9_43 and the D3DCompiler_43 it loads, run as PE images
# under Rosetta) against FFReference's ps.1.1 assembler on the water's four programs.  Opt-in like
# d3dx_oracle: ZH_D3DX9_X64 at run time, skipped without it.  Tests/d3dx_assemble/run_assemble_oracle.sh.
if(ZH_PLATFORM_POSIX)
  add_test(NAME d3dx_assemble_oracle
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/d3dx_assemble/run_assemble_oracle.sh ${CMAKE_CURRENT_SOURCE_DIR})
  set_tests_properties(d3dx_assemble_oracle PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(RELEASE_DEBUG_LOGGING)
  if(ZH_PLATFORM_WINDOWS)
    # W3: not in a Debug build, which is EA's Debug build with its asserts (RELEASE_DEBUG_LOGGING turns
    # DEBUG_CRASHING off, while the libraries WWVegas builds without it call DebugCrash).  Every target has
    # _DEBUG there, so DEBUG_LOGGING's layouts still agree.  Release is as before.
    foreach(name gameengine gameenginedevice generals)
      target_compile_definitions(${name} PRIVATE $<$<NOT:$<CONFIG:Debug>>:RELEASE_DEBUG_LOGGING>)
    endforeach()
  else()
    target_compile_definitions(gameengine PRIVATE RELEASE_DEBUG_LOGGING)
    # The same off Windows, for the same reason: the device layer and the exe log, and a class laid out
    # under DEBUG_LOGGING (ScriptEngine's timing fields) has to be laid out alike in every TU that uses it.
    target_compile_definitions(posixdevice PRIVATE RELEASE_DEBUG_LOGGING)
    target_compile_definitions(sdldevice PRIVATE RELEASE_DEBUG_LOGGING)
    target_compile_definitions(sdlinput PRIVATE RELEASE_DEBUG_LOGGING)	# the SDL layer's input half
    target_compile_definitions(w3ddevice PRIVATE RELEASE_DEBUG_LOGGING)	# gameenginedevice's W3D half
    target_compile_definitions(generals PRIVATE RELEASE_DEBUG_LOGGING)
  endif()
endif()

# THREADING-ROADMAP.md section 0: the measurement build.  EA's PerfGather call sites are all
# over the frame already, but PerfTimer.h defines NO_PERF_TIMERS in *both* branches of its
# #if, so PERF_TIMERS is off in every configuration and every USE_PERF_TIMER compiles to
# nothing.  This switch turns them on without touching the shipping default: the timers read
# the TSC on every scope entry and exit, which is not free, so OFF is the right default and
# any perf claim has to say which build it came from.
#
# Only the three targets that include PerfTimer.h get the define.  It must not go on globally:
# WWVegas' headers reach GameEngine's Debug.h, and a mismatched define across libraries is how
# you get a link error at best and a silently different struct at worst.
option(PERF_TIMERS "Compile EA's PerfGather scopes in (measurement build, not for shipping)" OFF)
if(PERF_TIMERS)
  target_compile_definitions(gameengine PRIVATE PERF_TIMERS)
  # gameenginedevice and generals are only defined in a Windows configuration; see the platform
  # split at the top of this file.
  if(ZH_PLATFORM_WINDOWS)
    target_compile_definitions(gameenginedevice PRIVATE PERF_TIMERS)
    target_compile_definitions(generals PRIVATE PERF_TIMERS)
    # A measurement build is not the build anyone plays, and its post-build step copies into the
    # same Run/ directory the shipping one does.  Renaming the output keeps the two side by side -
    # which is also the only way to measure one against the other without rebuilding in between
    # (ai-batch.ps1 -Exe takes the name).
    set_target_properties(generals PROPERTIES OUTPUT_NAME generals_perf)
    message(STATUS "PERF_TIMERS ON - measurement build, timers cost real time; exe is generals_perf.exe")
  else()
    message(STATUS "PERF_TIMERS ON - measurement build, timers cost real time")
  endif()
endif()

# Every test's own user data directory, in the build tree (the harness's guard, test_user_data_guard.cpp,
# stops any test binary started without one).  Here, after the last add_test, so no test is missed and
# no later set_tests_properties replaces it.  One folder per test so tests run in parallel do not share
# an Options.ini or a Registry.ini.  Off Windows only: there the engine does not read the variable.
#
# And every test's own TMPDIR, in the build tree, made here at configure time (a deleted one comes back
# with the next configure).  The system's is one folder for every process of the user: ctest -j4 on
# 2026-09-26 lost a replay_check's work folder mid-run while other checkouts' harnesses, of several
# versions, ran in the same folder.  A test's temporary trees are its own now, and whatever is left
# behind stays in this build, where it can be seen.
#
# And every test on miniaudio's null backend (ZH_AUDIO_BACKEND=null, honoured where the one audio device
# is opened, AIL_quick_startup in miles_miniaudio.cpp): automated runs make no sound
# and show no window.  It reaches the games a test script starts too, so no ctest can make a sound, whatever
# its switches.  The null device still mixes in real time, which is what the audio tests measure.
if(ZH_PLATFORM_POSIX)
  get_property(_zh_tests DIRECTORY PROPERTY TESTS)
  foreach(_zh_test IN LISTS _zh_tests)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-tmp/${_zh_test})
    set_property(TEST ${_zh_test} APPEND PROPERTY ENVIRONMENT
      "ZH_USER_DATA_DIR=${CMAKE_BINARY_DIR}/test-user-data/${_zh_test}"
      "TMPDIR=${CMAKE_BINARY_DIR}/test-tmp/${_zh_test}"
      "ZH_AUDIO_BACKEND=null")
  endforeach()

  # The tests that build a farm of the install (a folder of links into it) never run at the same time:
  # one at a time is the belt to the braces above.  Readers that only open archives are not in it.
  foreach(_zh_test replay_check replay_check_app net_check overlay_crc_check ini_crlf_check packaging_resolution_check root_readonly_check
                   lod_first_run_check test_bigfilesystem test_install_textures test_milesaudiomanager
                   data_gone_check locomotor_check)
    if(TEST ${_zh_test})
      set_property(TEST ${_zh_test} APPEND PROPERTY RESOURCE_LOCK zh_install)
    endif()
  endforeach()

  # The SDL3 GPU selfchecks draw offscreen only.  On Apple they take SDL's dummy video driver and the
  # windowless Metal hint (decision 10), so a host with no window server (a worker over ssh) runs them
  # where it used to skip them, and every Mac takes the one path.  On Linux they take SDL's offscreen
  # video driver, which gives Vulkan a VK_EXT_headless_surface and needs no patch: without a display they
  # skipped ("SDL_Init: No available video device"), and with it all six pass on Mesa's ANV (L1b,
  # measured on a Linux x86_64 host).  Not dummy there: it has no Vulkan_CreateSurface, so SDL finds no GPU backend.
  if(APPLE)
    foreach(_zh_test sdl_gpu_frame_selfcheck sdl_program_cache_selfcheck sdl_pipeline_state_selfcheck
                     posix_gpu_draw_selfcheck ffref_gpu_selfcheck ffref_capture_selfcheck)
      if(TEST ${_zh_test})
        set_property(TEST ${_zh_test} APPEND PROPERTY ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" "ZH_SDL_GPU_METAL_WINDOWLESS=1")
      endif()
    endforeach()
  elseif(ZH_PLATFORM_LINUX)
    foreach(_zh_test sdl_gpu_frame_selfcheck sdl_program_cache_selfcheck sdl_pipeline_state_selfcheck
                     posix_gpu_draw_selfcheck ffref_gpu_selfcheck ffref_capture_selfcheck)
      if(TEST ${_zh_test})
        set_property(TEST ${_zh_test} APPEND PROPERTY ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
      endif()
    endforeach()
  endif()
endif()

# P2: on macOS our own code may not call an API newer than the deployment target without guarding it
# (@available / __builtin_available): that is a build error here, not a crash on a player's older Mac.
# Every target this file defines - the game, the port's libraries, the tests - but not the vendored
# projects add_subdirectory brings in (SDL3, freetype, glslang, SPIRV-Cross, litehtml, GameSpy), which
# guard their own calls and are not ours to fail.  Clang's unguarded-availability-new covers every API
# introduced since macOS 10.13, so it reaches everything above a 13.0 target.
if(APPLE)
  get_property(_zh_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_zh_target IN LISTS _zh_targets)
    get_target_property(_zh_type ${_zh_target} TYPE)
    if(_zh_type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
      target_compile_options(${_zh_target} PRIVATE
        $<$<COMPILE_LANGUAGE:C,CXX>:-Werror=unguarded-availability-new>)
    endif()
  endforeach()
endif()

# Say out loud which tests this configuration does not run.  A test that is
# absent from `ctest` looks identical to one that passed, and on a port where
# half the tree does not compile yet that distinction is the whole point.
get_property(_zh_deferred GLOBAL PROPERTY ZH_DEFERRED_TESTS)
if(_zh_deferred)
  list(LENGTH _zh_deferred _zh_deferred_count)
  string(REPLACE ";" ", " _zh_deferred_text "${_zh_deferred}")
  message(STATUS "ctest: ${_zh_deferred_count} test(s) registered DISABLED on this platform "
                 "- ${_zh_deferred_text}")
  message(STATUS "ctest: they are built EXCLUDE_FROM_ALL and will not link until their task lands")
endif()
