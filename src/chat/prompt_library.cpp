#include "chat/prompt_library.hpp"

#include "chat/config_paths.hpp"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <toml++/toml.hpp>
#include <utility>

namespace yac::chat {
namespace {

constexpr std::string_view kFallbackDescription = "Run predefined prompt";

// Default prompt templates are vendored from OpenCode at commit
// ad0545335a9ac5c371f4bd51674cd6da2414e2e9:
// - packages/opencode/src/command/template/initialize.txt
// - packages/opencode/src/command/template/review.txt
//
// OpenCode MIT notice:
// Copyright (c) 2025 opencode
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED
// "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
// LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
constexpr std::string_view kDefaultInitPrompt =
    R"($REPO_CONTEXT
Create or update `AGENTS.md` for this repository.

The goal is a compact instruction file that helps future OpenCode sessions avoid mistakes and ramp up quickly. Every line should answer: "Would an agent likely miss this without help?" If not, leave it out.

User-provided focus or constraints (honor these):
$ARGUMENTS

## How to investigate

Read the highest-value sources first:
- `README*`, root manifests, workspace config, lockfiles
- build, test, lint, formatter, typecheck, and codegen config
- CI workflows and pre-commit / task runner config
- existing instruction files (`AGENTS.md`, `CLAUDE.md`, `.cursor/rules/`, `.cursorrules`, `.github/copilot-instructions.md`)
- repo-local OpenCode config such as `opencode.json`

If architecture is still unclear after reading config and docs, inspect a small number of representative code files to find the real entrypoints, package boundaries, and execution flow. Prefer reading the files that explain how the system is wired together over random leaf files.

Prefer executable sources of truth over prose. If docs conflict with config or scripts, trust the executable source and only keep what you can verify.

## What to extract

Look for the highest-signal facts for an agent working in this repo:
- exact developer commands, especially non-obvious ones
- how to run a single test, a single package, or a focused verification step
- required command order when it matters, such as `lint -> typecheck -> test`
- monorepo or multi-package boundaries, ownership of major directories, and the real app/library entrypoints
- framework or toolchain quirks: generated code, migrations, codegen, build artifacts, special env loading, dev servers, infra deploy flow
- repo-specific style or workflow conventions that differ from defaults
- testing quirks: fixtures, integration test prerequisites, snapshot workflows, required services, flaky or expensive suites
- important constraints from existing instruction files worth preserving

Good `AGENTS.md` content is usually hard-earned context that took reading multiple files to infer.

## Questions

Only ask the user questions if the repo cannot answer something important. Use the `question` tool for one short batch at most.

Good questions:
- undocumented team conventions
- branch / PR / release expectations
- missing setup or test prerequisites that are known but not written down

Do not ask about anything the repo already makes clear.

## Writing rules

Include only high-signal, repo-specific guidance such as:
- exact commands and shortcuts the agent would otherwise guess wrong
- architecture notes that are not obvious from filenames
- conventions that differ from language or framework defaults
- setup requirements, environment quirks, and operational gotchas
- references to existing instruction sources that matter

Exclude:
- generic software advice
- long tutorials or exhaustive file trees
- obvious language conventions
- speculative claims or anything you could not verify
- content better stored in another file referenced via `opencode.json` `instructions`

When in doubt, omit.

Prefer short sections and bullets. If the repo is simple, keep the file simple. If the repo is large, summarize the few structural facts that actually change how an agent should work.

If `AGENTS.md` already exists at `$WORKSPACE_ROOT`, improve it in place rather than rewriting blindly. Preserve verified useful guidance, delete fluff or stale claims, and reconcile it with the current codebase.)";

constexpr std::string_view kDefaultReviewPrompt =
    R"(You are a code reviewer. Your job is to review code changes and provide actionable feedback.

---

Input: $ARGUMENTS

---

## Determining What to Review

Based on the input provided, determine which type of review to perform:

1. **No arguments (default)**: Review all uncommitted changes
   - Run: `git diff` for unstaged changes
   - Run: `git diff --cached` for staged changes
   - Run: `git status --short` to identify untracked (net new) files

2. **Commit hash** (40-char SHA or short hash): Review that specific commit
   - Run: `git show $ARGUMENTS`

3. **Branch name**: Compare current branch to the specified branch
   - Run: `git diff $ARGUMENTS...HEAD`

4. **PR URL or number** (contains "github.com" or "pull" or looks like a PR number): Review the pull request
   - Run: `gh pr view $ARGUMENTS` to get PR context
   - Run: `gh pr diff $ARGUMENTS` to get the diff

Use best judgement when processing input.

---

## Gathering Context

**Diffs alone are not enough.** After getting the diff, read the entire file(s) being modified to understand the full context. Code that looks wrong in isolation may be correct given surrounding logic--and vice versa.

- Use the diff to identify which files changed
- Use `git status --short` to identify untracked files, then read their full contents
- Read the full file to understand existing patterns, control flow, and error handling
- Check for existing style guide or conventions files (CONVENTIONS.md, AGENTS.md, .editorconfig, etc.)

---

## What to Look For

**Bugs** - Your primary focus.
- Logic errors, off-by-one mistakes, incorrect conditionals
- If-else guards: missing guards, incorrect branching, unreachable code paths
- Edge cases: null/empty/undefined inputs, error conditions, race conditions
- Security issues: injection, auth bypass, data exposure
- Broken error handling that swallows failures, throws unexpectedly or returns error types that are not caught.

**Structure** - Does the code fit the codebase?
- Does it follow existing patterns and conventions?
- Are there established abstractions it should use but doesn't?
- Excessive nesting that could be flattened with early returns or extraction

**Performance** - Only flag if obviously problematic.
- O(n^2) on unbounded data, N+1 queries, blocking I/O on hot paths

**Behavior Changes** - If a behavioral change is introduced, raise it (especially if it's possibly unintentional).

---

## Before You Flag Something

**Be certain.** If you're going to call something a bug, you need to be confident it actually is one.

- Only review the changes - do not review pre-existing code that wasn't modified
- Don't flag something as a bug if you're unsure - investigate first
- Don't invent hypothetical problems - if an edge case matters, explain the realistic scenario where it breaks
- If you need more context to be sure, use the tools below to get it

**Don't be a zealot about style.** When checking code against conventions:

- Verify the code is *actually* in violation. Don't complain about else statements if early returns are already being used correctly.
- Some "violations" are acceptable when they're the simplest option. A `let` statement is fine if the alternative is convoluted.
- Excessive nesting is a legitimate concern regardless of other style choices.
- Don't flag style preferences as issues unless they clearly violate established project conventions.

---

## Tools

Use these to inform your review:

- **Explore agent** - Find how existing code handles similar problems. Check patterns, conventions, and prior art before claiming something doesn't fit.
- **Exa Code Context** - Verify correct usage of libraries/APIs before flagging something as wrong.
- **Exa Web Search** - Research best practices if you're unsure about a pattern.

If you're uncertain about something and can't verify it with these tools, say "I'm not sure about X" rather than flagging it as a definite issue.

---

## Output

1. If there is a bug, be direct and clear about why it is a bug.
2. Clearly communicate severity of issues. Do not overstate severity.
3. Critiques should clearly and explicitly communicate the scenarios, environments, or inputs that are necessary for the bug to arise. The comment should immediately indicate that the issue's severity depends on these factors.
4. Your tone should be matter-of-fact and not accusatory or overly positive. It should read as a helpful AI assistant suggestion without sounding too much like a human reviewer.
5. Write so the reader can quickly understand the issue without reading too closely.
6. AVOID flattery, do not give any comments that are not helpful to the reader. Avoid phrasing like "Great job ...", "Thanks for ...".)";

struct DefaultPromptSeed {
  std::string_view name;
  std::string_view description;
  std::string_view prompt;
  int revision;
};

constexpr DefaultPromptSeed kDefaultPromptSeeds[] = {
    {.name = "init",
     .description = "Create or update repository agent instructions",
     .prompt = kDefaultInitPrompt,
     .revision = 2},
    {.name = "review",
     .description = "Review code changes and report actionable feedback",
     .prompt = kDefaultReviewPrompt,
     .revision = 2},
};

void AddWarning(std::vector<ConfigIssue>& issues, std::string message,
                std::string detail) {
  issues.push_back({.severity = ConfigIssueSeverity::Warning,
                    .message = std::move(message),
                    .detail = std::move(detail)});
}

bool IsCommandNameChar(unsigned char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' ||
         ch == '-';
}

bool IsValidPromptName(const std::string& name) {
  return !name.empty() &&
         std::all_of(name.begin(), name.end(),
                     [](unsigned char ch) { return IsCommandNameChar(ch); });
}

std::string TrimAsciiWhitespace(std::string value) {
  const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  auto begin = std::find_if_not(value.begin(), value.end(), is_space);
  auto end = std::find_if_not(value.rbegin(), value.rend(), is_space).base();
  if (begin >= end) {
    return {};
  }
  return std::string(begin, end);
}

bool EnsurePromptDirectory(const std::filesystem::path& prompts_dir,
                           std::vector<ConfigIssue>& issues) {
  std::error_code ec;
  std::filesystem::create_directories(prompts_dir, ec);
  if (ec) {
    AddWarning(issues, "Failed to create " + prompts_dir.string(),
               ec.message());
    return false;
  }
#ifndef _WIN32
  std::filesystem::permissions(prompts_dir, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, ec);
  if (ec) {
    AddWarning(issues, "Failed to set permissions on " + prompts_dir.string(),
               ec.message());
    ec.clear();
  }
#endif
  return true;
}

std::string BuildSeedContent(const DefaultPromptSeed& seed) {
  std::string content;
  content.reserve(256 + seed.prompt.size());
  content += "description = \"";
  content += seed.description;
  content += "\"\n";
  content += "revision = ";
  content += std::to_string(seed.revision);
  content += "\n";
  content += "prompt = '''\n";
  content += seed.prompt;
  if (!seed.prompt.empty() && seed.prompt.back() != '\n') {
    content += '\n';
  }
  content += "'''\n";
  return content;
}

bool IsUnmodifiedSeed(const std::filesystem::path& path,
                      const DefaultPromptSeed& seed) {
  std::ifstream input(path);
  if (!input) {
    return false;
  }
  std::string existing((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
  return existing == BuildSeedContent(seed);
}

void WriteSeedPrompt(const std::filesystem::path& prompts_dir,
                     const DefaultPromptSeed& seed,
                     std::vector<ConfigIssue>& issues) {
  const auto path = prompts_dir / (std::string(seed.name) + ".toml");
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    try {
      const auto table = toml::parse_file(path.string());
      const auto stored_revision = table["revision"].value<int>();
      if (stored_revision.has_value() && *stored_revision >= seed.revision) {
        return;
      }
      if (!IsUnmodifiedSeed(path, seed)) {
        return;
      }
    } catch (...) {
      return;
    }
  }
  if (ec) {
    AddWarning(issues, "Failed to check " + path.string(), ec.message());
    return;
  }

  const auto content = BuildSeedContent(seed);
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    AddWarning(issues, "Failed to create " + path.string(),
               "YAC will continue without this default prompt.");
    return;
  }

  output << content;
  output.close();
  if (!output) {
    AddWarning(issues, "Failed to write " + path.string(),
               "YAC will continue without this default prompt.");
    return;
  }

#ifndef _WIN32
  std::filesystem::permissions(
      path,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      std::filesystem::perm_options::replace, ec);
  if (ec) {
    AddWarning(issues, "Failed to set permissions on " + path.string(),
               ec.message());
  }
#endif
}

void SeedDefaultPrompts(const std::filesystem::path& prompts_dir,
                        std::vector<ConfigIssue>& issues) {
  for (const auto& seed : kDefaultPromptSeeds) {
    WriteSeedPrompt(prompts_dir, seed, issues);
  }
}

std::optional<PromptDefinition> LoadPromptFile(
    const std::filesystem::path& path, std::vector<ConfigIssue>& issues) {
  const auto name = path.stem().string();
  if (!IsValidPromptName(name)) {
    AddWarning(issues, "Skipped prompt file " + path.filename().string(),
               "File names must use lowercase letters, digits, hyphens, or "
               "underscores.");
    return std::nullopt;
  }

  toml::table table;
  try {
    table = toml::parse_file(path.string());
  } catch (const toml::parse_error& error) {
    AddWarning(issues,
               "Failed to parse prompt file " + path.filename().string(),
               std::string(error.description()));
    return std::nullopt;
  } catch (const std::exception& error) {
    AddWarning(issues, "Failed to read prompt file " + path.filename().string(),
               error.what());
    return std::nullopt;
  }

  auto* prompt = table["prompt"].as_string();
  if (prompt == nullptr) {
    AddWarning(issues, "Skipped prompt file " + path.filename().string(),
               "Expected a string field named prompt.");
    return std::nullopt;
  }

  std::string description{kFallbackDescription};
  if (auto* parsed_description = table["description"].as_string()) {
    description = parsed_description->get();
  } else if (table.contains("description")) {
    AddWarning(issues,
               "Invalid description in prompt file " + path.filename().string(),
               "Expected a string; using the fallback description.");
  }

  return PromptDefinition{
      .name = name,
      .description = std::move(description),
      .prompt = prompt->get(),
  };
}

}  // namespace

