#include "tool_call/lsp_client.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <openai.hpp>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace yac::tool_call {

namespace {

using Json = openai::_detail::Json;

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

}  // namespace

class JsonRpcLspClient::Impl {
 public:
  explicit Impl(LspServerConfig config)
      : config_(std::move(config)),
        workspace_root_(std::filesystem::absolute(config_.workspace_root)
                            .lexically_normal()) {}

  ~Impl() { Stop(); }
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&) = delete;
  Impl& operator=(Impl&&) = delete;

  [[nodiscard]] LspDiagnosticsCall Diagnostics(const std::string& file_path) {
    try {
      const auto path = AbsolutePath(file_path);
      EnsureInitialized();
      SyncDocument(path);
      const auto uri = FileUri(path);
      std::unique_lock lock(mutex_);
      diagnostics_wake_.wait_for(lock, config_.diagnostics_timeout,
                                 [&] { return diagnostics_.contains(uri); });
      auto diagnostics = diagnostics_.contains(uri)
                             ? diagnostics_[uri]
                             : std::vector<LspDiagnostic>{};
      return LspDiagnosticsCall{.file_path = DisplayPath(path),
                                .diagnostics = std::move(diagnostics)};
    } catch (const std::exception& error) {
      return LspDiagnosticsCall{
          .file_path = file_path, .is_error = true, .error = error.what()};
    }
  }

  [[nodiscard]] LspReferencesCall References(const std::string& file_path,
                                             int line, int character,
                                             const std::string& symbol) {
    try {
      const auto path = AbsolutePath(file_path);
      EnsureInitialized();
      SyncDocument(path);
      auto response =
          SendRequest("textDocument/references",
                      {{"textDocument", {{"uri", FileUri(path)}}},
                       {"position", Position(line, character)},
                       {"context", {{"includeDeclaration", true}}}});
      auto locations = ParseLocations(response["result"]);
      return LspReferencesCall{.symbol = symbol,
                               .file_path = DisplayPath(path),
                               .references = std::move(locations)};
    } catch (const std::exception& error) {
      return LspReferencesCall{.symbol = symbol,
                               .file_path = file_path,
                               .is_error = true,
                               .error = error.what()};
    }
  }

  [[nodiscard]] LspGotoDefinitionCall GotoDefinition(
      const std::string& file_path, int line, int character,
      const std::string& symbol) {
    try {
      const auto path = AbsolutePath(file_path);
      EnsureInitialized();
      SyncDocument(path);
      auto response = SendRequest("textDocument/definition",
                                  {{"textDocument", {{"uri", FileUri(path)}}},
                                   {"position", Position(line, character)}});
      auto locations = ParseLocations(response["result"]);
      return LspGotoDefinitionCall{.symbol = symbol,
                                   .file_path = DisplayPath(path),
                                   .line = line,
                                   .character = character,
                                   .definitions = std::move(locations)};
    } catch (const std::exception& error) {
      return LspGotoDefinitionCall{.symbol = symbol,
                                   .file_path = file_path,
                                   .line = line,
                                   .character = character,
                                   .is_error = true,
                                   .error = error.what()};
    }
  }

  [[nodiscard]] LspRenameCall Rename(const std::string& file_path, int line,
                                     int character, const std::string& old_name,
                                     const std::string& new_name) {
    try {
      const auto path = AbsolutePath(file_path);
      EnsureInitialized();
      SyncDocument(path);
      auto response = SendRequest("textDocument/rename",
                                  {{"textDocument", {{"uri", FileUri(path)}}},
                                   {"position", Position(line, character)},
                                   {"newName", new_name}});
      auto edits = ParseWorkspaceEdits(response["result"]);
      return LspRenameCall{.file_path = DisplayPath(path),
                           .line = line,
                           .character = character,
                           .old_name = old_name,
                           .new_name = new_name,
                           .changes_count = static_cast<int>(edits.size()),
                           .changes = std::move(edits)};
    } catch (const std::exception& error) {
      return LspRenameCall{.file_path = file_path,
                           .line = line,
                           .character = character,
                           .old_name = old_name,
                           .new_name = new_name,
                           .is_error = true,
                           .error = error.what()};
    }
  }

  [[nodiscard]] LspSymbolsCall Symbols(const std::string& file_path) {
    try {
      const auto path = AbsolutePath(file_path);
      EnsureInitialized();
      SyncDocument(path);
      auto response = SendRequest("textDocument/documentSymbol",
                                  {{"textDocument", {{"uri", FileUri(path)}}}});
      std::vector<LspSymbol> symbols;
      ParseSymbols(response["result"], symbols);
      return LspSymbolsCall{.file_path = DisplayPath(path),
                            .symbols = std::move(symbols)};
    } catch (const std::exception& error) {
      return LspSymbolsCall{
          .file_path = file_path, .is_error = true, .error = error.what()};
    }
  }

 private:
  void EnsureInitialized() {
    std::lock_guard lock(start_mutex_);
    if (initialized_) {
      return;
    }
    Start();
    SendRequest(
        "initialize",
        {{"processId", static_cast<int>(getpid())},
         {"rootUri", FileUri(workspace_root_)},
         {"capabilities", Json::object()},
         {"workspaceFolders",
          Json::array({{{"uri", FileUri(workspace_root_)},
                        {"name", workspace_root_.filename().string()}}})}});
    SendNotification("initialized", Json::object());
    initialized_ = true;
  }

  void Start() {
    if (pid_ > 0) {
      return;
    }
    std::array<int, 2> stdin_pipe{};
    std::array<int, 2> stdout_pipe{};
    if (pipe(stdin_pipe.data()) != 0 || pipe(stdout_pipe.data()) != 0) {
      throw std::runtime_error("Unable to create LSP pipes.");
    }
    pid_ = fork();
    if (pid_ < 0) {
      throw std::runtime_error("Unable to fork LSP server.");
    }
    if (pid_ == 0) {
      dup2(stdin_pipe[0], STDIN_FILENO);
      dup2(stdout_pipe[1], STDOUT_FILENO);
      close(stdin_pipe[0]);
      close(stdin_pipe[1]);
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);

      std::vector<std::string> argv_storage;
      argv_storage.push_back(config_.command);
      argv_storage.insert(argv_storage.end(), config_.args.begin(),
                          config_.args.end());
      std::vector<char*> argv;
      for (auto& item : argv_storage) {
        argv.push_back(item.data());
      }
      argv.push_back(nullptr);
      execvp(argv[0], argv.data());
      _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    write_fd_ = stdin_pipe[1];
    read_fd_ = stdout_pipe[0];
    reader_ = std::jthread([this](std::stop_token st) { ReaderLoop(st); });
  }

  void Stop() {
    if (reader_.joinable()) {
      reader_.request_stop();
    }
    if (read_fd_ >= 0) {
      close(read_fd_);
      read_fd_ = -1;
    }
    if (write_fd_ >= 0) {
      close(write_fd_);
      write_fd_ = -1;
    }
    if (pid_ > 0) {
      kill(pid_, SIGTERM);
      waitpid(pid_, nullptr, 0);
      pid_ = -1;
    }
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

  Json SendRequest(const std::string& method, Json params) {
    const auto id = next_id_.fetch_add(1);
    SendMessage({{"jsonrpc", "2.0"},
                 {"id", id},
                 {"method", method},
                 {"params", std::move(params)}});

    std::unique_lock lock(mutex_);
    const bool ready = response_wake_.wait_for(
        lock, config_.request_timeout,
        [&] { return responses_.find(id) != responses_.end(); });
    if (!ready) {
      throw std::runtime_error("LSP request timed out: " + method);
    }
    auto response = std::move(responses_[id]);
    responses_.erase(id);
    if (response.contains("error")) {
      throw std::runtime_error(response["error"].dump());
    }
    return response;
  }

  void SendNotification(const std::string& method, Json params) {
    SendMessage({{"jsonrpc", "2.0"},
                 {"method", method},
                 {"params", std::move(params)}});
  }

  void SendMessage(const Json& message) {
    const auto body = message.dump();
    const auto header =
        "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    WriteBytes(header);
    WriteBytes(body);
  }

  void WriteBytes(const std::string& bytes) const {
    size_t written = 0;
    while (written < bytes.size()) {
      const auto result =
          write(write_fd_, bytes.data() + written, bytes.size() - written);
      if (result < 0) {
        throw std::runtime_error("Failed to write to LSP server: " +
                                 std::string(std::strerror(errno)));
      }
      written += static_cast<size_t>(result);
    }
  }

  void ReaderLoop(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
      auto message = ReadMessage();
      if (!message.has_value()) {
        return;
      }
      ProcessMessage(*message);
    }
  }

  std::optional<std::string> ReadMessage() {
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
        content_length =
            static_cast<size_t>(std::stoul(line->substr(kHeader.size())));
      }
    }
    if (content_length == 0) {
      return std::string{};
    }
    std::string body(content_length, '\0');
    size_t read_count = 0;
    while (read_count < content_length) {
      const auto result =
          read(read_fd_, body.data() + read_count, content_length - read_count);
      if (result <= 0) {
        return std::nullopt;
      }
      read_count += static_cast<size_t>(result);
    }
    return body;
  }

  [[nodiscard]] std::optional<std::string> ReadLine() const {
    std::string line;
    char ch = '\0';
    while (true) {
      const auto result = read(read_fd_, &ch, 1);
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

  void ProcessMessage(const std::string& body) {
    Json message;
    try {
      message = Json::parse(body);
    } catch (const std::exception&) {
      return;
    }
    if (message.contains("id")) {
      const auto id = message["id"].get<int>();
      {
        std::lock_guard lock(mutex_);
        responses_[id] = std::move(message);
      }
      response_wake_.notify_all();
      return;
    }
    if (!message.contains("method") || !message["method"].is_string()) {
      return;
    }
    if (message["method"].get<std::string>() ==
        "textDocument/publishDiagnostics") {
      ProcessDiagnostics(message["params"]);
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
      std::lock_guard lock(mutex_);
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
        ParseLocation(item, locations);
      }
      return locations;
    }
    ParseLocation(result, locations);
    return locations;
  }

  void ParseLocation(const Json& item, std::vector<LspLocation>& locations) {
    if (!item.contains("uri") || !item.contains("range")) {
      return;
    }
    const auto path =
        DisplayPath(PathFromFileUri(item["uri"].get<std::string>()));
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

  void ParseSymbols(const Json& result, std::vector<LspSymbol>& symbols) {
    if (!result.is_array()) {
      return;
    }
    for (const auto& item : result) {
      ParseSymbol(item, symbols);
    }
  }

  void ParseSymbol(const Json& item, std::vector<LspSymbol>& symbols) {
    if (!item.contains("name")) {
      return;
    }
    int line = 1;
    if (item.contains("selectionRange")) {
      line = JsonIntAt(item["selectionRange"]["start"], "line", 0) + 1;
    } else if (item.contains("location")) {
      line = JsonIntAt(item["location"]["range"]["start"], "line", 0) + 1;
    }
    symbols.push_back(
        LspSymbol{.name = JsonStringAt(item, "name"),
                  .kind = SymbolKindFromLsp(JsonIntAt(item, "kind", 0)),
                  .line = line});
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
    auto normalized = std::filesystem::absolute(path).lexically_normal();
    std::error_code error;
    auto relative =
        std::filesystem::relative(normalized, workspace_root_, error);
    if (!error && !relative.empty() && !relative.native().starts_with("..")) {
      return relative.string();
    }
    return normalized.string();
  }

  LspServerConfig config_;
  std::filesystem::path workspace_root_;
  std::mutex start_mutex_;
  std::mutex mutex_;
  std::condition_variable_any response_wake_;
  std::condition_variable_any diagnostics_wake_;
  std::map<int, Json> responses_;
  std::map<std::string, std::vector<LspDiagnostic>> diagnostics_;
  std::set<std::string> opened_documents_;
  std::map<std::string, int> document_versions_;
  std::atomic<int> next_id_{1};
  std::jthread reader_;
  pid_t pid_ = -1;
  int read_fd_ = -1;
  int write_fd_ = -1;
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
