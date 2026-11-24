#pragma once

#include "llvm/Support/JSON.h"

#include <iosfwd>
#include <optional>
#include <string>

namespace riktig {

// A minimal language server (LSP over a stream pair) that offers Rename as a code action and runs it on request.
// It reads files from disk, so the client saves modified files before running a refactoring, and it loads the
// workspace for every request.
class LanguageServer {
  public:
    static constexpr const char *refactorCommand = "riktig.refactor";

    LanguageServer(std::istream &input, std::ostream &output) : input_(input), output_(output) {}

    // Serves requests until the client sends `exit` or closes the stream. Returns the exit code the LSP
    // specification asks for: 0 after `shutdown`, 1 otherwise.
    int run();

  private:
    std::optional<llvm::json::Value> readMessage();
    void writeMessage(llvm::json::Value message);
    void writeError(const llvm::json::Value &id, int code, const std::string &message);
    void handle(const llvm::json::Object &message);

    llvm::json::Value initialize(const llvm::json::Object *parameters);
    llvm::json::Value codeActions(const llvm::json::Object *parameters);
    llvm::json::Value executeCommand(const llvm::json::Object *parameters);
    llvm::json::Value rename(const llvm::json::Object &request);

    std::istream &input_;
    std::ostream &output_;
    std::string workspacePath_;
    bool initialized_ = false;
    bool shutdownRequested_ = false;
};

// Turns a `file:` URI into a native path, or returns an empty string for any other URI.
std::string pathFromFileUri(const std::string &uri);

} // namespace riktig