PromptLibraryResult LoadPromptLibrary(const std::filesystem::path& prompts_dir,
                                      bool seed_defaults) {
  PromptLibraryResult result;
  if (!EnsurePromptDirectory(prompts_dir, result.issues)) {
    return result;
  }

  if (seed_defaults) {
    SeedDefaultPrompts(prompts_dir, result.issues);
  }

  std::error_code ec;
  std::filesystem::directory_iterator iterator(prompts_dir, ec);
  if (ec) {
    AddWarning(result.issues, "Failed to read " + prompts_dir.string(),
               ec.message());
    return result;
  }

  for (const auto& entry : iterator) {
    std::error_code entry_ec;
    if (!entry.is_regular_file(entry_ec) ||
        entry.path().extension() != ".toml") {
      continue;
    }
    auto prompt = LoadPromptFile(entry.path(), result.issues);
    if (prompt.has_value()) {
      result.prompts.push_back(std::move(*prompt));
    }
  }

  std::sort(result.prompts.begin(), result.prompts.end(),
            [](const PromptDefinition& lhs, const PromptDefinition& rhs) {
              return lhs.name < rhs.name;
            });
  return result;
}

PromptLibraryResult LoadPromptLibrary(bool seed_defaults) {
  try {
    return LoadPromptLibrary(GetPromptsDir(), seed_defaults);
  } catch (const std::exception& error) {
    PromptLibraryResult result;
    AddWarning(result.issues, "Could not locate ~/.yac/prompts", error.what());
    return result;
  }
}

