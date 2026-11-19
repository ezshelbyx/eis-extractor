add_executable(eis_tests test_engine.cpp)
target_link_libraries(eis_tests PRIVATE eis_core Qt6::Test)
target_compile_options(eis_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME eis_tests COMMAND eis_tests)

add_executable(eis_operation_tests test_operations.cpp)
target_link_libraries(eis_operation_tests PRIVATE eis_core Qt6::Test)
target_compile_options(eis_operation_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME eis_operation_tests COMMAND eis_operation_tests)
