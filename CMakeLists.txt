cmake_minimum_required(VERSION 3.10)

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

# Link libraries (if any)
target_link_libraries(${PROJECT_NAME} PRIVATE c ${CMAKE_DL_LIBS})
target_link_libraries(RecastDetour PRIVATE c ${CMAKE_DL_LIBS})
target_link_libraries(Havok PRIVATE c ${CMAKE_DL_LIBS})
target_link_libraries(VRageNative PRIVATE c ${CMAKE_DL_LIBS})
target_link_libraries(D3DCompiler PRIVATE c ${CMAKE_DL_LIBS})
target_link_libraries(KytheraV2 PRIVATE c)
target_link_libraries(PlatformWindows PRIVATE c)

# Compiler flags
#-Werror -Wextra
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -fpermissive)
target_compile_options(RecastDetour PRIVATE -Wall -fpermissive -fPIC)
target_compile_options(Havok PRIVATE -Wall -fpermissive -fPIC)
target_compile_options(VRageNative PRIVATE -Wall -fpermissive -fPIC)
target_compile_options(D3DCompiler PRIVATE -Wall -fpermissive -fPIC)
target_compile_options(KytheraV2 PRIVATE -Wall -fpermissive -fPIC)
target_compile_options(PlatformWindows PRIVATE -Wall -fpermissive -fPIC)

add_executable(generate_pe_sidecar
        src/pe_sidecar.cpp
        tools/generate_pe_sidecar.cpp)
target_compile_options(generate_pe_sidecar PRIVATE -Wall)

set(NATIVE_DLL_DIR "" CACHE PATH "Directory containing the Windows native DLLs")

include(CTest)
if(BUILD_TESTING)
    find_program(PYTHON3_EXECUTABLE python3)
    if(NOT PYTHON3_EXECUTABLE)
        message(FATAL_ERROR "python3 is required for the Havok generator test")
    endif()

    add_test(NAME havok_generator
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/havok_generator.py)

    add_executable(thread_id_dll_attach_test
            ${SOURCES}
            tests/thread_id_dll_attach.cpp
    )
    target_compile_definitions(thread_id_dll_attach_test PRIVATE NATIVE_WRAPPERS_TESTING)
    target_link_libraries(thread_id_dll_attach_test PRIVATE c ${CMAKE_DL_LIBS})
    target_compile_options(thread_id_dll_attach_test PRIVATE -Wall -fpermissive)
    add_test(NAME thread_id_dll_attach COMMAND thread_id_dll_attach_test)
    set_tests_properties(thread_id_dll_attach PROPERTIES TIMEOUT 5)

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
    endif()
endif()

# Install target (optional)
#install(TARGETS ${PROJECT_NAME} DESTINATION bin)
