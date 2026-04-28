#include "tool_call/lsp_client.hpp"

#include "tool_call/json_rpc_stdio_base.hpp"
#include "tool_call/lsp_error.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>

namespace yac::tool_call {

namespace {

using Json = JsonRpcStdioBase::Json;

std::string ReadWholeFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Unable to read file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string LanguageIdForPath(const std::filesystem::path& path) {
  const auto extension = path.extension().string();
  if (extension == ".c") {
    return "c";
  }
  if (extension == ".cc" || extension == ".cpp" || extension == ".cxx" ||
      extension == ".hpp" || extension == ".hh" || extension == ".hxx" ||
      extension == ".h") {
    return "cpp";
  }
  return "plaintext";
}

std::string FileUri(const std::filesystem::path& path) {
  return "file://" +
         std::filesystem::absolute(path).lexically_normal().string();
}

std::filesystem::path PathFromFileUri(const std::string& uri) {
  constexpr std::string_view kPrefix = "file://";
  if (uri.starts_with(kPrefix)) {
    return {uri.substr(kPrefix.size())};
  }
  return {uri};
}

DiagnosticSeverity SeverityFromLsp(int severity) {
  switch (severity) {
    case 1:
      return DiagnosticSeverity::Error;
    case 2:
      return DiagnosticSeverity::Warning;
    case 3:
      return DiagnosticSeverity::Information;
    case 4:
      return DiagnosticSeverity::Hint;
    default:
      return DiagnosticSeverity::Information;
  }
}

std::string SymbolKindFromLsp(int kind) {
  switch (kind) {
    case 1:
      return "file";
    case 2:
      return "module";
    case 3:
      return "namespace";
    case 4:
      return "package";
    case 5:
      return "class";
    case 6:
      return "method";
    case 7:
      return "property";
    case 8:
      return "field";
    case 9:
      return "constructor";
    case 10:
      return "enum";
    case 11:
      return "interface";
    case 12:
      return "function";
    case 13:
      return "variable";
    case 14:
      return "constant";
    case 15:
      return "string";
    case 16:
      return "number";
    case 17:
      return "boolean";
    case 18:
      return "array";
    case 23:
      return "struct";
    default:
      return "symbol";
  }
}

int JsonIntAt(const Json& object, const std::string& key, int fallback = 0) {
  if (!object.contains(key) || !object[key].is_number_integer()) {
    return fallback;
  }
  return object[key].get<int>();
}

std::string JsonStringAt(const Json& object, const std::string& key) {
  if (!object.contains(key) || !object[key].is_string()) {
    return {};
  }
  return object[key].get<std::string>();
}

std::string DisplayWorkspacePath(const std::filesystem::path& path,
                                 const std::filesystem::path& workspace_root) {
  auto normalized = std::filesystem::absolute(path).lexically_normal();
  std::error_code error;
  auto relative = std::filesystem::relative(normalized, workspace_root, error);
  if (!error && !relative.empty() && !relative.native().starts_with("..")) {
    return relative.string();
  }
  return normalized.string();
}

int SymbolLine(const Json& item) {
  if (item.contains("selectionRange")) {
    return JsonIntAt(item["selectionRange"]["start"], "line", 0) + 1;
  }
  if (item.contains("location")) {
    return JsonIntAt(item["location"]["range"]["start"], "line", 0) + 1;
  }
  return 1;
}

}  // namespace

class JsonRpcLspClient::Impl : public JsonRpcStdioBase {
 public:
  static constexpr std::size_t kMaxLspMessageBytes = 64UL * 1024UL * 1024UL;

  explicit Impl(LspServerConfig config)
      : JsonRpcStdioBase("LSP"),
        config_(std::move(config)),
        workspace_root_(std::filesystem::absolute(config_.workspace_root)
                            .lexically_normal()) {}

  ~Impl() override { Stop(); }
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  [[nodiscard]] LspDiagnosticsCall Diagnostics(const std::string& file_path) {
    return CallWithLspErrorTo(
        LspDiagnosticsCall{.file_path = file_path}, [&] {
          const auto path = AbsolutePath(file_path);
          EnsureInitialized();
          SyncDocument(path);
          const auto uri = FileUri(path);
          std::unique_lock lock(diagnostics_mutex_);
          diagnostics_wake_.wait_for(
              lock, config_.diagnostics_timeout,
              [&] { return diagnostics_.contains(uri); });
          auto diagnostics = diagnostics_.contains(uri)
                                 ? diagnostics_[uri]
                                 : std::vector<LspDiagnostic>{};
          return LspDiagnosticsCall{.file_path = DisplayPath(path),
                                     .diagnostics = std::move(diagnostics)};
        });
  }

