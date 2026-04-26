#include "mcp/secret_redaction.hpp"

#include <array>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {

struct GoldenCase {
  std::string_view name;
  std::string_view input;
  std::string_view expected;
};

const std::array<GoldenCase, 6> kGoldenCases = {{
    {"redacts_bearer_in_json", R"({"Authorization":"Bearer abc.def_123"})",
     R"({"Authorization":"[REDACTED]"})"},
    {"redacts_access_token_field", R"({"access_token":"at-123"})",
     R"({"access_token":"[REDACTED]"})"},
    {"redacts_oauth_code_in_url",
     R"(https://example.test/callback?code=abc123&state=ok)",
     R"(https://example.test/callback?code=[REDACTED]&state=ok)"},
    {"redacts_bearer_plain_text", R"(Bearer xyz)", R"(Bearer [REDACTED])"},
    {"keeps_non_secret_json", R"({"name":"alice","status":"ok"})",
     R"({"name":"alice","status":"ok"})"},
    {"does_not_redact_tokenize", R"({"tokenize":"keep me"})",
     R"({"tokenize":"keep me"})"},
}};

}  // namespace

TEST_CASE("golden_cases", "[mcp_secret_redaction]") {
  for (const auto& test_case : kGoldenCases) {
    CAPTURE(test_case.name);
    REQUIRE(yac::mcp::RedactSecrets(test_case.input) == test_case.expected);
  }
}

TEST_CASE("idempotent", "[mcp_secret_redaction]") {
  for (const auto& test_case : kGoldenCases) {
    CAPTURE(test_case.name);
    const std::string once = yac::mcp::RedactSecrets(test_case.input);
    REQUIRE(yac::mcp::RedactSecrets(once) == once);
  }
}

TEST_CASE("safe_on_malformed_input", "[mcp_secret_redaction]") {
  std::string malformed;
  malformed.push_back('\0');
  malformed.push_back('\xff');
  malformed += "Authorization: Bearer bad\n{\"access_token\":\"tok";
  const std::string redacted = yac::mcp::RedactSecrets(malformed);
  REQUIRE(redacted.find("[REDACTED]") != std::string::npos);
}
