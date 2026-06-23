add_executable(jac313_jtext_v002_test v002_test.cpp)
target_link_libraries(jac313_jtext_v002_test PRIVATE jac313::jText)
add_test(NAME jac313_jtext_v002_test COMMAND jac313_jtext_v002_test)

# Negative / hardening tests for the line + structure parser.
add_executable(jtext_parser_validator_test parser_validator_test.cpp)
target_link_libraries(jtext_parser_validator_test PRIVATE jac313::jText)
add_test(NAME jtext_parser_validator_test COMMAND jtext_parser_validator_test)