#include "mcp/oauth/browser_launcher.hpp"

#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <array>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace yac::mcp::oauth {

#if defined(_WIN32)

bool LaunchBrowser(std::string_view url) {
  const std::string url_str(url);
  const HINSTANCE result = ShellExecuteA(nullptr, "open", url_str.c_str(),
                                         nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
}

#else

namespace {

bool ForkExec(const char* cmd, std::string url_str) {
  const pid_t pid = fork();
  if (pid < 0) {
    return false;
  }
  if (pid == 0) {
    const pid_t grandchild = fork();
    if (grandchild != 0) {
      std::_Exit(0);
    }
    std::string cmd_str(cmd);
    std::array<char*, 3> argv_buf{};
    argv_buf[0] = cmd_str.data();
    argv_buf[1] = url_str.data();
    argv_buf[2] = nullptr;
    execvp(argv_buf[0], argv_buf.data());
    std::_Exit(127);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return true;
}

}  // namespace

bool LaunchBrowser(std::string_view url) {
#if defined(__APPLE__)
  return ForkExec("open", std::string(url));
#else
  return ForkExec("xdg-open", std::string(url));
#endif
}

#endif

}  // namespace yac::mcp::oauth
