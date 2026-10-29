add_executable(jtext_v001_test v001_test.cpp)
target_link_libraries(jtext_v001_test PRIVATE jText::jText)
add_test(NAME jtext_v001_test COMMAND jtext_v001_test)

# Negative / hardening tests for the line + structure parser.
add_executable(jtext_parser_validator_test parser_validator_test.cpp)
target_link_libraries(jtext_parser_validator_test PRIVATE jText::jText)
add_test(NAME jtext_parser_validator_test COMMAND jtext_parser_validator_test)
# Calling the writer out of order terminates (contracts).
add_executable(jtext_contracts_test contracts_test.cpp)
target_link_libraries(jtext_contracts_test PRIVATE jText::jText)
add_test(NAME jtext_contracts_test COMMAND jtext_contracts_test)
