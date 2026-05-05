include(Catch)

function(yac_add_test name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE
    yac_app
    yac_presentation
    yac_service
    Catch2::Catch2WithMain
  )
  catch_discover_tests(${name} TEST_PREFIX "${name}::")
endfunction()

yac_add_test(yac_test_message          test_message.cpp)
yac_add_test(yac_test_string_util      test_string_util.cpp)
yac_add_test(yac_test_count_summary    test_count_summary.cpp)
yac_add_test(yac_test_mcp_json_helpers test_mcp_json_helpers.cpp)
yac_add_test(yac_test_tool_call_types  test_tool_call_types.cpp)
yac_add_test(yac_test_tool_executor    test_tool_executor.cpp)
yac_add_test(yac_test_chat_session     test_chat_session.cpp)
yac_add_test(yac_test_composer_state   test_composer_state.cpp)
yac_add_test(yac_test_parser           test_parser.cpp)
yac_add_test(yac_test_highlighter      test_highlighter.cpp)
yac_add_test(yac_test_highlighter_multiline test_highlighter_multiline.cpp)
yac_add_test(yac_test_highlighter_diff test_highlighter_diff.cpp)
yac_add_test(yac_test_renderer         test_renderer.cpp)
yac_add_test(yac_test_tool_call_renderer test_tool_call_renderer.cpp)
yac_add_test(yac_test_renderer_mcp       test_renderer_mcp.cpp)
yac_add_test(yac_test_approval_mcp       test_approval_mcp.cpp)
yac_add_test(yac_test_sub_agent_renderer test_sub_agent_renderer.cpp)
yac_add_test(yac_test_chat_ui          test_chat_ui.cpp)
yac_add_test(yac_test_chat_ui_render_plan test_chat_ui_render_plan.cpp)
yac_add_test(yac_test_chat_ui_dialog   test_chat_ui_dialog.cpp)
yac_add_test(yac_test_chat_types_mcp   test_chat_types_mcp.cpp)
yac_add_test(yac_test_collapsible      test_collapsible.cpp)
yac_add_test(yac_test_command_palette  test_command_palette.cpp)
yac_add_test(yac_test_dialog           test_dialog.cpp)
yac_add_test(yac_test_theme_tool_colors test_theme_tool_colors.cpp)
yac_add_test(yac_test_theme            test_theme.cpp)
yac_add_test(yac_test_theme_registry   test_theme_registry.cpp)
yac_add_test(yac_test_message_renderer test_message_renderer.cpp)
yac_add_test(yac_test_tool_call_integration test_tool_call_integration.cpp)
yac_add_test(yac_test_time_util        test_time_util.cpp)
yac_add_test(yac_test_scroll_math      test_scroll_math.cpp)
yac_add_test(yac_test_chat_service     test_chat_service.cpp)
yac_add_test(yac_test_agents_md_loading test_agents_md_loading.cpp)
yac_add_test(yac_test_chat_event_bridge test_chat_event_bridge.cpp)
yac_add_test(yac_test_chat_event_bridge_mcp test_chat_event_bridge_mcp.cpp)
yac_add_test(yac_test_model_discovery  test_model_discovery.cpp)
yac_add_test(yac_test_openai_compatible_chat_provider test_openai_compatible_chat_provider.cpp)
yac_add_test(yac_test_openai_compatible_chat_protocol test_openai_compatible_chat_protocol.cpp)
yac_add_test(yac_test_bedrock_chat_protocol test_bedrock_chat_protocol.cpp)
target_include_directories(yac_test_bedrock_chat_protocol SYSTEM PRIVATE
  ${aws-sdk-cpp_SOURCE_DIR}/src/aws-cpp-sdk-core/include
  ${aws-sdk-cpp_SOURCE_DIR}/generated/src/aws-cpp-sdk-bedrock-runtime/include
)
target_link_libraries(yac_test_bedrock_chat_protocol PRIVATE
  aws-cpp-sdk-bedrock-runtime aws-cpp-sdk-core
)
yac_add_test(yac_test_bedrock_tool_translator         test_bedrock_tool_translator.cpp)
target_link_libraries(yac_test_bedrock_tool_translator PRIVATE
  aws-cpp-sdk-bedrock-runtime
  aws-cpp-sdk-core
)
target_include_directories(yac_test_bedrock_tool_translator PRIVATE
  ${aws-sdk-cpp_SOURCE_DIR}/src/aws-cpp-sdk-core/include
  ${aws-sdk-cpp_SOURCE_DIR}/generated/src/aws-cpp-sdk-bedrock-runtime/include
)
yac_add_test(yac_test_model_context_windows test_model_context_windows.cpp)
yac_add_test(yac_test_provider_context_window test_provider_context_window.cpp)
yac_add_test(yac_test_auto_compact      test_auto_compact.cpp)
yac_add_test(yac_test_slash_command_registry test_slash_command_registry.cpp)
yac_add_test(yac_test_slash_command_task    test_slash_command_task.cpp)
yac_add_test(yac_test_prompt_slash_commands test_prompt_slash_commands.cpp)
yac_add_test(yac_test_slash_command_menu test_slash_command_menu.cpp)
yac_add_test(yac_test_slash_mcp test_slash_mcp.cpp)
yac_add_test(yac_test_settings_toml    test_settings_toml.cpp)
yac_add_test(yac_test_config_paths     test_config_paths.cpp)
yac_add_test(yac_test_prompt_library   test_prompt_library.cpp)
yac_add_test(yac_test_ask_user         test_ask_user.cpp)
yac_add_test(yac_test_sub_agent_manager test_sub_agent_manager.cpp)
yac_add_test(yac_test_bedrock_history_coalescing
  test_bedrock_history_coalescing.cpp
)
target_link_libraries(yac_test_bedrock_history_coalescing PRIVATE
  aws-cpp-sdk-bedrock-runtime
  aws-cpp-sdk-core
)
yac_add_test(yac_test_sub_agent_mcp     test_sub_agent_mcp.cpp)
target_include_directories(yac_test_sub_agent_mcp PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
yac_add_test(yac_test_sub_agent_event_adapter test_sub_agent_event_adapter.cpp)
yac_add_test(yac_test_sub_agent_events test_sub_agent_events.cpp)
yac_add_test(yac_test_sub_agent_executor test_sub_agent_executor.cpp)
yac_add_test(yac_test_tool_call_descriptor test_tool_call_descriptor.cpp)
yac_add_test(yac_test_tool_call_argument_parser test_tool_call_argument_parser.cpp)
yac_add_test(yac_test_agent_mode     test_agent_mode.cpp)
yac_add_test(yac_test_compact_history test_compact_history.cpp)
yac_add_test(yac_test_bash_tool_executor test_bash_tool_executor.cpp)
yac_add_test(yac_test_edit_tool_executor test_edit_tool_executor.cpp)
yac_add_test(yac_test_edit_replacers test_edit_replacers.cpp)
yac_add_test(yac_test_lsp_client      test_lsp_client.cpp)
yac_add_test(yac_test_json_rpc_stdio_base test_json_rpc_stdio_base.cpp)
yac_add_test(yac_test_workspace_filesystem test_workspace_filesystem.cpp)
yac_add_test(yac_test_todo_state      test_todo_state.cpp)
yac_add_test(yac_test_headless        test_headless.cpp)
yac_add_test(yac_test_headless_cancel test_headless_cancel.cpp)
yac_add_test(yac_test_glob_matcher    test_glob_matcher.cpp)
yac_add_test(yac_test_gitignore_filter test_gitignore_filter.cpp)
yac_add_test(yac_test_glob_tool_executor test_glob_tool_executor.cpp)
yac_add_test(yac_test_grep_tool_executor test_grep_tool_executor.cpp)
yac_add_test(yac_test_subprocess_runner test_subprocess_runner.cpp)
yac_add_test(yac_test_mcp_protocol_constants test_mcp_protocol_constants.cpp)
yac_add_test(yac_test_mcp_tool_naming test_mcp_tool_naming.cpp)
yac_add_test(yac_test_mcp_tool_name_invariant test_mcp_tool_name_invariant.cpp)
yac_add_test(yac_test_bedrock_mcp_interop test_bedrock_mcp_interop.cpp)
target_include_directories(yac_test_bedrock_mcp_interop SYSTEM PRIVATE
  ${aws-sdk-cpp_SOURCE_DIR}/src/aws-cpp-sdk-core/include
  ${aws-sdk-cpp_SOURCE_DIR}/generated/src/aws-cpp-sdk-bedrock-runtime/include
)
target_link_libraries(yac_test_bedrock_mcp_interop PRIVATE
  aws-cpp-sdk-bedrock-runtime
  aws-cpp-sdk-core
)
yac_add_test(yac_test_mcp_secret_redaction test_mcp_secret_redaction.cpp)
yac_add_test(yac_test_mcp_debug_log test_mcp_debug_log.cpp)
yac_add_test(yac_test_mcp_file_token_store test_mcp_file_token_store.cpp)
yac_add_test(yac_test_core_types_mcp_manager test_core_types_mcp_manager.cpp)
yac_add_test(yac_test_chat_service_mcp       test_chat_service_mcp.cpp)
target_include_directories(yac_test_chat_service_mcp PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
yac_add_test(yac_test_chat_service_prompt_processor_mcp
  test_chat_service_prompt_processor_mcp.cpp
)
target_include_directories(yac_test_chat_service_prompt_processor_mcp PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
yac_add_test(yac_test_tool_validation_error test_tool_validation_error.cpp)
yac_add_test(yac_test_prepare_validation test_prepare_validation.cpp)
target_include_directories(yac_test_prepare_validation PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
yac_add_test(yac_test_bootstrap_mcp          test_bootstrap_mcp.cpp)
target_include_directories(yac_test_bootstrap_mcp PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
yac_add_test(yac_test_mcp_keychain_token_store test_mcp_keychain_token_store.cpp)
yac_add_test(yac_test_mcp_server_config test_mcp_server_config.cpp)
yac_add_test(yac_test_mcp_env_override test_mcp_env_override.cpp)
yac_add_test(yac_test_mcp_protocol_messages test_mcp_protocol_messages.cpp)
yac_add_test(yac_test_mcp_sse_parser test_mcp_sse_parser.cpp)
yac_add_test(yac_test_mcp_streamable_http test_mcp_streamable_http.cpp)
yac_add_test(yac_test_mcp_bearer_auth   test_mcp_bearer_auth.cpp)
yac_add_test(yac_test_mcp_oauth_pkce    test_mcp_oauth_pkce.cpp)
yac_add_test(yac_test_mcp_oauth_flow    test_mcp_oauth_flow.cpp)
yac_add_test(yac_test_mcp_loopback_callback test_mcp_loopback_callback.cpp)
yac_add_test(yac_test_mcp_oauth_discovery test_mcp_oauth_discovery.cpp)
yac_add_test(yac_test_mcp_status_panel test_mcp_status_panel.cpp)
yac_add_test(yac_test_mcp_resources_slash test_mcp_resources_slash.cpp)
yac_add_test(yac_test_mcp_admin test_mcp_admin.cpp)
yac_add_test(yac_test_yac_mcp_cli test_yac_mcp_cli.cpp)
target_include_directories(yac_test_mcp_resources_slash PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(yac_test_mcp_manager
  mock_mcp_transport.cpp
  test_mcp_manager.cpp
)
target_include_directories(yac_test_mcp_manager PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(yac_test_mcp_manager PRIVATE
  yac_app
  yac_presentation
  yac_service
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mcp_manager
  TEST_PREFIX "yac_test_mcp_manager::"
)

add_executable(echo_jsonrpc_server echo_jsonrpc_server.cpp)
target_link_libraries(echo_jsonrpc_server PRIVATE openai_cpp)

yac_add_test(yac_test_mcp_stdio_transport test_mcp_stdio_transport.cpp)
target_compile_definitions(yac_test_mcp_stdio_transport PRIVATE
  ECHO_JSONRPC_SERVER_PATH="$<TARGET_FILE:echo_jsonrpc_server>"
)

add_executable(fake_mcp_server fake_mcp_server.cpp)
target_link_libraries(fake_mcp_server PRIVATE openai_cpp)

add_executable(yac_test_fake_mcp_server_smoke test_fake_mcp_server_smoke.cpp)
target_link_libraries(yac_test_fake_mcp_server_smoke PRIVATE
  openai_cpp
  Catch2::Catch2WithMain
)
target_compile_definitions(yac_test_fake_mcp_server_smoke PRIVATE
  FAKE_MCP_SERVER_PATH="$<TARGET_FILE:fake_mcp_server>"
)
catch_discover_tests(yac_test_fake_mcp_server_smoke
  TEST_PREFIX "yac_test_fake_mcp_server_smoke::"
)

add_executable(yac_test_mcp_cancellation
  mock_mcp_transport.cpp
  test_mcp_cancellation.cpp
)
target_include_directories(yac_test_mcp_cancellation PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(yac_test_mcp_cancellation PRIVATE
  yac_app
  yac_presentation
  yac_service
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mcp_cancellation
  TEST_PREFIX "yac_test_mcp_cancellation::"
)

add_executable(yac_test_mcp_server_session
  mock_mcp_transport.cpp
  test_mcp_server_session.cpp
)
target_include_directories(yac_test_mcp_server_session PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(yac_test_mcp_server_session PRIVATE
  yac_app
  yac_presentation
  yac_service
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mcp_server_session
  TEST_PREFIX "yac_test_mcp_server_session::"
)

add_executable(yac_test_mcp_transport_mock
  mock_mcp_transport.cpp
  test_mcp_transport_mock.cpp
)
target_include_directories(yac_test_mcp_transport_mock PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(yac_test_mcp_transport_mock PRIVATE
  yac_app
  yac_presentation
  yac_service
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mcp_transport_mock
  TEST_PREFIX "yac_test_mcp_transport_mock::"
)

add_executable(yac_test_keychain_smoke test_keychain_smoke.cpp)
target_link_libraries(yac_test_keychain_smoke PRIVATE
  Catch2::Catch2WithMain
  yac_keychain
)
catch_discover_tests(yac_test_keychain_smoke TEST_PREFIX "yac_test_keychain_smoke::")

add_executable(mock_oauth_server mock_oauth_server.cpp)
target_link_libraries(mock_oauth_server PRIVATE OpenSSL::Crypto)

add_executable(yac_test_mock_oauth_server_smoke test_mock_oauth_server_smoke.cpp)
target_link_libraries(yac_test_mock_oauth_server_smoke PRIVATE
  Catch2::Catch2WithMain
)
target_compile_definitions(yac_test_mock_oauth_server_smoke PRIVATE
  MOCK_OAUTH_SERVER_PATH="$<TARGET_FILE:mock_oauth_server>"
)
catch_discover_tests(yac_test_mock_oauth_server_smoke
  TEST_PREFIX "yac_test_mock_oauth_server_smoke::"
)

add_executable(yac_test_e2e_runner
  mock_response_provider.cpp
  yac_test_e2e_runner.cpp
)
target_include_directories(yac_test_e2e_runner PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(yac_test_e2e_runner PRIVATE
  yac_app
  yac_service
  yac_presentation
  openai_cpp
)

add_executable(yac_test_mcp_oauth_e2e
  integration/test_mcp_oauth_e2e.cpp
)
target_compile_definitions(yac_test_mcp_oauth_e2e PRIVATE
  MOCK_OAUTH_SERVER_PATH="$<TARGET_FILE:mock_oauth_server>"
  YAC_BINARY_PATH="$<TARGET_FILE:yac>"
  YAC_TEST_E2E_RUNNER_PATH="$<TARGET_FILE:yac_test_e2e_runner>"
  MOCK_OAUTH_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mock_oauth.toml"
  OAUTH_E2E_SCRIPT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/integration/scripts/oauth_e2e.jsonl"
)
target_link_libraries(yac_test_mcp_oauth_e2e PRIVATE
  openai_cpp
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mcp_oauth_e2e
  TEST_PREFIX "yac_test_mcp_oauth_e2e::"
  PROPERTIES RESOURCE_LOCK "oauth_port_19877"
)

add_executable(yac_test_mcp_cold_start_integration
  integration/test_mcp_cold_start.cpp
)
target_compile_definitions(yac_test_mcp_cold_start_integration PRIVATE
  FAKE_MCP_SERVER_PATH="$<TARGET_FILE:fake_mcp_server>"
  YAC_TEST_E2E_RUNNER_PATH="$<TARGET_FILE:yac_test_e2e_runner>"
  COLD_START_SCRIPT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/integration/scripts/cold_start.jsonl"
  TWO_STDIO_SERVERS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/two_stdio_servers.toml"
  ONE_STDIO_SERVER_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/one_stdio_server.toml"
)
target_link_libraries(yac_test_mcp_cold_start_integration PRIVATE
  openai_cpp
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mcp_cold_start_integration
  TEST_PREFIX "yac_test_mcp_cold_start_integration::"
)

add_executable(yac_test_mcp_warm_cancel_integration
  integration/test_mcp_warm_cancel.cpp
)
target_compile_definitions(yac_test_mcp_warm_cancel_integration PRIVATE
  FAKE_MCP_SERVER_PATH="$<TARGET_FILE:fake_mcp_server>"
  YAC_TEST_E2E_RUNNER_PATH="$<TARGET_FILE:yac_test_e2e_runner>"
  WARM_CANCEL_SCRIPT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/integration/scripts/warm_cancel.jsonl"
  SLOW_SERVER_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/slow_server.toml"
)
target_link_libraries(yac_test_mcp_warm_cancel_integration PRIVATE
  openai_cpp
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mcp_warm_cancel_integration
  TEST_PREFIX "yac_test_mcp_warm_cancel_integration::"
)

add_executable(yac_test_mcp_shutdown_integration
  integration/test_mcp_shutdown.cpp
)
target_compile_definitions(yac_test_mcp_shutdown_integration PRIVATE
  FAKE_MCP_SERVER_PATH="$<TARGET_FILE:fake_mcp_server>"
  YAC_TEST_E2E_RUNNER_PATH="$<TARGET_FILE:yac_test_e2e_runner>"
  SHUTDOWN_SCRIPT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/integration/scripts/shutdown.jsonl"
  TWO_STDIO_SERVERS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/two_stdio_servers.toml"
)
target_link_libraries(yac_test_mcp_shutdown_integration PRIVATE
  openai_cpp
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mcp_shutdown_integration
  TEST_PREFIX "yac_test_mcp_shutdown_integration::"
)

add_executable(yac_test_mcp_crash_integration
  integration/test_mcp_crash.cpp
)
target_compile_definitions(yac_test_mcp_crash_integration PRIVATE
  FAKE_MCP_SERVER_PATH="$<TARGET_FILE:fake_mcp_server>"
  YAC_TEST_E2E_RUNNER_PATH="$<TARGET_FILE:yac_test_e2e_runner>"
  CRASH_SCRIPT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/integration/scripts/crash.jsonl"
  CRASHING_SERVER_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crashing_server.toml"
)
target_link_libraries(yac_test_mcp_crash_integration PRIVATE
  openai_cpp
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mcp_crash_integration
  TEST_PREFIX "yac_test_mcp_crash_integration::"
)

add_executable(yac_test_mock_response_provider
  mock_response_provider.cpp
  test_mock_response_provider.cpp
)
target_include_directories(yac_test_mock_response_provider PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(yac_test_mock_response_provider PRIVATE
  yac_app
  yac_service
  yac_presentation
  openai_cpp
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mock_response_provider
  TEST_PREFIX "yac_test_mock_response_provider::"
)

add_executable(yac_test_mock_bedrock_provider
  mock_bedrock_provider.cpp
  test_mock_bedrock_provider.cpp
)
target_include_directories(yac_test_mock_bedrock_provider PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(yac_test_mock_bedrock_provider PRIVATE
  yac_app
  yac_service
  yac_presentation
  openai_cpp
  Catch2::Catch2WithMain
)
catch_discover_tests(yac_test_mock_bedrock_provider
  TEST_PREFIX "yac_test_mock_bedrock_provider::"
)