  [[nodiscard]] LspReferencesCall References(const std::string& file_path,
                                             int line, int character,
                                             const std::string& symbol) {
    return CallWithLspErrorTo(
        LspReferencesCall{.symbol = symbol, .file_path = file_path}, [&] {
          const auto path = AbsolutePath(file_path);
          EnsureInitialized();
          SyncDocument(path);
          auto response = SendRequest(
              "textDocument/references",
              {{"textDocument", {{"uri", FileUri(path)}}},
               {"position", Position(line, character)},
               {"context", {{"includeDeclaration", true}}}},
              config_.request_timeout);
          auto locations = ParseLocations(response["result"]);
          return LspReferencesCall{.symbol = symbol,
                                    .file_path = DisplayPath(path),
                                    .references = std::move(locations)};
        });
  }

  [[nodiscard]] LspGotoDefinitionCall GotoDefinition(
      const std::string& file_path, int line, int character,
      const std::string& symbol) {
    return CallWithLspErrorTo(
        LspGotoDefinitionCall{.symbol = symbol,
                               .file_path = file_path,
                               .line = line,
                               .character = character},
        [&] {
          const auto path = AbsolutePath(file_path);
          EnsureInitialized();
          SyncDocument(path);
          auto response = SendRequest(
              "textDocument/definition",
              {{"textDocument", {{"uri", FileUri(path)}}},
               {"position", Position(line, character)}},
              config_.request_timeout);
          auto locations = ParseLocations(response["result"]);
          return LspGotoDefinitionCall{.symbol = symbol,
                                        .file_path = DisplayPath(path),
                                        .line = line,
                                        .character = character,
                                        .definitions = std::move(locations)};
        });
  }

  [[nodiscard]] LspRenameCall Rename(const std::string& file_path, int line,
                                     int character, const std::string& old_name,
                                     const std::string& new_name) {
    return CallWithLspErrorTo(
        LspRenameCall{.file_path = file_path,
                       .line = line,
                       .character = character,
                       .old_name = old_name,
                       .new_name = new_name},
        [&] {
          const auto path = AbsolutePath(file_path);
          EnsureInitialized();
          SyncDocument(path);
          auto response = SendRequest(
              "textDocument/rename",
              {{"textDocument", {{"uri", FileUri(path)}}},
               {"position", Position(line, character)},
               {"newName", new_name}},
              config_.request_timeout);
          auto edits = ParseWorkspaceEdits(response["result"]);
          return LspRenameCall{
              .file_path = DisplayPath(path),
              .line = line,
              .character = character,
              .old_name = old_name,
              .new_name = new_name,
              .changes_count = static_cast<int>(edits.size()),
              .changes = std::move(edits)};
        });
  }

  [[nodiscard]] LspSymbolsCall Symbols(const std::string& file_path) {
    return CallWithLspErrorTo(
        LspSymbolsCall{.file_path = file_path}, [&] {
          const auto path = AbsolutePath(file_path);
          EnsureInitialized();
          SyncDocument(path);
          auto response = SendRequest(
              "textDocument/documentSymbol",
              {{"textDocument", {{"uri", FileUri(path)}}}},
              config_.request_timeout);
          std::vector<LspSymbol> symbols;
          ParseSymbols(response["result"], symbols);
          return LspSymbolsCall{.file_path = DisplayPath(path),
                                 .symbols = std::move(symbols)};
        });
  }

 private:
  void EnsureInitialized() {
    std::lock_guard lock(start_mutex_);
    if (initialized_) {
      return;
    }
    Start(config_.command, config_.args);
    [[maybe_unused]] const auto initialize_response = SendRequest(
        "initialize",
        {{"processId", static_cast<int>(getpid())},
         {"rootUri", FileUri(workspace_root_)},
         {"capabilities", Json::object()},
         {"workspaceFolders",
          Json::array({{{"uri", FileUri(workspace_root_)},
                        {"name", workspace_root_.filename().string()}}})}},
        config_.request_timeout);
    SendNotification("initialized", Json::object());
    initialized_ = true;
  }

  void SyncDocument(const std::filesystem::path& path) {
    const auto uri = FileUri(path);
    const auto text = ReadWholeFile(path);
    auto& version = document_versions_[uri];
    if (version == 0) {
      version = 1;
      opened_documents_.insert(uri);
      SendNotification("textDocument/didOpen",
                       {{"textDocument",
                         {{"uri", uri},
                          {"languageId", LanguageIdForPath(path)},
                          {"version", version},
                          {"text", text}}}});
      return;
    }
    ++version;
    SendNotification("textDocument/didChange",
                     {{"textDocument", {{"uri", uri}, {"version", version}}},
                      {"contentChanges", Json::array({{{"text", text}}})}});
  }

