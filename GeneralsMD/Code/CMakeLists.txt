cmake_minimum_required(VERSION 3.20)

# The zip goes to machines that have never installed a Visual Studio redistributable, and
# generals.exe imported MSVCP140.dll and VCRUNTIME140.dll by name.  That fails in the Windows
# loader before a line of our code runs, so there is no DebugLogFile.txt and no crash file to
# send back - the same undiagnosable failure the retail Miles and Bink DLLs are packaged to
# avoid.  Link the CRT into the exe and into d3d8.dll instead of depending on a machine having
# it.  CMAKE_POLICY_DEFAULT is what reaches the vendored subdirectories: d3d8to9 declares
# cmake_minimum_required(3.12), which predates CMP0091, and would otherwise ignore the setting
# below and keep importing the DLL.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(GeneralsMD CXX C)

# x64 only.  The 32-bit build was dropped once this one was proven against it: same simulation to
# the bit, same picture to within sub-pixel sampling, and sound and video of its own that do not
# depend on the two 32-bit DLLs the game shipped with.  Nothing here is written for x86 any more.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Configure for x64: cmake -A x64")
endif()

set(WW ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/WWVegas)
set(COMP ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/Compression)

# The third-party sources EA stripped out, which this repository does not carry either.  build.bat
# fetches them before it configures; say so here rather than let the generator report a missing
# source file per library.
foreach(vendored
    ${COMP}/ZLib/deflate.c
    ${COMP}/LZHCompress/CompLibSource/Lzhl.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/DirectX/Include/d3d8.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/GameSpy/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/litehtml/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/nanosvg/nanosvgrast.h)
  if(NOT EXISTS ${vendored})
    message(FATAL_ERROR
      "${vendored} is missing. Run Tools\\vendor.ps1 (build.bat does it for you).")
  endif()
endforeach()

add_compile_definitions(WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)

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
endif()

# ---- wwdebug --------------------------------------------------------------
add_library(wwdebug STATIC
  ${WW}/WWDebug/wwdebug.cpp
  ${WW}/WWDebug/wwmemlog.cpp
  ${WW}/WWDebug/wwprofile.cpp)
target_include_directories(wwdebug PUBLIC ${WW}/WWDebug PRIVATE ${WW}/WWLib ${WW}/Wwutil)

# ---- wwutil ---------------------------------------------------------------
add_library(wwutil STATIC
  ${WW}/Wwutil/mathutil.cpp
  ${WW}/Wwutil/miscutil.cpp)
target_include_directories(wwutil PUBLIC ${WW}/Wwutil PRIVATE ${WW}/WWLib ${WW}/WWMath)
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
target_include_directories(wwmath PUBLIC ${WW}/WWMath PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)
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

# Round-trip check over every codec CompressionManager exposes.
add_executable(compression_selfcheck ${COMP}/test_compression.cpp)
target_link_libraries(compression_selfcheck PRIVATE compression)

# Guards the hand-rolled Matrix3D::Get_Inverse that replaced D3DXMatrixInverse.
add_executable(wwmath_selfcheck ${WW}/WWMath/test_inverse.cpp)
target_include_directories(wwmath_selfcheck PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)
target_link_libraries(wwmath_selfcheck PRIVATE wwmath wwsaveload wwlib wwdebug)

