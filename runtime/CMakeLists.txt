cmake_minimum_required(VERSION 3.24)
project(dedsec_rt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "dedsec_rt must be built for x64 (run from an x64 Native Tools prompt or vcvars64.bat)")
endif()

if(WIN32)
  # Root-signature flag fix, shared by the d3d12_xs shim and its tests.
  add_library(dedsec_rt_d3d12_xs_rootsig STATIC
    shims/d3d12_xs_rootsig.cpp
  )
  target_include_directories(dedsec_rt_d3d12_xs_rootsig PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_rt_d3d12_xs_rootsig PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_rt_d3d12_xs_rootsig PRIVATE /W4 /permissive- /utf-8)

  # Portable root-signature model + packer to user SGPRs (no d3d12.h, no windows.h).
  add_library(dedsec_rt_root_signature STATIC
    src/root_signature.cpp
  )
  target_include_directories(dedsec_rt_root_signature PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(dedsec_rt_root_signature PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_rt_root_signature PRIVATE /W4 /permissive- /utf-8)

  # Portable contract test: model, packer, canonical codec and RTS0 hypothesis.
  # Needs neither d3d12.lib nor the game; runs on Windows CI like the rest here.
  add_executable(root_signature_test
    test/root_signature_test.cpp
  )
  target_link_libraries(root_signature_test PRIVATE dedsec_rt_root_signature)
  target_compile_definitions(root_signature_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(root_signature_test PRIVATE /W4 /permissive- /utf-8)
  target_link_options(root_signature_test PRIVATE /OPT:NOREF)

  # XTaskQueue/XAsync from microsoft/libHttpClient (MIT), vendored in third_party/xtaskqueue; shims/xtaskqueue/pch.h
  # stands in for libHttpClient's pch.h so only the task queue builds. Used by the xgameruntime shim.
  set(XTASKQUEUE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/xtaskqueue)
  add_library(dedsec_rt_xtaskqueue STATIC
    ${XTASKQUEUE_DIR}/Source/Task/TaskQueue.cpp
    ${XTASKQUEUE_DIR}/Source/Task/AsyncLib.cpp
    ${XTASKQUEUE_DIR}/Source/Task/ThreadPool_win32.cpp
    ${XTASKQUEUE_DIR}/Source/Task/WaitTimer_win32.cpp
  )
  target_include_directories(dedsec_rt_xtaskqueue
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shims/xtaskqueue ${XTASKQUEUE_DIR}/Source/Task ${XTASKQUEUE_DIR}/Source/Common
    PUBLIC ${XTASKQUEUE_DIR}/Include)
  target_compile_definitions(dedsec_rt_xtaskqueue PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_rt_xtaskqueue PRIVATE /utf-8)  # third-party code: its own warning level
  target_link_libraries(dedsec_rt_xtaskqueue PUBLIC ole32)

  # DedSec GPU Tier Detection & Scalable Presets
  add_library(dedsec_gpu_tier STATIC
    shims/dedsec_gpu_tier.cpp
  )
  target_include_directories(dedsec_gpu_tier PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_gpu_tier PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_gpu_tier PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(dedsec_gpu_tier PRIVATE d3d12 dxgi)

  # DedSec Persistent Shader Cache
  add_library(dedsec_shader_cache STATIC
    shims/dedsec_shader_cache.cpp
  )
  target_include_directories(dedsec_shader_cache PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_shader_cache PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_shader_cache PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(dedsec_shader_cache PRIVATE d3d12 dxgi shlwapi)

  # DedSec PSO Pre-warming System
  add_library(dedsec_pso_warmup STATIC
    shims/dedsec_pso_warmup.cpp
  )
  target_include_directories(dedsec_pso_warmup PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_pso_warmup PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_pso_warmup PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(dedsec_pso_warmup PRIVATE d3d12 dxgi)

  # DedSec Async Compute Dispatcher
  add_library(dedsec_async_compute STATIC
    shims/dedsec_async_compute.cpp
  )
  target_include_directories(dedsec_async_compute PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_async_compute PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_async_compute PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(dedsec_async_compute PRIVATE d3d12 dxgi)

  # DedSec Hi-Z Occlusion Culling
  add_library(dedsec_occlusion_cull STATIC
    shims/dedsec_occlusion_cull.cpp
  )
  target_include_directories(dedsec_occlusion_cull PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_occlusion_cull PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_occlusion_cull PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(dedsec_occlusion_cull PRIVATE d3d12 dxgi)

  # DedSec Variable Rate Shading Tile Classification
  add_library(dedsec_vrs STATIC
    shims/dedsec_vrs.cpp
  )
  target_include_directories(dedsec_vrs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_vrs PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_vrs PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(dedsec_vrs PRIVATE d3d12 dxgi)

  # DedSec FSR3 Frame Generation + Upscaling (DX12 Native Pipeline)
  add_library(dedsec_fsr3 STATIC
    shims/dedsec_fsr3.cpp
    shims/dedsec_fsr3_hook.cpp
  )
  target_include_directories(dedsec_fsr3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_fsr3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_fsr3 PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(dedsec_fsr3 PRIVATE d3d12 dxgi dedsec_gpu_tier dedsec_shader_cache dedsec_pso_warmup dedsec_async_compute)

  # DedSec RDNA2 → HLSL SM 6.6 Translator
  add_library(dedsec_rdna2_translator STATIC
    shims/rdna2_to_hlsl.cpp
  )
  target_include_directories(dedsec_rdna2_translator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_rdna2_translator PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_rdna2_translator PRIVATE /W4 /permissive- /utf-8)

  # DedSec RDNA2 Translator unit test
  add_executable(rdna2_translator_test
    test/rdna2_translator_test.cpp
  )
  target_link_libraries(rdna2_translator_test PRIVATE dedsec_rdna2_translator)
  target_compile_definitions(rdna2_translator_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(rdna2_translator_test PRIVATE /W4 /permissive- /utf-8)
  add_test(NAME rdna2_translator COMMAND rdna2_translator_test)

  # DedSec DXC Runtime Compiler (dynamic dxcompiler.dll loading)
  add_library(dedsec_dxc_compiler STATIC
    shims/dedsec_dxc_compiler.cpp
  )
  target_include_directories(dedsec_dxc_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_dxc_compiler PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_dxc_compiler PRIVATE /W4 /permissive- /utf-8)

  # DedSec DXC Compiler unit test
  add_executable(dxc_compiler_test
    test/dxc_compiler_test.cpp
  )
  target_link_libraries(dxc_compiler_test PRIVATE dedsec_dxc_compiler)
  target_compile_definitions(dxc_compiler_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dxc_compiler_test PRIVATE /W4 /permissive- /utf-8)
  add_test(NAME dxc_compiler COMMAND dxc_compiler_test)

  # DedSec RDNA2 MIMG (Memory Image) Translator — 111 texture instruction forms
  add_library(dedsec_rdna2_mimg STATIC
    shims/rdna2_mimg.cpp
  )
  target_include_directories(dedsec_rdna2_mimg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_rdna2_mimg PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_rdna2_mimg PRIVATE /W4 /permissive- /utf-8)

  # DedSec MIMG Translator unit test
  add_executable(rdna2_mimg_test
    test/rdna2_mimg_test.cpp
  )
  target_link_libraries(rdna2_mimg_test PRIVATE dedsec_rdna2_mimg)
  target_compile_definitions(rdna2_mimg_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(rdna2_mimg_test PRIVATE /W4 /permissive- /utf-8)
  add_test(NAME rdna2_mimg COMMAND rdna2_mimg_test)

  # DedSec Xbox→PC Input Adapter (GameInput + XInput + Keyboard fallback)
  add_library(dedsec_xbox_input STATIC
    shims/xbox_input_adapter.cpp
  )
  target_include_directories(dedsec_xbox_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_xbox_input PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_xbox_input PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(dedsec_xbox_input PRIVATE xinput)

  # DedSec Xbox Input Adapter unit test
  add_executable(xbox_input_adapter_test
    test/xbox_input_adapter_test.cpp
  )
  target_link_libraries(xbox_input_adapter_test PRIVATE dedsec_xbox_input)
  target_compile_definitions(xbox_input_adapter_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(xbox_input_adapter_test PRIVATE /W4 /permissive- /utf-8)
  add_test(NAME xbox_input_adapter COMMAND xbox_input_adapter_test)

  # DedSec GDeflate Decompressor — Xbox Series asset compression format
  add_library(dedsec_gdeflate STATIC
    shims/gdeflate_decompressor.cpp
  )
  target_include_directories(dedsec_gdeflate PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_gdeflate PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_gdeflate PRIVATE /W4 /permissive- /utf-8)

  # DedSec XAudio2 Shim — Xbox audio API over Windows XAudio2/WASAPI
  add_library(dedsec_xaudio2 STATIC
    shims/xaudio2_shim.cpp
  )
  target_include_directories(dedsec_xaudio2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_xaudio2 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_xaudio2 PRIVATE /W4 /permissive- /utf-8)
  target_link_libraries(dedsec_xaudio2 PRIVATE xaudio2)

  # DedSec RDNA2 FLAT/MUBUF Translator — 133 flat + 73 buffer instruction forms
  add_library(dedsec_rdna2_flat_mubuf STATIC
    shims/rdna2_flat_mubuf.cpp
  )
  target_include_directories(dedsec_rdna2_flat_mubuf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_rdna2_flat_mubuf PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_rdna2_flat_mubuf PRIVATE /W4 /permissive- /utf-8)

  # DedSec RDNA2 DPP8/DPP16 + SMEM Translator — 200 wave primitives + 84 scalar memory forms
  add_library(dedsec_rdna2_dpp_smem STATIC
    shims/rdna2_dpp_smem.cpp
  )
  target_include_directories(dedsec_rdna2_dpp_smem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_rdna2_dpp_smem PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_rdna2_dpp_smem PRIVATE /W4 /permissive- /utf-8)

  # DedSec RDNA2 VOP3 Translator — 425 vector ALU 3-operand instruction forms
  add_library(dedsec_rdna2_vop3 STATIC
    shims/rdna2_vop3.cpp
  )
  target_include_directories(dedsec_rdna2_vop3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(dedsec_rdna2_vop3 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(dedsec_rdna2_vop3 PRIVATE /W4 /permissive- /utf-8)

  add_executable(dedsec_rt
    src/main.cpp
    src/pe_loader.cpp
    shims/registry.cpp
    shims/xmem.cpp
    shims/d3d12_xs.cpp
    shims/d3d12_xs_device.cpp
    shims/xgameruntime.cpp
  )
  target_compile_definitions(dedsec_rt PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(dedsec_rt PRIVATE /W4 /permissive- /utf-8)
  # Static shim tables register themselves at startup; keep the linker from dropping their objects.
  target_link_options(dedsec_rt PRIVATE /OPT:NOREF)
  target_link_libraries(dedsec_rt PRIVATE dedsec_rt_d3d12_xs_rootsig dedsec_rt_xtaskqueue dedsec_fsr3 dedsec_gpu_tier dedsec_shader_cache dedsec_pso_warmup dedsec_async_compute dedsec_occlusion_cull dedsec_vrs dedsec_rdna2_translator dedsec_dxc_compiler dedsec_rdna2_mimg dedsec_xbox_input dedsec_gdeflate dedsec_xaudio2 dedsec_rdna2_flat_mubuf dedsec_rdna2_dpp_smem dedsec_rdna2_vop3 psapi)

  # Root-signature forwarding test: needs a real PC d3d12.lib to serialize the synthetic descriptors.
  # find_library searches %LIB% (a Developer Prompt), not the Windows SDK the Visual Studio generator links
  # against, so also look in that SDK's um/x64 folder.
  set(_sdk_lib_hints)
  if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKLibVersion})
    list(APPEND _sdk_lib_hints "$ENV{WindowsSdkDir}Lib/$ENV{WindowsSDKLibVersion}um/x64")
  endif()
  get_filename_component(_kits_root
    "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
  if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION AND EXISTS "${_kits_root}")
    list(APPEND _sdk_lib_hints "${_kits_root}/Lib/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/um/x64")
  endif()
  find_library(D3D12_LIBRARY NAMES d3d12 HINTS ${_sdk_lib_hints})
  find_library(DXGI_LIBRARY NAMES dxgi HINTS ${_sdk_lib_hints})
  if(D3D12_LIBRARY)
    add_executable(root_sig_test
      test/root_sig_test.cpp
      shims/registry.cpp
      shims/d3d12_xs.cpp
      shims/d3d12_xs_device.cpp
    )
    target_link_libraries(root_sig_test PRIVATE dedsec_rt_d3d12_xs_rootsig ${D3D12_LIBRARY})
    target_compile_definitions(root_sig_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(root_sig_test PRIVATE /W4 /permissive- /utf-8)
    target_link_options(root_sig_test PRIVATE /OPT:NOREF)

    # RTS0 parser oracle: synthetic descriptors only, serialized by the real PC
    # d3d12.dll and parsed back by rt::rootsig::parse_rts0. A layout mismatch
    # fails here with the blob bytes, never silently in a draw.
    add_executable(root_signature_oracle_test
      test/root_signature_oracle_test.cpp
    )
    target_link_libraries(root_signature_oracle_test PRIVATE dedsec_rt_root_signature ${D3D12_LIBRARY})
    target_compile_definitions(root_signature_oracle_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(root_signature_oracle_test PRIVATE /W4 /permissive- /utf-8)
    target_link_options(root_signature_oracle_test PRIVATE /OPT:NOREF)

    # Xbox-ABI device proxy test: WARP PC device, no game executable. Needs DXGI for the WARP adapter.
    if(DXGI_LIBRARY)
      add_executable(device_proxy_test
        test/device_proxy_test.cpp
        shims/registry.cpp
        shims/d3d12_xs.cpp
        shims/d3d12_xs_device.cpp
      )
      target_link_libraries(device_proxy_test PRIVATE dedsec_rt_d3d12_xs_rootsig ${D3D12_LIBRARY} ${DXGI_LIBRARY})
      target_compile_definitions(device_proxy_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
      target_compile_options(device_proxy_test PRIVATE /W4 /permissive- /utf-8)
      target_link_options(device_proxy_test PRIVATE /OPT:NOREF)
    else()
      message(STATUS "dxgi.lib not found: device_proxy_test is not built")
    endif()
  else()
    message(STATUS "d3d12.lib not found: root_sig_test and device_proxy_test are not built")
  endif()

  # xmem shim ABI test: resolves the registered xmem.dll exports itself, so xmem.cpp and registry.cpp are compiled
  # into this executable instead of reusing dedsec_rt's objects (which would duplicate their symbols).
  add_executable(xmem_test
    test/xmem_test.cpp
    shims/xmem.cpp
    shims/registry.cpp
  )
  target_include_directories(xmem_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(xmem_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(xmem_test PRIVATE /W4 /permissive- /utf-8)
  target_link_options(xmem_test PRIVATE /OPT:NOREF)
  target_link_libraries(xmem_test PRIVATE psapi)

  # Offline GDK runtime shim (XGameRuntime, XSystem, XTaskQueue, XAsync), driven through the shim registry the way
  # a title calls it at boot. Like xmem_test it compiles its own registry.cpp.
  add_executable(xgameruntime_test
    test/xgameruntime_test.cpp
    shims/xgameruntime.cpp
    shims/registry.cpp
  )
  target_include_directories(xgameruntime_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shims)
  target_compile_definitions(xgameruntime_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(xgameruntime_test PRIVATE /W4 /permissive- /utf-8)
  target_link_options(xgameruntime_test PRIVATE /OPT:NOREF)
  target_link_libraries(xgameruntime_test PRIVATE dedsec_rt_xtaskqueue)

else()
  message(STATUS "dedsec_rt requires Windows; only Vulkan tools are available on Linux")
endif()

include(CTest)
if(BUILD_TESTING)
  if(WIN32)
    # Loader tests. They build their own PE images, so they need neither the game nor any other input.
    add_executable(loader_test
      test/loader_test.cpp
      src/pe_loader.cpp
      shims/registry.cpp
    )
    target_compile_definitions(loader_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_compile_options(loader_test PRIVATE /W4 /permissive- /utf-8)
    target_link_options(loader_test PRIVATE /OPT:NOREF)
    add_test(NAME loader COMMAND loader_test)
    # PE loader tests use two locally built fixtures: a DLL with exports, imports and relocations, and an EXE that
    # imports it so Loader::load exercises recursive module loading. They contain no game data.
    set(TINY_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures/$<CONFIG>")

    add_library(tiny_dll SHARED test/fixtures/tiny_dll.cpp)
    target_compile_definitions(tiny_dll PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tiny_dll PRIVATE /W4 /permissive- /utf-8 /GS-)
    set_target_properties(tiny_dll PROPERTIES
      OUTPUT_NAME tiny_dll
      PREFIX ""
      RUNTIME_OUTPUT_DIRECTORY "${TINY_FIXTURE_DIR}"
      ARCHIVE_OUTPUT_DIRECTORY "${TINY_FIXTURE_DIR}"
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    add_executable(tiny_exe test/fixtures/tiny_exe.cpp)
    target_link_libraries(tiny_exe PRIVATE tiny_dll)
    target_compile_definitions(tiny_exe PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(tiny_exe PRIVATE /W4 /permissive- /utf-8 /GS-)
    set_target_properties(tiny_exe PROPERTIES
      OUTPUT_NAME tiny_exe
      RUNTIME_OUTPUT_DIRECTORY "${TINY_FIXTURE_DIR}"
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    # pe_loader.cpp needs the shim registry for shims::find/make_unimplemented_thunk. The xmem and d3d12_xs tables
    # are linked too so the optional DEDSEC_GAME_DIR run classifies the shipped modules' imports exactly like
    # dedsec_rt does (no d3d12.lib is needed: d3d12_xs resolves its host entry points at run time).
    add_executable(pe_loader_test
      test/pe_loader_test.cpp
      src/pe_loader.cpp
      shims/registry.cpp
      shims/xmem.cpp
      shims/d3d12_xs.cpp
      shims/d3d12_xs_device.cpp
    )
    target_include_directories(pe_loader_test PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/shims
    )
    target_compile_definitions(pe_loader_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(pe_loader_test PRIVATE /W4 /permissive- /utf-8)
    target_link_options(pe_loader_test PRIVATE /OPT:NOREF)
    target_link_libraries(pe_loader_test PRIVATE dedsec_rt_d3d12_xs_rootsig psapi)
    add_dependencies(pe_loader_test tiny_dll tiny_exe)
    add_test(NAME pe_loader COMMAND pe_loader_test "$<TARGET_FILE:tiny_dll>" "$<TARGET_FILE:tiny_exe>")
  endif()

  if(TARGET device_proxy_test)
    add_test(NAME device_proxy COMMAND device_proxy_test)
  endif()
  if(TARGET root_sig_test)
    add_test(NAME root_sig COMMAND root_sig_test)
  endif()
  add_executable(command_list_test test/command_list_test.cpp)
  add_test(NAME command_list COMMAND command_list_test)
  add_executable(descriptor_table_test test/descriptor_table_test.cpp)
  add_test(NAME descriptor_table COMMAND descriptor_table_test)
  if(WIN32)
    # Optional: skips unless DEDSEC_GAME_DIR and DEDSEC_MOVIE are set.
    add_executable(bink_decode_test
      test/bink_decode_test.cpp
      src/pe_loader.cpp
      shims/registry.cpp
      shims/xmem.cpp
    )
    target_compile_definitions(bink_decode_test PRIVATE
      WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(bink_decode_test PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_link_options(bink_decode_test PRIVATE /OPT:NOREF)
    target_link_libraries(bink_decode_test PRIVATE psapi)
    add_test(NAME bink_decode COMMAND bink_decode_test)

    # Optional: skips unless DEDSEC_GAME_DIR and DEDSEC_MOVIE_AUDIO are set.
    add_executable(bink_audio_test
      test/bink_audio_test.cpp
      src/pe_loader.cpp
      shims/registry.cpp
      shims/xmem.cpp
    )
    target_compile_definitions(bink_audio_test PRIVATE
      WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(bink_audio_test PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_link_options(bink_audio_test PRIVATE /OPT:NOREF)
    target_link_libraries(bink_audio_test PRIVATE psapi)
    add_test(NAME bink_audio COMMAND bink_audio_test)
  endif()
  if(WIN32 AND TARGET xmem_test)
    add_test(NAME xmem COMMAND xmem_test)
  endif()
  if(WIN32 AND TARGET xgameruntime_test)
    add_test(NAME xgameruntime COMMAND xgameruntime_test)
  endif()
  if(WIN32 AND TARGET root_signature_test)
    add_test(NAME root_signature COMMAND root_signature_test)
  endif()
  if(WIN32 AND TARGET root_signature_oracle_test)
    add_test(NAME root_signature_oracle COMMAND root_signature_oracle_test)
  endif()
endif()

if(WIN32)
option(DEDSEC_D3D12_SHIM_TESTS "Build the standalone D3D12 root-signature shim test" OFF)
if(DEDSEC_D3D12_SHIM_TESTS)
  enable_testing()
  add_executable(d3d12_xs_root_signature_test
    test/d3d12_xs_root_signature_test.cpp
    shims/d3d12_xs.cpp
    shims/registry.cpp
    shims/d3d12_xs_device.cpp
  )
  target_include_directories(d3d12_xs_root_signature_test PRIVATE shims)
  target_compile_definitions(d3d12_xs_root_signature_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(d3d12_xs_root_signature_test PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_link_options(d3d12_xs_root_signature_test PRIVATE /OPT:NOREF)
  target_link_libraries(d3d12_xs_root_signature_test PRIVATE dedsec_rt_d3d12_xs_rootsig d3d12 dxgi)
  add_test(NAME d3d12_xs_root_signature COMMAND d3d12_xs_root_signature_test)
endif()
endif()

# GPU test and pipeline tools load Vulkan at run time.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
  foreach(tool vk_harness vk_texture_test vk_compute_test vk_store_test vk_pipeline_check vk_command_replay_test
          vk_bindless_test)
    add_executable(${tool} test/${tool}.cpp)
    target_link_libraries(${tool} PRIVATE Vulkan::Headers ${CMAKE_DL_LIBS})
    if(WIN32)
      target_compile_definitions(${tool} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
  endforeach()
  # D3D12 COM objects over the Vulkan backend. Their method stubs are generated from the SDK's d3d12.h at build
  # time (tools/gen_d3d12_stubs.py), so nothing derived from the SDK header is checked in.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(WIN32 AND Python3_FOUND)
    set(DEDSEC_STUBS ${CMAKE_CURRENT_BINARY_DIR}/generated/d3d12_stubs.h)
    add_custom_command(
      OUTPUT ${DEDSEC_STUBS}
      COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_d3d12_stubs.py --out ${DEDSEC_STUBS}
              ID3D12GraphicsCommandList8 ID3D12CommandQueue ID3D12CommandAllocator ID3D12Fence1
              ID3D12Device11 ID3D12DescriptorHeap ID3D12Resource
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../tools/gen_d3d12_stubs.py
      COMMENT "Generating D3D12 COM stubs"
      VERBATIM)
    add_library(dedsec_rt_d3d12_com STATIC shims/d3d12_com.cpp shims/d3d12_vulkan_device.cpp shims/d3d12_xs_device.cpp
                ${DEDSEC_STUBS})   # d3d12_vulkan_device.cpp reads the PC device behind the proxy (#224)
    target_include_directories(dedsec_rt_d3d12_com PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_libraries(dedsec_rt_d3d12_com PUBLIC Vulkan::Headers)
    target_compile_definitions(dedsec_rt_d3d12_com PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(dedsec_rt_d3d12_com PRIVATE /W4 /permissive- /utf-8)

    add_executable(d3d12_com_test test/d3d12_com_test.cpp)
    target_link_libraries(d3d12_com_test PRIVATE dedsec_rt_d3d12_com)
    target_compile_definitions(d3d12_com_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(d3d12_com_test PRIVATE /W4 /permissive- /utf-8)

    # GCL8's QueryInterface and its one method need an allocator and a list, not a Vulkan device.
    add_executable(d3d12_gcl8_test test/d3d12_gcl8_test.cpp)
    target_link_libraries(d3d12_gcl8_test PRIVATE dedsec_rt_d3d12_com)
    target_compile_definitions(d3d12_gcl8_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(d3d12_gcl8_test PRIVATE /W4 /permissive- /utf-8)
    add_test(NAME d3d12_gcl8 COMMAND d3d12_gcl8_test)

    # The Vulkan device behind the Xbox-ABI proxy, end to end.
    add_executable(d3d12_vulkan_device_test test/d3d12_vulkan_device_test.cpp)
    target_link_libraries(d3d12_vulkan_device_test PRIVATE dedsec_rt_d3d12_com)
    target_compile_definitions(d3d12_vulkan_device_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(d3d12_vulkan_device_test PRIVATE /W4 /permissive- /utf-8)

    # Descriptors through the proxy: copies between heaps.
    add_executable(d3d12_descriptor_test test/d3d12_descriptor_test.cpp shims/d3d12_xs_device.cpp)
    target_link_libraries(d3d12_descriptor_test PRIVATE dedsec_rt_d3d12_com)
    target_compile_definitions(d3d12_descriptor_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(d3d12_descriptor_test PRIVATE /W4 /permissive- /utf-8)

    # The loader can create this device instead of the host's (D3D12XboxCreateDevice, DEDSEC_D3D12=vulkan).
    if(TARGET dedsec_rt)
      target_link_libraries(dedsec_rt PRIVATE dedsec_rt_d3d12_com)
      target_compile_definitions(dedsec_rt PRIVATE DEDSEC_VULKAN_DEVICE)
    endif()
  else()
    message(STATUS "Python 3 or Windows missing: the D3D12 COM objects and d3d12_com_test are not built")
  endif()

  # Needs only the Vulkan headers, not a GPU.
  if(BUILD_TESTING)
    add_executable(d3d12_state_map_test test/d3d12_state_map_test.cpp)
    target_link_libraries(d3d12_state_map_test PRIVATE Vulkan::Headers)
    if(WIN32)
      target_compile_definitions(d3d12_state_map_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    add_test(NAME d3d12_state_map COMMAND d3d12_state_map_test)
    # GFX10 T#/S#/V# descriptor encoding and its Vulkan mappings (src/gfx10_descriptors.h).
    add_executable(gfx10_descriptors_test test/gfx10_descriptors_test.cpp)
    target_link_libraries(gfx10_descriptors_test PRIVATE Vulkan::Headers)
    add_test(NAME gfx10_descriptors COMMAND gfx10_descriptors_test)
  endif()
else()
  message(STATUS "Vulkan SDK not found: GPU test tools, vk_pipeline_check, d3d12_state_map_test and "
                 "gfx10_descriptors_test are not built")
endif()
