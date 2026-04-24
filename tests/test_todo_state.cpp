#include "tool_call/todo_state.hpp"

#include <atomic>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::tool_call::TodoItem;
using yac::tool_call::TodoState;

TEST_CASE("TodoState: Update and Current round-trip") {
  TodoState state;
  std::vector<TodoItem> todos = {
      {.content = "task1", .status = "pending", .priority = "high"},
      {.content = "task2", .status = "in_progress", .priority = "medium"},
  };
  state.Update(todos);
  auto current = state.Current();
  REQUIRE(current.size() == 2);
  CHECK(current[0].content == "task1");
  CHECK(current[0].status == "pending");
  CHECK(current[0].priority == "high");
  CHECK(current[1].content == "task2");
  CHECK(current[1].status == "in_progress");
  CHECK(current[1].priority == "medium");
}

TEST_CASE("TodoState: Clear resets to empty") {
  TodoState state;
  state.Update({{.content = "a", .status = "pending", .priority = "low"}});
  state.Clear();
  REQUIRE(state.Current().empty());
}

TEST_CASE("TodoState: Empty update") {
  TodoState state;
  state.Update({});
  REQUIRE(state.Current().empty());
}

TEST_CASE("TodoState: Update replaces previous list") {
  TodoState state;
  state.Update({{.content = "first", .status = "pending", .priority = "low"}});
  state.Update(
      {{.content = "second", .status = "completed", .priority = "high"}});
  auto current = state.Current();
  REQUIRE(current.size() == 1);
  CHECK(current[0].content == "second");
  CHECK(current[0].status == "completed");
  CHECK(current[0].priority == "high");
}

TEST_CASE("TodoState: Thread safety concurrent Update and Current") {
  TodoState state;
  std::vector<TodoItem> todos = {
      {.content = "concurrent", .status = "pending", .priority = "medium"}};

  std::thread writer([&] {
    for (int i = 0; i < 1000; ++i) {
      state.Update(todos);
    }
  });

  std::thread reader([&] {
    for (int i = 0; i < 1000; ++i) {
      (void)state.Current();
    }
  });

  writer.join();
  reader.join();
  SUCCEED("Thread safety test completed without crashing");
}