# ---- wwlib ----------------------------------------------------------------
add_library(wwlib STATIC
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
target_include_directories(wwlib PUBLIC ${WW}/WWLib PRIVATE ${WW}/WWMath ${WW}/Wwutil)
# winmm: timeGetTime/timeBeginPeriod.  version: verchk.cpp's GetFileVersionInfo,
# which Wwutil drags in for Get_File_Id_String.
target_link_libraries(wwlib PRIVATE wwdebug PUBLIC winmm version)

# ---- wwsaveload -----------------------------------------------------------
add_library(wwsaveload STATIC
  ${WW}/WWSaveLoad/definition.cpp          ${WW}/WWSaveLoad/definitionfactory.cpp
  ${WW}/WWSaveLoad/definitionfactorymgr.cpp ${WW}/WWSaveLoad/definitionmgr.cpp
  ${WW}/WWSaveLoad/parameter.cpp           ${WW}/WWSaveLoad/persistfactory.cpp
  ${WW}/WWSaveLoad/pointerremap.cpp        ${WW}/WWSaveLoad/saveload.cpp
  ${WW}/WWSaveLoad/saveloadstatus.cpp      ${WW}/WWSaveLoad/saveloadsubsystem.cpp
  ${WW}/WWSaveLoad/twiddler.cpp            ${WW}/WWSaveLoad/wwsaveload.cpp)
target_include_directories(wwsaveload PUBLIC ${WW}/WWSaveLoad PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/Wwutil)
target_link_libraries(wwsaveload PRIVATE wwdebug wwlib wwmath)

# ---- d3dx9runtime ---------------------------------------------------------
# The D3DX9 entry points the renderer binds at run time, in a library of their own
# because GameEngine needs the math half without the renderer: BezierSegment carries
# D3DXMATRIX and DumbProjectileBehavior steers a shell with it.
add_library(d3dx9runtime STATIC
  ${WW}/WW3D2/d3dx9runtime.cpp ${WW}/WW3D2/d3d8shadertranslate.cpp)
set_target_properties(d3dx9runtime PROPERTIES CXX_STANDARD 17)
target_include_directories(d3dx9runtime PUBLIC ${WW}/WW3D2)
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
# dx8webbrowser.cpp stays inert (ENABLE_EMBEDDED_BROWSER unset).
add_library(ww3d2 STATIC

  ${WW}/WW3D2/FramGrab.cpp ${WW}/WW3D2/aabtree.cpp ${WW}/WW3D2/aabtreebuilder.cpp 
  ${WW}/WW3D2/agg_def.cpp ${WW}/WW3D2/animatedsoundmgr.cpp ${WW}/WW3D2/animobj.cpp 
  ${WW}/WW3D2/assetmgr.cpp ${WW}/WW3D2/assetstatus.cpp ${WW}/WW3D2/bitmaphandler.cpp 
  ${WW}/WW3D2/bmp2d.cpp ${WW}/WW3D2/boxrobj.cpp ${WW}/WW3D2/bwrender.cpp 
  ${WW}/WW3D2/camera.cpp ${WW}/WW3D2/collect.cpp ${WW}/WW3D2/coltest.cpp 
  ${WW}/WW3D2/composite.cpp ${WW}/WW3D2/dazzle.cpp ${WW}/WW3D2/ddsfile.cpp 
  ${WW}/WW3D2/decalmsh.cpp ${WW}/WW3D2/decalsys.cpp ${WW}/WW3D2/distlod.cpp 
  ${WW}/WW3D2/dx8caps.cpp ${WW}/WW3D2/dx8fvf.cpp ${WW}/WW3D2/dx8indexbuffer.cpp 
  ${WW}/WW3D2/dx8polygonrenderer.cpp ${WW}/WW3D2/dx8renderer.cpp ${WW}/WW3D2/dx8rendererdebugger.cpp 
  ${WW}/WW3D2/dx8texman.cpp ${WW}/WW3D2/dx8vertexbuffer.cpp ${WW}/WW3D2/dx8webbrowser.cpp 
  ${WW}/WW3D2/dx8wrapper.cpp ${WW}/WW3D2/dynamesh.cpp ${WW}/WW3D2/ffprobe.cpp
  ${WW}/WW3D2/ffshadercache.cpp ${WW}/WW3D2/dx11runtime.cpp ${WW}/WW3D2/font3d.cpp
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
# DirectX 8 SDK and the d3d8.dll the renderer used to load; that path is gone, and
# test_dx8_smoke.cpp stays in the tree unbuilt as the record of what it measured.
# d3d9.h and d3d9.lib come from the Windows SDK, so this target never sees
# Libraries/DirectX/Include.
add_executable(dx9_smoke ${WW}/WW3D2/test_dx9_smoke.cpp)
target_link_libraries(dx9_smoke PRIVATE d3dx9runtime user32)

# ===========================================================================
# Phase 2 - remaining support libs.  None of these touch DX8.
# ===========================================================================
set(LIBSRC ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source)

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
target_link_libraries(debug PUBLIC dbghelp comctl32 ws2_32)
# "debug" is also a target_link_libraries config keyword, so it can never be
# passed as a plain library name.  Link this alias instead.
add_library(debuglib ALIAS debug)

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

# ---- wwdownload -----------------------------------------------------------
# FTP patch downloader.  Its wlib/resource.h includes are all commented out in
# the shipped source, so it needs nothing but winsock.
add_library(wwdownload STATIC
  ${WW}/WWDownload/Download.cpp ${WW}/WWDownload/FTP.CPP
  ${WW}/WWDownload/registry.cpp ${WW}/WWDownload/urlBuilder.cpp)
target_include_directories(wwdownload PUBLIC ${WW}/WWDownload ${WW})
target_link_libraries(wwdownload PUBLIC wsock32 winmm)

# ---- benchmark ------------------------------------------------------------
# BYTEmark sources are missing; benchmark_stub.c stands in for them.  See the
# file header for why a constant is enough and when to revisit.
add_library(benchmark STATIC ${LIBSRC}/Benchmark/benchmark_stub.c)
target_include_directories(benchmark PUBLIC ${LIBSRC}/Benchmark)

# ---- eabrowserdispatch ----------------------------------------------------
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

# ---- gamespy --------------------------------------------------------------
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

add_library(gameengine STATIC ${GAMEENGINE_SOURCES})
target_include_directories(gameengine PUBLIC
  ${GE}/Include ${GE}/Include/Precompiled
  ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
  ${WW} ${WW}/WWLib
  # The game includes "GameSpy/Peer/Peer.h", the SDK's own headers include each
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
target_link_libraries(gameengine PUBLIC
  ww3d2 wwlib wwmath wwsaveload wwdebug wwutil compression debuglib wwdownload
  benchmark eabrowserdispatch profile gamespy litehtml
  dinput8 wininet imagehlp imm32)

# ===========================================================================
# Phase 4 - GameEngineDevice + Main -> generals.exe
# ===========================================================================
set(GED ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice)
set(STUBS ${CMAKE_CURRENT_SOURCE_DIR}/Stubs)

file(GLOB_RECURSE GAMEENGINEDEVICE_SOURCES ${GED}/Source/*.cpp)
# On disk but absent from GameEngineDevice.dsp: the DirectInput mouse (the
# shipping build uses the Win32 message mouse instead).
list(REMOVE_ITEM GAMEENGINEDEVICE_SOURCES
  ${GED}/Source/Win32Device/GameClient/Win32DIMouse.cpp)
# Sound and video are the game's own.  Miles and Bink were binary-only, and the DLLs the retail
# game ships are 32-bit, so there is nothing for a 64-bit exe to bind to: what it links instead is
# those two APIs written over XAudio2 and FFmpeg.  FFmpeg decodes binkvideo and binkaudio natively,
# so the .bik files on disk play as they are, and swscale converts each frame into whatever surface
# format the video player asks for.  MilesAudioManager.cpp and BinkVideoPlayer.cpp are unchanged and
# still call AIL_* and Bink*; the reconstructed headers in Miles6/include and Bink/include are still
# the declarations.  miles_smoke and bink_smoke are the checks.
set(FFMPEG_DIR ${LIBSRC}/FFmpeg/dist)
if(NOT EXISTS ${FFMPEG_DIR}/include)
  message(FATAL_ERROR
    "FFmpeg is missing from Libraries/Source/FFmpeg/dist - see the vendored-libs rule.")
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
# BuildVersion.h and GeneratedVersion.h are normally written by Tools/
# buildVersionUpdate and Tools/versionUpdate as a pre-build step, from a build
# counter in the source tree.  Both are three-line headers, so write them at
# configure time instead - it keeps two more VC6 tools out of the port, and
# Windows wants UAC elevation for exe names containing "version" anyway.
set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(WRITE ${GENERATED}/BuildVersion.h
"// Generated by CMake, standing in for Tools/buildVersionUpdate.\n"
"#define VERSION_STRING \"v2.1.0\"\n"
"#define VERSION_MAJOR 2\n"
"#define VERSION_MINOR 1\n"
"#define VERSION_BUILDNUM 0\n")
file(WRITE ${GENERATED}/GeneratedVersion.h
"// Generated by CMake, standing in for Tools/versionUpdate.\n"
"#define LOCAL_BUILD_STRING \"0\"\n"
"#define VERSION_LOCALBUILDNUM 0\n"
"#define VERSION_BUILDUSER \"cmake\"\n"
"#define VERSION_BUILDLOC \"${CMAKE_HOST_SYSTEM_NAME}\"\n")

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
  # The startup splash.  WinMain loads it from a loose file next to the exe, so
  # the fork's own artwork is a tracked master here rather than a hand-made file
  # in gitignored Run/.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Install_Final.bmp
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # The three command bar plates per side, the smoke signals' ground marks and the
  # SupW uplink's pink laser strip (Tools/pink_laser.py).  A loose
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

# ===========================================================================
# Tests.  Run with:  ctest --test-dir build -C Release --output-on-failure
# ===========================================================================
enable_testing()

set(TESTS ${CMAKE_CURRENT_SOURCE_DIR}/Tests)

# Shared runner for the micro-harness in Tests/test_harness.h; every test
# binary links it instead of writing its own main().
add_library(test_harness STATIC ${TESTS}/test_main.cpp)
target_include_directories(test_harness PUBLIC ${TESTS})

# add_lib_test(<name> <libs...>) -> Tests/<name>.cpp, built and registered.
function(add_lib_test name)
  add_executable(${name} ${TESTS}/${name}.cpp)
  target_link_libraries(${name} PRIVATE test_harness ${ARGN})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_lib_test(test_wwmath wwmath wwlib wwsaveload wwdebug)
target_include_directories(test_wwmath PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)

add_lib_test(test_wwlib wwlib wwmath wwsaveload wwdebug)
target_include_directories(test_wwlib PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWSaveLoad ${WW}/Wwutil)
# generals.exe is linked /LARGEADDRESSAWARE, and that flag changes what
# GlobalMemoryStatus saturates its 32-bit fields to (0xFFFFFFFF, not 0x7FFFFFFF).
# cpudetect_memory_fits_in_the_signed_int_its_callers_use only means anything in a
# binary that sees the same value the game does.
target_link_options(test_wwlib PRIVATE /LARGEADDRESSAWARE)

add_lib_test(test_wwsaveload wwsaveload wwlib wwmath wwdebug)
target_include_directories(test_wwsaveload PRIVATE ${WW}/WWSaveLoad ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/Wwutil)

add_lib_test(test_wwutil wwutil wwmath wwlib wwdebug)
target_include_directories(test_wwutil PRIVATE ${WW}/Wwutil ${WW}/WWLib ${WW}/WWMath)

add_lib_test(test_compression compression)

add_lib_test(test_ww3d2 ww3d2 wwsaveload wwlib wwmath wwdebug)
target_include_directories(test_ww3d2 PRIVATE
  ${WW}/WW3D2 ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil)

add_lib_test(test_ffshader ffshader)
add_lib_test(test_dx11device dx11device)
add_lib_test(test_dx11state dx11state)
add_lib_test(test_dx11layout dx11layout)
add_lib_test(test_ffvertex ffvertex)
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

add_lib_test(test_wwdownload wwdownload)
target_include_directories(test_wwdownload PRIVATE ${WW}/WWDownload)

add_lib_test(test_debug debuglib)

add_lib_test(test_support benchmark eabrowserdispatch)

# Phase 3 checkpoint: boots the memory manager, name keys and file system, then
# runs a real INI file through the parser.
add_lib_test(test_gameengine gameengine)
# the well-known Dict keys and a handful of device/exe entry points live in
# GameEngineDevice, which Phase 4 ports; stand-ins until then.
target_sources(test_gameengine PRIVATE ${TESTS}/test_gameengine_stubs.cpp)
set_target_properties(test_gameengine PROPERTIES CXX_STANDARD 17)
# simulation_uses_no_runtime_trig reads the engine's own sources back off disk to prove nothing in
# GameLogic or Common has gone back to calling sinf().
# fxlist_reforged_ini_parses_and_keeps_its_light runs the fork's own FXList file through the real
# INI parser, so it needs the tracked master rather than whatever sits in Run/.
# the_options_menu_buttons_all_wear_one_font reads the tracked layout master, not Run/'s copy.
# the_spectator_page_names_only_on_off_options reads the tracked page master the same way.
target_compile_definitions(test_gameengine PRIVATE
  GAMEENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/GameEngine"
  FXLIST_REFORGED_INI="${CMAKE_CURRENT_SOURCE_DIR}/Data/INI/FXListReforged.ini"
  OPTIONS_MENU_WND="${CMAKE_CURRENT_SOURCE_DIR}/Data/Window/Menus/OptionsMenu.wnd"
  SPECTATOR_HTML="${CMAKE_CURRENT_SOURCE_DIR}/Data/Window/Html/Spectator.html"
  PLATE_TEXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Data/Art/Textures")

# The three hand-written self-checks predate the harness; run them from ctest
# too so one command covers everything.
add_test(NAME wwmath_selfcheck COMMAND wwmath_selfcheck)
add_test(NAME compression_selfcheck COMMAND compression_selfcheck)
# dx9_smoke creates a device the way the renderer does, with nothing next to it to
# translate anything, and puts the D3D8 calls that have no D3D9 counterpart through their
# replacements.  "-msaa 4" adds the multisampled back buffer and the plain render target
# with a depth buffer of its own that DX8Wrapper::_Get_Non_MultiSampled_Depth_Buffer pairs
# for the screen filters.
add_test(NAME dx9_smoke COMMAND dx9_smoke)
add_test(NAME dx9_smoke_msaa COMMAND dx9_smoke -msaa 4)

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

# Tools/bigfile.py reads and writes the game's BIG archives (that is how the shipped INIs
# come out for editing).  Its selfcheck round-trips synthetic files, so it needs no game
# data - but it does need an interpreter, so it is only registered when one is present.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME bigfile_selfcheck
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/bigfile.py selfcheck)
  # The fixed-point conversion's ratchet: the count of Real, float literals, sqrt and the float
  # vector types left in GameLogic must not grow past Tools/fixcheck.limit at the repo root.
  add_test(NAME fixcheck
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../../Tools/fixcheck.py)
  # Tools/vfx_tune.py rewrites those INIs in place; its selfcheck runs both rule sets over a
  # synthetic one and checks the block parser did not eat an End or duplicate a key.
  add_test(NAME vfx_tune_selfcheck
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/vfx_tune.py selfcheck)
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
  # Turkish master the way GameText.cpp does, and against Run/EnglishZH.big when it is there.
  add_test(NAME gametext_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/gametext.py selfcheck)
endif()

if(RELEASE_DEBUG_LOGGING)
  target_compile_definitions(gameengine PRIVATE RELEASE_DEBUG_LOGGING)
  target_compile_definitions(gameenginedevice PRIVATE RELEASE_DEBUG_LOGGING)
  target_compile_definitions(generals PRIVATE RELEASE_DEBUG_LOGGING)
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
  target_compile_definitions(gameenginedevice PRIVATE PERF_TIMERS)
  target_compile_definitions(generals PRIVATE PERF_TIMERS)
  # A measurement build is not the build anyone plays, and its post-build step copies into the
  # same Run/ directory the shipping one does.  Renaming the output keeps the two side by side -
  # which is also the only way to measure one against the other without rebuilding in between
  # (ai-batch.ps1 -Exe takes the name).
  set_target_properties(generals PROPERTIES OUTPUT_NAME generals_perf)
  message(STATUS "PERF_TIMERS ON - measurement build, timers cost real time; exe is generals_perf.exe")
endif()
