cmake_minimum_required(VERSION 3.13)

project(NativeWrappers VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define source files
set(SOURCES
        src/support.cpp
        src/pe_loader.cpp
        src/pe_sidecar.cpp
        src/winlibs.cpp
        src/cpu_features.cpp
        src/dll_loader.cpp
)

# Include directories
include_directories(src)

# Add executable
add_executable(
        ${PROJECT_NAME}
        ${SOURCES}
        src/main.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Wrapper libraries
add_library(
        RecastDetour
        SHARED
        ${SOURCES}
        src/RecastDetour.cpp
)

add_library(
        Havok
        SHARED
        ${SOURCES}
        src/Havok.cpp
)

set_target_properties(RecastDetour PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_library(
        VRageNative
        SHARED
        ${SOURCES}
        src/VRageNative.cpp
)

set_target_properties(Havok PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_target_properties(VRageNative PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_library(
        D3DCompiler
        SHARED
        ${SOURCES}
        src/D3DCompiler.cpp
)

add_library(
        KytheraV2
        SHARED
        src/KytheraV2.cpp
)

add_library(
        PlatformWindows
        SHARED
        src/PlatformWindows.cpp
)

add_library(
        Slug
        SHARED
        ${SOURCES}
        src/Slug.cpp
)

add_library(
        Physics
        SHARED
        ${SOURCES}
        src/Physics.cpp
)

add_library(
        Voxels
        SHARED
        ${SOURCES}
        src/Voxels.cpp
)

set_target_properties(D3DCompiler PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_target_properties(KytheraV2 PROPERTIES
        OUTPUT_NAME "VRage.KytheraV2.Native"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_target_properties(PlatformWindows PROPERTIES
        OUTPUT_NAME "VRage.Platform.Windows.Native"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_target_properties(Slug PROPERTIES
        OUTPUT_NAME "VRage.Slug.Native"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_target_properties(Physics PROPERTIES
        OUTPUT_NAME "VRage.Physics.Native"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_target_properties(Voxels PROPERTIES
        OUTPUT_NAME "VRage.Voxels.Native"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Link libraries (if any)
target_link_libraries(${PROJECT_NAME} PRIVATE c ${CMAKE_DL_LIBS})
target_link_libraries(RecastDetour PRIVATE c ${CMAKE_DL_LIBS})
target_link_libraries(Havok PRIVATE c ${CMAKE_DL_LIBS})
target_link_libraries(VRageNative PRIVATE c ${CMAKE_DL_LIBS})
target_link_libraries(D3DCompiler PRIVATE c ${CMAKE_DL_LIBS})
target_link_libraries(KytheraV2 PRIVATE c)
target_link_libraries(PlatformWindows PRIVATE c)
target_link_libraries(Slug PRIVATE c ${CMAKE_DL_LIBS})
target_link_options(Slug PRIVATE -Wl,--hash-style=sysv)
add_custom_command(TARGET Slug POST_BUILD
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/rename_slug_exports.py" "$<TARGET_FILE:Slug>"
)
target_link_libraries(Physics PRIVATE c ${CMAKE_DL_LIBS})
target_link_options(Physics PRIVATE -Wl,--hash-style=sysv)
add_custom_command(TARGET Physics POST_BUILD
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/rename_physics_exports.py"
                "$<TARGET_FILE:Physics>" "${CMAKE_SOURCE_DIR}/src/Physics.exports"
)
target_link_libraries(Voxels PRIVATE c ${CMAKE_DL_LIBS})

# Compiler flags
#-Werror -Wextra
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -fpermissive)
target_compile_options(RecastDetour PRIVATE -Wall -fpermissive -fPIC)
target_compile_options(Havok PRIVATE -Wall -fpermissive -fPIC)
target_compile_options(VRageNative PRIVATE -Wall -fpermissive -fPIC)
target_compile_options(D3DCompiler PRIVATE -Wall -fpermissive -fPIC)
target_compile_options(KytheraV2 PRIVATE -Wall -fpermissive -fPIC)
target_compile_options(PlatformWindows PRIVATE -Wall -fpermissive -fPIC)
# Physics and Slug export MSVC-decorated names. At -O2 and above GCC splits
# cold paths into a separate section and appends `.cold` to the mangled symbol
# name, producing identifiers such as
# `??0AppliedImpulses$hknpConstraintData$$QEAA$AEBU01$$Z.cold` that the GNU
# assembler cannot parse. Keeping the blocks together avoids the `.cold`
# symbols entirely, so these targets build in every configuration.
target_compile_options(Slug PRIVATE -Wall -fpermissive -fPIC -fno-reorder-blocks-and-partition)
target_compile_options(Physics PRIVATE -Wall -fpermissive -fPIC -fno-reorder-blocks-and-partition)
target_compile_options(Voxels PRIVATE -Wall -fpermissive -fPIC)

add_executable(generate_pe_sidecar
        src/pe_sidecar.cpp
        tools/generate_pe_sidecar.cpp)
target_compile_options(generate_pe_sidecar PRIVATE -Wall)

add_executable(d3dcompiler_tool
        tools/d3dcompiler_tool.cpp)
target_link_libraries(d3dcompiler_tool PRIVATE D3DCompiler)
target_compile_options(d3dcompiler_tool PRIVATE -Wall)

set(NATIVE_DLL_DIR "" CACHE PATH "Directory containing the Windows native DLLs")

include(CTest)
if(BUILD_TESTING)
    find_program(PYTHON3_EXECUTABLE python3)
    if(NOT PYTHON3_EXECUTABLE)
        message(FATAL_ERROR "python3 is required for generator tests")
    endif()

    add_test(NAME se1_generators
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/se1_generators.py)
    add_test(NAME se2_generators
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/se2_generators.py)

    add_executable(thread_id_dll_attach_test
            ${SOURCES}
            tests/thread_id_dll_attach.cpp
    )
    target_compile_definitions(thread_id_dll_attach_test PRIVATE NATIVE_WRAPPERS_TESTING)
    target_link_libraries(thread_id_dll_attach_test PRIVATE c ${CMAKE_DL_LIBS})
    target_compile_options(thread_id_dll_attach_test PRIVATE -Wall -fpermissive)
    add_test(NAME thread_id_dll_attach COMMAND thread_id_dll_attach_test)
    set_tests_properties(thread_id_dll_attach PROPERTIES TIMEOUT 5)

    add_executable(windows_memory_test
            ${SOURCES}
            tests/windows_memory.cpp
    )
    target_link_libraries(windows_memory_test PRIVATE c ${CMAKE_DL_LIBS})
    target_compile_options(windows_memory_test PRIVATE -Wall -fpermissive)
    add_test(NAME windows_memory COMMAND windows_memory_test)
    set_tests_properties(windows_memory PROPERTIES
            TIMEOUT 60
            ENVIRONMENT "ASAN_OPTIONS=allocator_may_return_null=1:handle_segv=0")

    if(NATIVE_DLL_DIR AND EXISTS "${NATIVE_DLL_DIR}/d3dcompiler_47.dll")
        add_test(NAME d3dcompiler_preprocess
                COMMAND ${PYTHON3_EXECUTABLE}
                        ${CMAKE_SOURCE_DIR}/tests/d3dcompiler_preprocess.py
                        "$<TARGET_FILE:d3dcompiler_tool>"
                        "${NATIVE_DLL_DIR}/d3dcompiler_47.dll")
        set_tests_properties(d3dcompiler_preprocess PROPERTIES TIMEOUT 60)
    endif()

    if(NATIVE_DLL_DIR AND EXISTS "${NATIVE_DLL_DIR}/Havok.dll")
        add_test(NAME pe_sidecar_generation
                COMMAND generate_pe_sidecar
                        "${NATIVE_DLL_DIR}/Havok.dll"
                        "${CMAKE_BINARY_DIR}/generated-Havok.dll")
        add_executable(havok_crash_test tests/havok_crash.cpp)
        target_link_libraries(havok_crash_test PRIVATE Havok)
        add_test(NAME havok_unwind
                COMMAND havok_crash_test "${NATIVE_DLL_DIR}/Havok.dll" --trace)
        set_tests_properties(havok_unwind PROPERTIES
                PASS_REGULAR_EXPRESSION "HkShape_CastRayCollectSingleHit")

        add_executable(havok_memory_test tests/havok_memory.cpp)
        target_link_libraries(havok_memory_test PRIVATE Havok)
        add_test(NAME havok_memory
                COMMAND havok_memory_test
                        "${NATIVE_DLL_DIR}/Havok.dll"
                        "${CMAKE_BINARY_DIR}/havok-memory-Havok.dll")
        set_tests_properties(havok_memory PROPERTIES
                TIMEOUT 120
                FAIL_REGULAR_EXPRESSION "duplicate free suppressed")
    endif()
endif()

# Install target (optional)
#install(TARGETS ${PROJECT_NAME} DESTINATION bin)
