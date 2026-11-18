add_executable(eis_tests test_engine.cpp)
target_link_libraries(eis_tests PRIVATE eis_core Qt6::Test)
target_compile_options(eis_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME eis_tests COMMAND eis_tests)
