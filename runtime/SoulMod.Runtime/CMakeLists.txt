# SoulMod.Runtime — promoted operation-driven runtime target.
#
# Builds the actual Aurie/YYToolkit runtime module graph: the operation-driven
# lifecycle, package parser (with JSON validation), operation executor/resolver,
# YYToolkit host adapter, and hooks/logger/managers. The framework roots are
# REQUIRED and validated: the build refuses to configure without real Aurie and
# YYToolkit headers. No empty default pretends framework support.
#
# The stale two-source POC build (SoulModEngine.cpp + HookRegistry.cpp, inline
# trampolines / fixed virtual addresses) and PocHook.cpp are EXCLUDED. They stay
# on disk as POC reference until checkbox 6 parity, never compiled into the
# product. Scratch remains the parity oracle, never a silent fallback.

# --- Promoted source graph ---------------------------------------------------
set(SOULMOD_RUNTIME_PROMOTED_SOURCES
    src/dllmain.cpp             # operation-driven lifecycle entry (Init -> Load -> Resolve -> Hook)
    src/ModLoader.cpp           # package parser: manifest.json + operations.json (validates JSON)
    src/PackageValidation.cpp   # package and asset SHA-256 validation
    src/JsonValidator.cpp       # recursive-descent JSON structural validator
    src/GlobalJsonEpoch.cpp     # verified global JSON epoch with atomic rollback
    src/OperationsResolver.cpp  # operation executor / duplicate+conflict resolver
    src/DependencyGraph.cpp     # topological load-order resolver (loadBefore/loadAfter/dependencies)
    src/YYToolkitAdapter.cpp    # host adapter: process detection + VFS CreateFileW IAT redirect
    src/GameHookEngine.cpp      # GameMaker asset_get_index binding + draw_set_font hook
    src/HookManager.cpp         # before/after/replace hook registry
    src/Logger.cpp              # structured leveled logger
    src/StringManager.cpp       # string replacement manager
    src/FontManager.cpp         # font redirect/register manager
)

# --- Excluded from the production target -------------------------------------
set(SOULMOD_RUNTIME_EXCLUDED_SOURCES
    src/PocHook.cpp             # fixed-VA (0x4F5CD0) direct trampoline POC
    src/SoulModEngine.cpp       # inline InstallInlineTrampoline + hardcoded d:\UTDT-SoulMod\scratch POC
    src/HookRegistry.cpp        # CreateInlineHook trampoline + global-namespace type pollution
)

# --- Omission guard: every promoted module must exist on disk ----------------
foreach(_promoted_source ${SOULMOD_RUNTIME_PROMOTED_SOURCES})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_promoted_source}")
        message(FATAL_ERROR
            "SoulMod.Runtime: required promoted source missing: ${_promoted_source}\n"
            "The production target refuses to configure without its complete promoted module graph."
        )
    endif()
endforeach()

# --- Exclusion guard: trampoline/POC must not enter the promoted graph --------
foreach(_excluded_source ${SOULMOD_RUNTIME_EXCLUDED_SOURCES})
    list(FIND SOULMOD_RUNTIME_PROMOTED_SOURCES "${_excluded_source}" _excluded_index)
    if(NOT _excluded_index EQUAL -1)
        message(FATAL_ERROR
            "SoulMod.Runtime: excluded trampoline/POC source must not be in the promoted graph: ${_excluded_source}"
        )
    endif()
endforeach()

# --- Required Aurie/YYToolkit framework roots (no silent empty default) -------
# The runtime is an Aurie module that accesses GameMaker through YYToolkit. The
# build MUST have real framework headers; an empty root is a configuration
# error, not a supported degraded mode.
if(NOT SOULMOD_AURIE_ROOT)
    message(FATAL_ERROR
        "SoulMod.Runtime requires -DSOULMOD_AURIE_ROOT=<path> pointing at the Aurie framework "
        "include tree (must contain Aurie/shared.hpp). The runtime is an Aurie module; an empty "
        "root is not supported."
    )
endif()
if(NOT SOULMOD_YYTOOLKIT_ROOT)
    message(FATAL_ERROR
        "SoulMod.Runtime requires -DSOULMOD_YYTOOLKIT_ROOT=<path> pointing at the YYToolkit "
        "include tree (must contain YYToolkit/YYTK_Shared.hpp). The runtime accesses GameMaker "
        "through YYToolkit; an empty root is not supported."
    )
endif()
set(SOULMOD_RUNTIME_REQUIRED_FRAMEWORK_HEADERS
    "${SOULMOD_AURIE_ROOT}/Aurie/shared.hpp"
    "${SOULMOD_YYTOOLKIT_ROOT}/YYToolkit/YYTK_Shared.hpp"
)
foreach(_required_header ${SOULMOD_RUNTIME_REQUIRED_FRAMEWORK_HEADERS})
    if(NOT EXISTS "${_required_header}")
        message(FATAL_ERROR
            "SoulMod.Runtime: required framework header missing: ${_required_header}\n"
            "Provide a complete Aurie/YYToolkit source tree via SOULMOD_AURIE_ROOT and "
            "SOULMOD_YYTOOLKIT_ROOT. The build does not fall back to scratch or stub headers."
        )
    endif()
