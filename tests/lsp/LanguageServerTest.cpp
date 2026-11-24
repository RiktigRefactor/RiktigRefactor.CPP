#include "LanguageServer.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"

#include <fstream>
#include <iostream>
#include <sstream>

using namespace riktig;
using llvm::json::Array;
using llvm::json::Object;
using llvm::json::Value;

namespace {

int failures = 0;

void expect(bool condition, const std::string &description) {
    if (!condition) {
        std::cerr << "FAILED: " << description << "\n";
        ++failures;
    }
}

std::string frame(const Value &message) {
    std::string body;
    llvm::raw_string_ostream stream(body);
    stream << message;
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

Value request(int id, const std::string &method, Value parameters) {
    return Object{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", std::move(parameters)}};
}

Value exitNotification() { return Object{{"jsonrpc", "2.0"}, {"method", "exit"}}; }

struct Conversation {
    int exitCode = 0;
    std::vector<Value> responses;
};

Conversation converse(const std::vector<Value> &messages) {
    std::string wire;
    for (const auto &message : messages) {
        wire += frame(message);
    }
    std::istringstream input(wire);
    std::ostringstream output;
    Conversation conversation;
    conversation.exitCode = LanguageServer(input, output).run();
    std::string text = output.str();
    size_t offset = 0;
    while (offset < text.size()) {
        size_t headerEnd = text.find("\r\n\r\n", offset);
        size_t length = std::stoul(text.substr(offset + 16, headerEnd - offset - 16));
        auto parsed = llvm::json::parse(text.substr(headerEnd + 4, length));
        conversation.responses.push_back(std::move(*parsed));
        offset = headerEnd + 4 + length;
    }
    return conversation;
}

std::string uriOf(const std::string &path) {
    std::string normalized = path;
    for (auto &character : normalized) {
        if (character == '\\') {
            character = '/';
        }
    }
    return (normalized.starts_with("/") ? "file://" : "file:///") + normalized;
}

Value initialize(int id, const std::string &folder) {
    return request(id, "initialize", Object{{"rootUri", uriOf(folder)}, {"capabilities", Object{}}});
}

Value renameRequest(int id, const std::string &newName, int line, int column) {
    return request(
        id, "workspace/executeCommand",
        Object{{"command", "riktig.refactor"},
               {"arguments", Array{Object{{"refactoring", "rename"},
                                          {"location", Object{{"file", "calc.cpp"},
                                                              {"start", Object{{"line", line}, {"column", column}}},
                                                              {"end", Object{{"line", line}, {"column", column}}}}},
                                          {"options", Object{{"newName", newName}}}}}}});
}

const Object &resultOf(const Conversation &conversation, size_t index) {
    return *conversation.responses.at(index).getAsObject()->getObject("result");
}

std::string makeProject(bool withCompileCommands) {
    llvm::SmallString<128> directory;
    llvm::sys::fs::createUniqueDirectory("riktig-cpp-lsp", directory);
    std::string folder(directory);
    std::ofstream(folder + "/calc.cpp")
        << "int twice(int value) { return value * 2; }\nint main() { return twice(0); }\n";
    if (withCompileCommands) {
        std::ofstream(folder + "/compile_commands.json")
            << "[{\"directory\": \"" << uriOf(folder).substr(8)
            << "\", \"arguments\": [\"clang++\", \"-std=c++20\", \"-c\", \"calc.cpp\"], "
            << "\"file\": \"calc.cpp\"}]";
    }
    return folder;
}

void testInitializeAdvertisesCodeActionsAndTheRefactorCommand() {
    auto conversation = converse({initialize(1, "/nowhere"), exitNotification()});
    const auto *capabilities = resultOf(conversation, 0).getObject("capabilities");
    expect(capabilities->getObject("codeActionProvider")->getArray("codeActionKinds")->front().getAsString() ==
               std::optional<llvm::StringRef>("refactor.rewrite.riktig.rename"),
           "initialize advertises the rename kind");
    expect(capabilities->getObject("executeCommandProvider")->getArray("commands")->front().getAsString() ==
               std::optional<llvm::StringRef>("riktig.refactor"),
           "initialize advertises the refactor command");
}

void testCodeActionOffersRenameAsAClientCommand() {
    auto conversation = converse({initialize(1, "/nowhere"),
                                  request(2, "textDocument/codeAction",
                                          Object{{"textDocument", Object{{"uri", "file:///x/calc.cpp"}}},
                                                 {"range", Object{{"start", Object{{"line", 0}, {"character", 4}}},
                                                                  {"end", Object{{"line", 0}, {"character", 4}}}}},
                                                 {"context", Object{{"diagnostics", Array{}}}}}),
                                  exitNotification()});
    const auto *action = conversation.responses.at(1).getAsObject()->getArray("result")->front().getAsObject();
    expect(action->getString("kind") == std::optional<llvm::StringRef>("refactor.rewrite.riktig.rename"), "kind");
    const auto *command = action->getObject("command");
    expect(command->getString("command") == std::optional<llvm::StringRef>("riktigrefactor.rename"), "client command");
    expect(command->getArray("arguments")->front().getAsString() ==
               std::optional<llvm::StringRef>("file:///x/calc.cpp"),
           "the command carries the document");
    expect(*(*command->getArray("arguments"))[1].getAsObject()->getObject("start")->getInteger("character") == 4,
           "the command carries the range");
}

void testCodeActionRespectsTheRequestedKinds() {
    auto conversation =
        converse({initialize(1, "/nowhere"),
                  request(2, "textDocument/codeAction", Object{{"context", Object{{"only", Array{"quickfix"}}}}}),
                  exitNotification()});
    expect(conversation.responses.at(1).getAsObject()->getArray("result")->empty(), "no action for other kinds");
}

void testExecutingRenameReturnsTheProtocolResultAndWritesNothing() {
    auto folder = makeProject(true);
    auto conversation = converse({initialize(1, folder), renameRequest(2, "doubled", 1, 5), exitNotification()});
    const auto &result = resultOf(conversation, 1);
    expect(result.getString("status") == std::optional<llvm::StringRef>("success"), "status is success");
    expect(result.getArray("edits")->size() == 2, "the declaration and the call are edited");
    expect(result.getArray("edits")->front().getAsObject()->getString("file") ==
               std::optional<llvm::StringRef>("calc.cpp"),
           "edit files are relative to the workspace root");
    std::ifstream file(folder + "/calc.cpp");
    std::stringstream text;
    text << file.rdbuf();
    expect(text.str().find("twice") != std::string::npos && text.str().find("doubled") == std::string::npos,
           "the file on disk is unchanged");
}

void testARefusalIsAProtocolResultNotAnError() {
    auto folder = makeProject(true);
    auto conversation = converse({initialize(1, folder), renameRequest(2, "class", 1, 5), exitNotification()});
    expect(resultOf(conversation, 1).getString("status") == std::optional<llvm::StringRef>("refused"),
           "status is refused");
    expect(!resultOf(conversation, 1).getArray("reasons")->empty(), "a refusal carries reasons");
}

void testExecutingWithoutCompileCommandsIsAnErrorThatSaysWhatIsMissing() {
    auto folder = makeProject(false);
    auto conversation = converse({initialize(1, folder), renameRequest(2, "doubled", 1, 5), exitNotification()});
    auto message = conversation.responses.at(1).getAsObject()->getObject("error")->getString("message");
    expect(message && message->contains("compile_commands.json"), "the error names compile_commands.json");
}

void testRequestsBeforeInitializeAreRejected() {
    auto conversation = converse({renameRequest(1, "doubled", 1, 5), exitNotification()});
    expect(*conversation.responses.at(0).getAsObject()->getObject("error")->getInteger("code") == -32002,
           "ServerNotInitialized");
}

void testUnknownRequestsGetMethodNotFound() {
    auto conversation =
        converse({initialize(1, "/nowhere"), request(2, "textDocument/hover", Object{}), exitNotification()});
    expect(*conversation.responses.at(1).getAsObject()->getObject("error")->getInteger("code") == -32601,
           "MethodNotFound");
}

void testExitCodes() {
    expect(converse({initialize(1, "/nowhere"), request(2, "shutdown", nullptr), exitNotification()}).exitCode == 0,
           "exit after shutdown returns 0");
    expect(converse({initialize(1, "/nowhere"), exitNotification()}).exitCode == 1, "exit without shutdown returns 1");
}

void testFileUris() {
    expect(pathFromFileUri("file:///home/ana/a%20b.cpp") == "/home/ana/a b.cpp", "percent-decoding");
    expect(pathFromFileUri("https://example.com").empty(), "other schemes are not paths");
}

} // namespace

int main() {
    testInitializeAdvertisesCodeActionsAndTheRefactorCommand();
    testCodeActionOffersRenameAsAClientCommand();
    testCodeActionRespectsTheRequestedKinds();
    testExecutingRenameReturnsTheProtocolResultAndWritesNothing();
    testARefusalIsAProtocolResultNotAnError();
    testExecutingWithoutCompileCommandsIsAnErrorThatSaysWhatIsMissing();
    testRequestsBeforeInitializeAreRejected();
    testUnknownRequestsGetMethodNotFound();
    testExitCodes();
    testFileUris();
    return failures == 0 ? 0 : 1;
}
