#include "chat/env_file.hpp"

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;

namespace {

std::filesystem::path CreateTempEnvFile(const std::string& content) {
  std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
  std::filesystem::path temp_file = temp_dir / "test_env_XXXXXX";

  std::ofstream file(temp_file);
  file << content;
  file.close();

  return temp_file;
}

}  // namespace

TEST_CASE("EnvFile::Parse - basic key-value pairs") {
  const auto temp_file = CreateTempEnvFile(
      "KEY1=value1\n"
      "KEY2=value2\n"
      "KEY3=value3\n");

  const auto env_vars = EnvFile::Parse(temp_file);

  REQUIRE(env_vars.size() == 3);
  REQUIRE(env_vars.at("KEY1") == "value1");
  REQUIRE(env_vars.at("KEY2") == "value2");
  REQUIRE(env_vars.at("KEY3") == "value3");

  std::filesystem::remove(temp_file);
}

TEST_CASE("EnvFile::Parse - empty lines are ignored") {
  const auto temp_file = CreateTempEnvFile(
      "\n"
      "KEY1=value1\n"
      "\n"
      "KEY2=value2\n"
      "\n");

  const auto env_vars = EnvFile::Parse(temp_file);

  REQUIRE(env_vars.size() == 2);
  REQUIRE(env_vars.at("KEY1") == "value1");
  REQUIRE(env_vars.at("KEY2") == "value2");

  std::filesystem::remove(temp_file);
}

TEST_CASE("EnvFile::Parse - comments are ignored") {
  const auto temp_file = CreateTempEnvFile(
      "# This is a comment\n"
      "KEY1=value1\n"
      "# Another comment\n"
      "KEY2=value2\n"
      "# Final comment\n");

  const auto env_vars = EnvFile::Parse(temp_file);

  REQUIRE(env_vars.size() == 2);
  REQUIRE(env_vars.at("KEY1") == "value1");
  REQUIRE(env_vars.at("KEY2") == "value2");

  std::filesystem::remove(temp_file);
}

TEST_CASE("EnvFile::Parse - double quoted values") {
  const auto temp_file = CreateTempEnvFile(
      "KEY1=\"value with spaces\"\n"
      "KEY2=\"another quoted value\"\n");

  const auto env_vars = EnvFile::Parse(temp_file);

  REQUIRE(env_vars.size() == 2);
  REQUIRE(env_vars.at("KEY1") == "value with spaces");
  REQUIRE(env_vars.at("KEY2") == "another quoted value");

  std::filesystem::remove(temp_file);
}

TEST_CASE("EnvFile::Parse - single quoted values") {
  const auto temp_file = CreateTempEnvFile(
      "KEY1='value with spaces'\n"
      "KEY2='another quoted value'\n");

  const auto env_vars = EnvFile::Parse(temp_file);

  REQUIRE(env_vars.size() == 2);
  REQUIRE(env_vars.at("KEY1") == "value with spaces");
  REQUIRE(env_vars.at("KEY2") == "another quoted value");

  std::filesystem::remove(temp_file);
}

TEST_CASE("EnvFile::Parse - whitespace around keys and values") {
  const auto temp_file = CreateTempEnvFile(
      "  KEY1  =  value1  \n"
      "KEY2=value2\n"
      "  KEY3  =value3\n"
      "KEY4=  value4  \n");

  const auto env_vars = EnvFile::Parse(temp_file);

  REQUIRE(env_vars.size() == 4);
  REQUIRE(env_vars.at("KEY1") == "value1");
  REQUIRE(env_vars.at("KEY2") == "value2");
  REQUIRE(env_vars.at("KEY3") == "value3");
  REQUIRE(env_vars.at("KEY4") == "value4");

  std::filesystem::remove(temp_file);
}

TEST_CASE("EnvFile::Parse - value with equals sign") {
  const auto temp_file = CreateTempEnvFile(
      "KEY1=value=with=equals\n"
      "KEY2=value\n");

  const auto env_vars = EnvFile::Parse(temp_file);

  REQUIRE(env_vars.size() == 2);
  REQUIRE(env_vars.at("KEY1") == "value=with=equals");
  REQUIRE(env_vars.at("KEY2") == "value");

  std::filesystem::remove(temp_file);
}

