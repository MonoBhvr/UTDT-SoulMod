# SoulMod.Runtime native contract tests.
#
# Each test links the production SoulMod.Runtime.Core OBJECT library (the same
# promoted module objects compiled into the SHARED runtime), so a passing test
# verifies the real product module graph links and behaves correctly -- not a
# standalone copy. Tests never compile dllmain.cpp (it owns DllMain). Windows
# lane only: the promoted sources consume the Win32 API. On non-Windows hosts
# the deterministic proof is the Python source-map validator.

set(SOULMOD_RUNTIME_TEST_DOUBLE YYToolkitRValueTestDouble.cpp)
add_executable(SoulModRuntimeDependencyGraphTests DependencyGraphTests.cpp ${SOULMOD_RUNTIME_TEST_DOUBLE})
target_link_libraries(SoulModRuntimeDependencyGraphTests PRIVATE SoulMod.Runtime.Core psapi bcrypt)
add_test(NAME SoulModRuntime.DependencyGraph COMMAND SoulModRuntimeDependencyGraphTests)

add_executable(SoulModRuntimeOperationsResolverTests OperationsResolverTests.cpp ${SOULMOD_RUNTIME_TEST_DOUBLE})
target_link_libraries(SoulModRuntimeOperationsResolverTests PRIVATE SoulMod.Runtime.Core psapi bcrypt)
add_test(NAME SoulModRuntime.OperationsResolver COMMAND SoulModRuntimeOperationsResolverTests)

add_executable(SoulModRuntimePackageParserTests PackageParserTests.cpp ${SOULMOD_RUNTIME_TEST_DOUBLE})
target_link_libraries(SoulModRuntimePackageParserTests PRIVATE SoulMod.Runtime.Core psapi bcrypt)
add_test(NAME SoulModRuntime.PackageParser COMMAND SoulModRuntimePackageParserTests)

add_executable(SoulModRuntimeJsonAndPackageValidationTests JsonAndPackageValidationTests.cpp ${SOULMOD_RUNTIME_TEST_DOUBLE})
target_link_libraries(SoulModRuntimeJsonAndPackageValidationTests PRIVATE SoulMod.Runtime.Core psapi bcrypt)
add_test(NAME SoulModRuntime.JsonAndPackageValidation COMMAND SoulModRuntimeJsonAndPackageValidationTests)

add_executable(SoulModRuntimeYYToolkitAdapterTests YYToolkitAdapterTests.cpp ${SOULMOD_RUNTIME_TEST_DOUBLE})
target_link_libraries(SoulModRuntimeYYToolkitAdapterTests PRIVATE SoulMod.Runtime.Core psapi bcrypt)
add_test(NAME SoulModRuntime.YYToolkitAdapter COMMAND SoulModRuntimeYYToolkitAdapterTests)

add_executable(SoulModRuntimeGlobalJsonEpochTests GlobalJsonEpochTests.cpp ${SOULMOD_RUNTIME_TEST_DOUBLE})
target_link_libraries(SoulModRuntimeGlobalJsonEpochTests PRIVATE SoulMod.Runtime.Core psapi bcrypt)
add_test(NAME SoulModRuntime.GlobalJsonEpoch COMMAND SoulModRuntimeGlobalJsonEpochTests)

add_executable(SoulModRuntimeParityTests RuntimeParityTests.cpp ${SOULMOD_RUNTIME_TEST_DOUBLE})
target_link_libraries(SoulModRuntimeParityTests PRIVATE SoulMod.Runtime.Core psapi bcrypt)
add_test(NAME SoulModRuntime.Parity COMMAND SoulModRuntimeParityTests)
