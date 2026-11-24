#include "LanguageServer.h"

#include "Rename.h"
#include "Workspace.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include <istream>
#include <ostream>
#include <stdexcept>

namespace riktig {

namespace {

constexpr const char *renameKind = "refactor.rewrite.riktig.rename";
constexpr int methodNotFound = -32601;
constexpr int invalidRequest = -32600;
constexpr int internalError = -32603;
constexpr int serverNotInitialized = -32002;

struct RequestFailure : std::runtime_error {
    RequestFailure(int code, const std::string &message) : std::runtime_error(message), code(code) {}
    int code;
};

int hexValue(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return -1;
}

std::string findCompileCommands(const std::string &folder) {
    for (const char *relative : {"compile_commands.json", "build/compile_commands.json"}) {
        llvm::SmallString<256> candidate(folder);
        llvm::sys::path::append(candidate, relative);
        if (llvm::sys::fs::exists(candidate)) {
            return std::string(candidate);
        }
    }
    return {};
}

bool kindRequested(const llvm::json::Object *parameters) {
    const auto *context = parameters ? parameters->getObject("context") : nullptr;
    const auto *only = context ? context->getArray("only") : nullptr;
    if (!only) {
        return true;
    }
    for (const auto &entry : *only) {
        auto kind = entry.getAsString();
        if (kind &&
            (llvm::StringRef(renameKind) == *kind || llvm::StringRef(renameKind).starts_with((*kind + ".").str()))) {
            return true;
        }
    }
    return false;
}

} // namespace

std::string pathFromFileUri(const std::string &uri) {
    constexpr llvm::StringLiteral scheme = "file://";
    if (!llvm::StringRef(uri).starts_with(scheme)) {
        return {};
    }
    std::string decoded;
    for (size_t index = scheme.size(); index < uri.size(); ++index) {
        if (uri[index] == '%' && index + 2 < uri.size() && hexValue(uri[index + 1]) >= 0 &&
            hexValue(uri[index + 2]) >= 0) {
            decoded += static_cast<char>(hexValue(uri[index + 1]) * 16 + hexValue(uri[index + 2]));
            index += 2;
        } else {
            decoded += uri[index];
        }
    }
    // file:///C:/dir has the path /C:/dir; drop the slash before a drive letter.
    if (decoded.size() >= 3 && decoded[0] == '/' && std::isalpha(static_cast<unsigned char>(decoded[1])) &&
        decoded[2] == ':') {
        decoded.erase(0, 1);
    }
    llvm::SmallString<256> native(decoded);
    llvm::sys::path::native(native);
    return std::string(native);
}

int LanguageServer::run() {
    while (auto message = readMessage()) {
        const auto *object = message->getAsObject();
        if (!object) {
            continue;
        }
        if (auto method = object->getString("method"); method && *method == "exit") {
            return shutdownRequested_ ? 0 : 1;
        }
        handle(*object);
    }
    return 1;
}

std::optional<llvm::json::Value> LanguageServer::readMessage() {
    long contentLength = -1;
    std::string line;
    while (std::getline(input_, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        llvm::StringRef header(line);
        if (header.starts_with_insensitive("Content-Length:")) {
            header.drop_front(15).trim().getAsInteger(10, contentLength);
        }
    }
    if (contentLength < 0) {
        return std::nullopt;
    }
    std::string body(static_cast<size_t>(contentLength), '\0');
    input_.read(body.data(), contentLength);
    if (input_.gcount() != contentLength) {
        return std::nullopt;
    }
    auto parsed = llvm::json::parse(body);
    if (!parsed) {
        llvm::consumeError(parsed.takeError());
        return llvm::json::Value(nullptr);
    }
    return std::move(*parsed);
}

void LanguageServer::writeMessage(llvm::json::Value message) {
    std::string body;
    llvm::raw_string_ostream stream(body);
    stream << message;
    output_ << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

void LanguageServer::writeError(const llvm::json::Value &id, int code, const std::string &message) {
    writeMessage(llvm::json::Object{
        {"jsonrpc", "2.0"}, {"id", id}, {"error", llvm::json::Object{{"code", code}, {"message", message}}}});
}

void LanguageServer::handle(const llvm::json::Object &message) {
    auto method = message.getString("method");
    const auto *id = message.get("id");
    if (!method || !id) {
        return; // Notifications and responses need no reply.
    }
    const auto *parameters = message.getObject("params");
    try {
        if (*method != "initialize" && !initialized_) {
            throw RequestFailure(serverNotInitialized, "The server has not been initialized.");
        }
        llvm::json::Value result = nullptr;
        if (*method == "initialize") {
            result = initialize(parameters);
        } else if (*method == "shutdown") {
            shutdownRequested_ = true;
        } else if (*method == "textDocument/codeAction") {
            result = codeActions(parameters);
        } else if (*method == "workspace/executeCommand") {
            result = executeCommand(parameters);
        } else {
            throw RequestFailure(methodNotFound, "Unsupported request " + method->str() + ".");
        }
        writeMessage(llvm::json::Object{{"jsonrpc", "2.0"}, {"id", *id}, {"result", std::move(result)}});
    } catch (const RequestFailure &failure) {
        writeError(*id, failure.code, failure.what());
    } catch (const std::exception &exception) {
        writeError(*id, internalError, std::string("Internal error: ") + exception.what());
    }
}

llvm::json::Value LanguageServer::initialize(const llvm::json::Object *parameters) {
    std::string folder;
    if (parameters) {
        std::optional<llvm::StringRef> uri;
        if (const auto *folders = parameters->getArray("workspaceFolders"); folders && !folders->empty()) {
            if (const auto *first = (*folders)[0].getAsObject()) {
                uri = first->getString("uri");
            }
        }
        if (!uri) {
            uri = parameters->getString("rootUri");
        }
        if (uri) {
            folder = pathFromFileUri(uri->str());
        }
        const auto *options = parameters->getObject("initializationOptions");
        if (auto configured = options ? options->getString("workspace") : std::nullopt) {
            workspacePath_ = normalizedAbsolutePath(*configured, folder.empty() ? "." : folder);
        }
    }
    if (workspacePath_.empty() && !folder.empty()) {
        workspacePath_ = findCompileCommands(folder);
    }
    initialized_ = true;
    return llvm::json::Object{
        {"capabilities",
         llvm::json::Object{
             {"codeActionProvider", llvm::json::Object{{"codeActionKinds", llvm::json::Array{renameKind}}}},
             {"executeCommandProvider", llvm::json::Object{{"commands", llvm::json::Array{refactorCommand}}}}}},
        {"serverInfo", llvm::json::Object{{"name", "riktig-cpp"}}}};
}

llvm::json::Value LanguageServer::codeActions(const llvm::json::Object *parameters) {
    if (!kindRequested(parameters)) {
        return llvm::json::Array{};
    }
    llvm::json::Value uri = nullptr;
    llvm::json::Value range = nullptr;
    if (parameters) {
        if (const auto *document = parameters->getObject("textDocument"); document && document->get("uri")) {
            uri = *document->get("uri");
        }
        if (const auto *requestedRange = parameters->get("range")) {
            range = *requestedRange;
        }
    }
    return llvm::json::Array{llvm::json::Object{
        {"title", "Riktig Rename"},
        {"kind", renameKind},
        {"command", llvm::json::Object{{"title", "Riktig Rename"},
                                       {"command", "riktigrefactor.rename"},
                                       {"arguments", llvm::json::Array{std::move(uri), std::move(range)}}}}}};
}

llvm::json::Value LanguageServer::executeCommand(const llvm::json::Object *parameters) {
    auto command = parameters ? parameters->getString("command") : std::nullopt;
    if (!command || *command != refactorCommand) {
        throw RequestFailure(methodNotFound, "Unsupported command.");
    }
    const auto *arguments = parameters->getArray("arguments");
    const auto *request = arguments && !arguments->empty() ? (*arguments)[0].getAsObject() : nullptr;
    if (!request) {
        throw RequestFailure(invalidRequest, "The command needs a protocol request as its argument.");
    }
    auto refactoring = request->getString("refactoring");
    if (!refactoring || *refactoring != "rename") {
        throw RequestFailure(invalidRequest, "Unsupported refactoring.");
    }
    return rename(*request);
}

llvm::json::Value LanguageServer::rename(const llvm::json::Object &request) {
    std::string workspacePath = workspacePath_;
    if (auto given = request.getString("workspace")) {
        workspacePath = given->str();
    }
    if (workspacePath.empty()) {
        throw RequestFailure(invalidRequest, "No compile_commands.json was found in the workspace folder or its build "
                                             "folder. Generate one (CMake: -DCMAKE_EXPORT_COMPILE_COMMANDS=ON), or set "
                                             "the riktigrefactor.cpp.workspace setting.");
    }
    const auto *location = request.getObject("location");
    auto file = location ? location->getString("file") : std::nullopt;
    const auto *start = location ? location->getObject("start") : nullptr;
    auto line = start ? start->getInteger("line") : std::nullopt;
    auto column = start ? start->getInteger("column") : std::nullopt;
    const auto *options = request.getObject("options");
    auto newName = options ? options->getString("newName") : std::nullopt;
    if (!file || !line || !column || !newName) {
        throw RequestFailure(invalidRequest, "Rename needs location.file, location.start and options.newName.");
    }
    auto workspace = Workspace::load(workspacePath);
    if (!workspace) {
        throw RequestFailure(internalError, llvm::toString(workspace.takeError()));
    }
    Position at;
    at.line = static_cast<unsigned>(*line);
    at.column = static_cast<unsigned>(*column);
    auto absolute = workspace->absolutePath(file->str());
    if (!llvm::sys::fs::exists(absolute)) {
        throw RequestFailure(internalError, "No such file: " + file->str());
    }
    return toJson(riktig::rename(*workspace, RenameRequest{absolute, at, newName->str()}));
}

} // namespace riktig