TEST_CASE("EnvFile::Parse - empty values are allowed") {
  const auto temp_file = CreateTempEnvFile(
      "KEY1=\n"
      "KEY2=value2\n"
      "KEY3=  \n");

  const auto env_vars = EnvFile::Parse(temp_file);

  REQUIRE(env_vars.size() == 3);
  REQUIRE(env_vars.at("KEY1").empty());
  REQUIRE(env_vars.at("KEY2") == "value2");
  REQUIRE(env_vars.at("KEY3").empty());

  std::filesystem::remove(temp_file);
}

TEST_CASE("EnvFile::Parse - malformed lines are ignored") {
  const auto temp_file = CreateTempEnvFile(
      "KEY1=value1\n"
      "no_equals_sign_here\n"
      "KEY2=value2\n"
      "=no_key\n");

  const auto env_vars = EnvFile::Parse(temp_file);

  REQUIRE(env_vars.size() == 2);
  REQUIRE(env_vars.at("KEY1") == "value1");
  REQUIRE(env_vars.at("KEY2") == "value2");

  std::filesystem::remove(temp_file);
}

TEST_CASE("EnvFile::Parse - non-existent file returns empty map") {
  const auto env_vars = EnvFile::Parse("/non/existent/path/.env");

  REQUIRE(env_vars.empty());
}

TEST_CASE("EnvFile::Parse - complex real-world example") {
  const auto temp_file = CreateTempEnvFile(
      "# YAC Configuration\n"
      "\n"
      "# Provider settings\n"
      "YAC_PROVIDER=openai\n"
      "YAC_MODEL=gpt-4\n"
      "YAC_BASE_URL=https://api.openai.com/v1/\n"
      "\n"
      "# Other settings\n"
      "YAC_TEMPERATURE=0.7\n"
      "YAC_SYSTEM_PROMPT=\"You are a helpful assistant.\"\n"
      "\n"
      "# API key (should be overridden by environment variable)\n"
      "OPENAI_API_KEY=sk-test-key\n");

  const auto env_vars = EnvFile::Parse(temp_file);

  REQUIRE(env_vars.size() == 6);
  REQUIRE(env_vars.at("YAC_PROVIDER") == "openai");
  REQUIRE(env_vars.at("YAC_MODEL") == "gpt-4");
  REQUIRE(env_vars.at("YAC_BASE_URL") == "https://api.openai.com/v1/");
  REQUIRE(env_vars.at("YAC_TEMPERATURE") == "0.7");
  REQUIRE(env_vars.at("YAC_SYSTEM_PROMPT") == "You are a helpful assistant.");
  REQUIRE(env_vars.at("OPENAI_API_KEY") == "sk-test-key");

  std::filesystem::remove(temp_file);
}

TEST_CASE("EnvFile::FindAndParse - finds .env in current directory") {
  const auto original_dir = std::filesystem::current_path();

  const auto temp_dir = std::filesystem::temp_directory_path() / "test_env_dir";
  std::filesystem::create_directories(temp_dir);
  std::filesystem::current_path(temp_dir);

  const std::string env_content =
      "KEY1=value1\n"
      "KEY2=value2\n";

  std::ofstream env_file(temp_dir / ".env");
  env_file << env_content;
  env_file.close();

  const auto env_vars = EnvFile::FindAndParse();

  REQUIRE(env_vars.size() == 2);
  REQUIRE(env_vars.at("KEY1") == "value1");
  REQUIRE(env_vars.at("KEY2") == "value2");

  std::filesystem::current_path(original_dir);
  std::filesystem::remove_all(temp_dir);
}

TEST_CASE("EnvFile::FindAndParse - returns empty map when no .env found") {
  const auto original_dir = std::filesystem::current_path();

  const auto temp_dir =
      std::filesystem::temp_directory_path() / "test_env_dir_no_file";
  std::filesystem::create_directories(temp_dir);
  std::filesystem::current_path(temp_dir);

  const auto env_vars = EnvFile::FindAndParse();

  REQUIRE(env_vars.empty());

  std::filesystem::current_path(original_dir);
  std::filesystem::remove_all(temp_dir);
}