  void WriteFrame(const std::string& body) override {
    const auto header =
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    WriteBytes(header);
    WriteBytes(body);
  }

  std::optional<std::string> ReadFrame() override {
    size_t content_length = 0;
    while (true) {
      auto line = ReadLine();
      if (!line.has_value()) {
        return std::nullopt;
      }
      if (line->empty()) {
        break;
      }
      constexpr std::string_view kHeader = "Content-Length:";
      if (line->rfind(kHeader, 0) == 0) {
        auto parsed =
            ParseContentLength(std::string_view(*line).substr(kHeader.size()));
        if (!parsed.has_value()) {
          return std::nullopt;
        }
        content_length = *parsed;
      }
    }
    if (content_length == 0) {
      return std::string{};
    }
    std::string body(content_length, '\0');
    size_t read_count = 0;
    while (read_count < content_length) {
      const auto result =
          read(ReadFd(), body.data() + read_count, content_length - read_count);
      if (result <= 0) {
        return std::nullopt;
      }
      read_count += static_cast<size_t>(result);
    }
    return body;
  }

  void OnNotification(std::string_view method, const Json& params) override {
    if (method == "textDocument/publishDiagnostics") {
      ProcessDiagnostics(params);
    }
  }

  [[nodiscard]] std::optional<std::string> ReadLine() const {
    std::string line;
    char ch = '\0';
    while (true) {
      const auto result = read(ReadFd(), &ch, 1);
      if (result <= 0) {
        return std::nullopt;
      }
      if (ch == '\n') {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        return line;
      }
      line.push_back(ch);
    }
  }

