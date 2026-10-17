function(emerick_target target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Werror)
    # GCC flags fields left out of designated initializers; Clang's -Wmissing-designated-field-initializers
    # already covers the fields that have no default, so we follow Clang's stricter rule there.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)
    endif()
endfunction()

# One Catch2 test binary per module, discovered by CTest.
function(emerick_test name)
    add_executable(${name} ${ARGN})
    emerick_target(${name})
    target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain)
    catch_discover_tests(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