endforeach()

# --- Core object library: promoted modules shared by the DLL and the tests ----
# dllmain.cpp owns DllMain and cannot link into a test executable. Every other
# promoted module compiles once into this OBJECT library, consumed by both the
# SHARED runtime and the native tests, so tests exercise the real production
# module graph rather than standalone copies.
set(SOULMOD_RUNTIME_LIFECYCLE_SOURCE src/dllmain.cpp)
set(SOULMOD_RUNTIME_CORE_SOURCES ${SOULMOD_RUNTIME_PROMOTED_SOURCES})
list(REMOVE_ITEM SOULMOD_RUNTIME_CORE_SOURCES ${SOULMOD_RUNTIME_LIFECYCLE_SOURCE})

add_library(SoulMod.Runtime.Core OBJECT ${SOULMOD_RUNTIME_CORE_SOURCES})
target_include_directories(SoulMod.Runtime.Core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SOULMOD_AURIE_ROOT}
        ${SOULMOD_YYTOOLKIT_ROOT}
)
target_link_libraries(SoulMod.Runtime.Core PUBLIC psapi bcrypt)
target_compile_definitions(SoulMod.Runtime.Core
    PUBLIC
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)
if(MSVC)
    target_compile_options(SoulMod.Runtime.Core PRIVATE /W4 /permissive-)
endif()

# --- Product SHARED library: core modules + lifecycle entry ------------------
add_library(SoulMod.Runtime SHARED
    ${SOULMOD_RUNTIME_LIFECYCLE_SOURCE}
    $<TARGET_OBJECTS:SoulMod.Runtime.Core>
)
target_link_libraries(SoulMod.Runtime PRIVATE psapi bcrypt)
target_include_directories(SoulMod.Runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${SOULMOD_AURIE_ROOT}
        ${SOULMOD_YYTOOLKIT_ROOT}
)
target_compile_definitions(SoulMod.Runtime
    PRIVATE
        SOULMOD_RUNTIME_EXPORTS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)
if(MSVC)
    target_compile_options(SoulMod.Runtime PRIVATE /W4 /permissive-)
endif()

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/SoulMod.Runtime.build-identity-$<CONFIG>.txt"
    CONTENT "target=SoulMod.Runtime\nbinary=$<TARGET_FILE:SoulMod.Runtime>\ncore_objects=$<TARGET_OBJECTS:SoulMod.Runtime.Core>\nsources=${SOULMOD_RUNTIME_PROMOTED_SOURCES}\n"
)

# --- Deferred effective-source guards ----------------------------------------
# DEFER runs after every subdirectory has configured, catching reassignment,
# list(APPEND), and target_sources injections wherever they occur.
function(soulmod_assert_final_source_graph)
    get_target_property(_core_effective_sources SoulMod.Runtime.Core SOURCES)
    set(_expected_core_sources ${SOULMOD_RUNTIME_PROMOTED_SOURCES})
    list(REMOVE_ITEM _expected_core_sources ${SOULMOD_RUNTIME_LIFECYCLE_SOURCE})
    foreach(_required_source ${_expected_core_sources})
        list(FIND _core_effective_sources "${_required_source}" _required_index)
        if(_required_index EQUAL -1)
            message(FATAL_ERROR "SoulMod.Runtime.Core: required source missing from effective SOURCES: ${_required_source}")
        endif()
    endforeach()
    foreach(_effective_source ${_core_effective_sources})
        list(FIND _expected_core_sources "${_effective_source}" _expected_index)
        if(_expected_index EQUAL -1)
            message(FATAL_ERROR "SoulMod.Runtime.Core: unexpected source in effective SOURCES: ${_effective_source}")
        endif()
    endforeach()

    get_target_property(_runtime_effective_sources SoulMod.Runtime SOURCES)
    foreach(_required_source ${SOULMOD_RUNTIME_LIFECYCLE_SOURCE} "$<TARGET_OBJECTS:SoulMod.Runtime.Core>")
        list(FIND _runtime_effective_sources "${_required_source}" _required_index)
        if(_required_index EQUAL -1)
            message(FATAL_ERROR "SoulMod.Runtime: required source missing from effective SOURCES: ${_required_source}")
        endif()
    endforeach()
    foreach(_effective_source ${_runtime_effective_sources})
        if(NOT _effective_source STREQUAL "${SOULMOD_RUNTIME_LIFECYCLE_SOURCE}" AND
           NOT _effective_source STREQUAL "$<TARGET_OBJECTS:SoulMod.Runtime.Core>")
            message(FATAL_ERROR "SoulMod.Runtime: unexpected source in effective SOURCES: ${_effective_source}")
        endif()
    endforeach()
endfunction()
cmake_language(DEFER CALL soulmod_assert_final_source_graph)

# --- Native contract tests (Windows lane) ------------------------------------
option(SOULMOD_RUNTIME_BUILD_TESTS "Build SoulMod.Runtime native contract tests" ON)
if(SOULMOD_RUNTIME_BUILD_TESTS)
    add_subdirectory(tests)
endif()