  // Parses a "Content-Length: <n>" header value. Returns nullopt if the value
  // is malformed or exceeds kMaxLspMessageBytes; caller treats either as a
  // framing error and lets the reader fault pending requests.
  [[nodiscard]] static std::optional<size_t> ParseContentLength(
      std::string_view value) {
    try {
      size_t consumed = 0;
      const std::string trimmed(value);
      const auto parsed = std::stoul(trimmed, &consumed);
      if (parsed > kMaxLspMessageBytes) {
        return std::nullopt;
      }
      return static_cast<size_t>(parsed);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  void ProcessDiagnostics(const Json& params) {
    if (!params.contains("uri") || !params["uri"].is_string()) {
      return;
    }
    std::vector<LspDiagnostic> diagnostics;
    if (params.contains("diagnostics") && params["diagnostics"].is_array()) {
      for (const auto& diagnostic : params["diagnostics"]) {
        const auto severity =
            SeverityFromLsp(JsonIntAt(diagnostic, "severity", 3));
        std::string message = JsonStringAt(diagnostic, "message");
        int line = 1;
        if (diagnostic.contains("range") &&
            diagnostic["range"].contains("start")) {
          line = JsonIntAt(diagnostic["range"]["start"], "line", 0) + 1;
        }
        diagnostics.push_back(LspDiagnostic{
            .severity = severity, .message = std::move(message), .line = line});
      }
    }
    {
      std::lock_guard lock(diagnostics_mutex_);
      diagnostics_[params["uri"].get<std::string>()] = std::move(diagnostics);
    }
    diagnostics_wake_.notify_all();
  }

  static Json Position(int line, int character) {
    return {{"line", std::max(1, line) - 1},
            {"character", std::max(1, character) - 1}};
  }

  std::vector<LspLocation> ParseLocations(const Json& result) {
    std::vector<LspLocation> locations;
    if (result.is_null()) {
      return locations;
    }
    if (result.is_array()) {
      for (const auto& item : result) {
        ParseLocation(item, workspace_root_, locations);
      }
      return locations;
    }
    ParseLocation(result, workspace_root_, locations);
    return locations;
  }

  static void ParseLocation(const Json& item,
                            const std::filesystem::path& workspace_root,
                            std::vector<LspLocation>& locations) {
    if (!item.contains("uri") || !item.contains("range")) {
      return;
    }
    const auto path = DisplayWorkspacePath(
        PathFromFileUri(item["uri"].get<std::string>()), workspace_root);
    const auto& start = item["range"]["start"];
    locations.push_back(
        LspLocation{.filepath = path,
                    .line = JsonIntAt(start, "line", 0) + 1,
                    .character = JsonIntAt(start, "character", 0) + 1});
  }

  std::vector<LspTextEdit> ParseWorkspaceEdits(const Json& result) {
    std::vector<LspTextEdit> edits;
    if (result.is_null()) {
      return edits;
    }
    if (result.contains("changes")) {
      for (auto it = result["changes"].begin(); it != result["changes"].end();
           ++it) {
        ParseTextEditsForUri(it.key(), it.value(), edits);
      }
    }
    if (result.contains("documentChanges") &&
        result["documentChanges"].is_array()) {
      for (const auto& change : result["documentChanges"]) {
        if (change.contains("textDocument") &&
            change["textDocument"].contains("uri") &&
            change.contains("edits")) {
          ParseTextEditsForUri(change["textDocument"]["uri"].get<std::string>(),
                               change["edits"], edits);
        }
      }
    }
    return edits;
  }

  void ParseTextEditsForUri(const std::string& uri, const Json& text_edits,
                            std::vector<LspTextEdit>& edits) {
    if (!text_edits.is_array()) {
      return;
    }
    const auto path = DisplayPath(PathFromFileUri(uri));
    for (const auto& edit : text_edits) {
      if (!edit.contains("range")) {
        continue;
      }
      const auto& range = edit["range"];
      const auto& start = range["start"];
      const auto& end = range["end"];
      edits.push_back(
          LspTextEdit{.filepath = path,
                      .start_line = JsonIntAt(start, "line", 0) + 1,
                      .start_character = JsonIntAt(start, "character", 0) + 1,
                      .end_line = JsonIntAt(end, "line", 0) + 1,
                      .end_character = JsonIntAt(end, "character", 0) + 1,
                      .new_text = JsonStringAt(edit, "newText")});
    }
  }

  static void ParseSymbols(const Json& result,
                           std::vector<LspSymbol>& symbols) {
    if (!result.is_array()) {
      return;
    }
    for (const auto& item : result) {
      ParseSymbol(item, symbols);
    }
  }

  static void ParseSymbol(const Json& item, std::vector<LspSymbol>& symbols) {
    if (!item.contains("name")) {
      return;
    }
    symbols.push_back(
        LspSymbol{.name = JsonStringAt(item, "name"),
                  .kind = SymbolKindFromLsp(JsonIntAt(item, "kind", 0)),
                  .line = SymbolLine(item)});
    if (item.contains("children") && item["children"].is_array()) {
      for (const auto& child : item["children"]) {
        ParseSymbol(child, symbols);
      }
    }
  }

  [[nodiscard]] std::filesystem::path AbsolutePath(
      const std::string& path) const {
    std::filesystem::path candidate(path);
    if (candidate.is_relative()) {
      candidate = workspace_root_ / candidate;
    }
    candidate = std::filesystem::absolute(candidate).lexically_normal();
    const auto root = workspace_root_.string();
    const auto value = candidate.string();
    const auto prefix =
        root.back() == std::filesystem::path::preferred_separator
            ? root
            : root + std::filesystem::path::preferred_separator;
    if (value != root && !value.starts_with(prefix)) {
      throw std::runtime_error("LSP path is outside the workspace: " + path);
    }
    return candidate;
  }

  [[nodiscard]] std::string DisplayPath(
      const std::filesystem::path& path) const {
    return DisplayWorkspacePath(path, workspace_root_);
  }

  LspServerConfig config_;
  std::filesystem::path workspace_root_;
  std::mutex start_mutex_;
  std::mutex diagnostics_mutex_;
  std::condition_variable_any diagnostics_wake_;
  std::map<std::string, std::vector<LspDiagnostic>> diagnostics_;
  std::set<std::string> opened_documents_;
  std::map<std::string, int> document_versions_;
  bool initialized_ = false;
};

JsonRpcLspClient::JsonRpcLspClient(LspServerConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

JsonRpcLspClient::~JsonRpcLspClient() = default;

LspDiagnosticsCall JsonRpcLspClient::Diagnostics(const std::string& file_path) {
  return impl_->Diagnostics(file_path);
}

LspReferencesCall JsonRpcLspClient::References(const std::string& file_path,
                                               int line, int character,
                                               const std::string& symbol) {
  return impl_->References(file_path, line, character, symbol);
}

LspGotoDefinitionCall JsonRpcLspClient::GotoDefinition(
    const std::string& file_path, int line, int character,
    const std::string& symbol) {
  return impl_->GotoDefinition(file_path, line, character, symbol);
}

LspRenameCall JsonRpcLspClient::Rename(const std::string& file_path, int line,
                                       int character,
                                       const std::string& old_name,
                                       const std::string& new_name) {
  return impl_->Rename(file_path, line, character, old_name, new_name);
}

LspSymbolsCall JsonRpcLspClient::Symbols(const std::string& file_path) {
  return impl_->Symbols(file_path);
}

}  // namespace yac::tool_call
