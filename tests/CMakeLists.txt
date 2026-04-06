include(Catch)

function(yac_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE yac_presentation Catch2::Catch2WithMain)
  catch_discover_tests(${name})
endfunction()

yac_add_test(yac_test_message     test_message.cpp)
yac_add_test(yac_test_parser      test_parser.cpp)
yac_add_test(yac_test_highlighter test_highlighter.cpp)
yac_add_test(yac_test_renderer    test_renderer.cpp)
yac_add_test(yac_test_chat_ui     test_chat_ui.cpp)