std::string BuildRepoContext() {
  std::string context = "=== Repo Context ===\n";
  try {
    const auto root = std::filesystem::current_path();
    context += "Workspace: " + root.string() + "\nTop-level entries:\n";
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
      context += "  " + entry.path().filename().string();
      if (entry.is_directory()) {
        context += "/";
      }
      context += "\n";
    }
  } catch (...) {
  }
  context += "\n";
  return context;
}

std::string RenderPrompt(const PromptDefinition& prompt,
                         const std::string& arguments) {
  return RenderPrompt(prompt.prompt, arguments);
}

std::string RenderPrompt(const std::string& prompt,
                         const std::string& arguments) {
  constexpr std::string_view kArgumentsToken = "$ARGUMENTS";
  constexpr std::string_view kRepoContextToken = "$REPO_CONTEXT";
  constexpr std::string_view kWorkspaceRootToken = "$WORKSPACE_ROOT";
  const auto rendered_arguments = TrimAsciiWhitespace(arguments);

  struct TokenReplacement {
    std::string_view token;
    std::string value;
  };
  const TokenReplacement replacements[] = {
      {kArgumentsToken, rendered_arguments},
      {kRepoContextToken, BuildRepoContext()},
      {kWorkspaceRootToken,
       [] {
         try {
           return std::filesystem::current_path().string();
         } catch (...) {
           return std::string{};
         }
       }()},
  };

  std::string rendered;
  rendered.reserve(prompt.size());

  std::size_t start = 0;
  while (start < prompt.size()) {
    auto best_pos = std::string::npos;
    std::string_view best_token;
    for (const auto& replacement : replacements) {
      const auto pos = prompt.find(replacement.token, start);
      if (pos < best_pos) {
        best_pos = pos;
        best_token = replacement.token;
      }
    }
    if (best_pos == std::string::npos) {
      rendered.append(prompt, start, std::string::npos);
      break;
    }
    rendered.append(prompt, start, best_pos - start);
    for (const auto& replacement : replacements) {
      if (replacement.token == best_token) {
        rendered.append(replacement.value);
        break;
      }
    }
    start = best_pos + best_token.size();
  }
  return rendered;
}

}  // namespace yac::chat
