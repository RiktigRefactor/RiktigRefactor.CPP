#include "Workspace.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/PCHContainerOperations.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"

#ifndef RIKTIG_CLANG_RESOURCE_DIR
#error "RIKTIG_CLANG_RESOURCE_DIR must name clang's resource directory"
#endif

namespace riktig {

std::string normalizedAbsolutePath(llvm::StringRef path, llvm::StringRef baseDirectory) {
    llvm::SmallString<256> result(path);
    if (!llvm::sys::path::is_absolute(result)) {
        llvm::SmallString<256> joined(baseDirectory);
        llvm::sys::path::append(joined, path);
        result = joined;
    }
    llvm::sys::path::remove_dots(result, /*remove_dot_dot=*/true);
    llvm::sys::path::native(result);
    return std::string(result);
}

static std::string commonDirectory(llvm::StringRef first, llvm::StringRef second) {
    llvm::SmallVector<llvm::StringRef> firstParts;
    llvm::SmallVector<llvm::StringRef> secondParts;
    for (auto it = llvm::sys::path::begin(first); it != llvm::sys::path::end(first); ++it) {
        firstParts.push_back(*it);
    }
    for (auto it = llvm::sys::path::begin(second); it != llvm::sys::path::end(second); ++it) {
        secondParts.push_back(*it);
    }
    llvm::SmallString<256> common;
    for (size_t index = 0; index < firstParts.size() && index < secondParts.size(); ++index) {
        if (firstParts[index] != secondParts[index]) {
            break;
        }
        llvm::sys::path::append(common, firstParts[index]);
    }
    return std::string(common);
}

llvm::Expected<Workspace> Workspace::load(llvm::StringRef compileCommandsPath) {
    llvm::SmallString<256> absolute(compileCommandsPath);
    if (auto error = llvm::sys::fs::make_absolute(absolute)) {
        return llvm::createStringError(error, "Cannot resolve %s", compileCommandsPath.str().c_str());
    }
    llvm::sys::path::remove_dots(absolute, true);
    if (llvm::sys::fs::is_directory(absolute)) {
        llvm::sys::path::append(absolute, "compile_commands.json");
    }
    if (!llvm::sys::fs::exists(absolute)) {
        return llvm::createStringError(std::errc::no_such_file_or_directory,
                                       "No compile_commands.json at %s. The tool needs one to resolve macros and "
                                       "includes; CMake writes it with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON.",
                                       absolute.c_str());
    }
    std::string errorMessage;
    auto database = clang::tooling::JSONCompilationDatabase::loadFromFile(
        absolute, errorMessage, clang::tooling::JSONCommandLineSyntax::AutoDetect);
    if (!database) {
        return llvm::createStringError(std::errc::invalid_argument, "%s", errorMessage.c_str());
    }
    Workspace workspace;
    std::string root(llvm::sys::path::parent_path(absolute));
    for (const auto &command : database->getAllCompileCommands()) {
        auto file = normalizedAbsolutePath(command.Filename, command.Directory);
        if (std::find(workspace.translationUnits_.begin(), workspace.translationUnits_.end(), file) ==
            workspace.translationUnits_.end()) {
            workspace.translationUnits_.push_back(file);
        }
        root = commonDirectory(root, llvm::sys::path::parent_path(file));
    }
    workspace.root_ = root;
    workspace.database_ = std::move(database);
    return workspace;
}

bool Workspace::contains(llvm::StringRef absolutePath) const {
    llvm::StringRef path(absolutePath);
    if (!path.starts_with(root_)) {
        return false;
    }
    auto rest = path.drop_front(root_.size());
    return !rest.empty() && llvm::sys::path::is_separator(rest.front());
}

std::string Workspace::displayPath(llvm::StringRef absolutePath) const {
    if (!contains(absolutePath)) {
        return absolutePath.str();
    }
    llvm::SmallString<256> relative(absolutePath.drop_front(root_.size() + 1));
    llvm::sys::path::native(relative, llvm::sys::path::Style::posix);
    return std::string(relative);
}

std::string Workspace::absolutePath(llvm::StringRef pathRelativeToRootOrAbsolute) const {
    return normalizedAbsolutePath(pathRelativeToRootOrAbsolute, root_);
}

llvm::Expected<std::string> Workspace::read(llvm::StringRef absolutePath, const FileOverlay &overlay) const {
    if (auto overlaid = overlay.find(absolutePath.str()); overlaid != overlay.end()) {
        return overlaid->second;
    }
    auto buffer = llvm::MemoryBuffer::getFile(absolutePath);
    if (!buffer) {
        return llvm::createStringError(buffer.getError(), "Cannot read %s", absolutePath.str().c_str());
    }
    return (*buffer)->getBuffer().str();
}

namespace {

class ErrorCollector : public clang::DiagnosticConsumer {
  public:
    explicit ErrorCollector(std::vector<std::string> &errors) : errors_(errors) {}

    void HandleDiagnostic(clang::DiagnosticsEngine::Level level, const clang::Diagnostic &info) override {
        clang::DiagnosticConsumer::HandleDiagnostic(level, info);
        if (level < clang::DiagnosticsEngine::Error) {
            return;
        }
        llvm::SmallString<256> message;
        info.FormatDiagnostic(message);
        std::string where;
        if (info.hasSourceManager() && info.getLocation().isValid()) {
            auto presumed = info.getSourceManager().getPresumedLoc(info.getLocation());
            if (presumed.isValid()) {
                where = std::string(presumed.getFilename()) + ":" + std::to_string(presumed.getLine()) + ":" +
                        std::to_string(presumed.getColumn()) + ": ";
            }
        }
        errors_.push_back(where + std::string(message));
    }

  private:
    std::vector<std::string> &errors_;
};

} // namespace

ParsedWorkspace parse(const Workspace &workspace, const FileOverlay &overlay) {
    ParsedWorkspace parsed;
    ErrorCollector collector(parsed.errors);
    // Each translation unit gets its own file manager whose working directory is the compile command's directory,
    // so relative file names can be made absolute after parsing.
    for (const auto &translationUnit : workspace.translationUnits()) {
        auto memory = llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
        for (const auto &[path, content] : overlay) {
            memory->addFile(path, 0, llvm::MemoryBuffer::getMemBufferCopy(content, path));
        }
        auto fileSystem = llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(llvm::vfs::getRealFileSystem());
        fileSystem->pushOverlay(memory);
        clang::FileSystemOptions options;
        auto commands = workspace.compilationDatabase().getCompileCommands(translationUnit);
        if (!commands.empty()) {
            options.WorkingDir = commands.front().Directory;
        }
        auto files = llvm::makeIntrusiveRefCnt<clang::FileManager>(options, fileSystem);
        clang::tooling::ClangTool tool(workspace.compilationDatabase(), {translationUnit},
                                       std::make_shared<clang::PCHContainerOperations>(), fileSystem, files);
        tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
            {"-resource-dir=" RIKTIG_CLANG_RESOURCE_DIR, "-Xclang", "-detailed-preprocessing-record", "-fsyntax-only"},
            clang::tooling::ArgumentInsertPosition::END));
        tool.setDiagnosticConsumer(&collector);
        std::vector<std::unique_ptr<clang::ASTUnit>> units;
        if (tool.buildASTs(units) != 0 || units.size() != 1) {
            parsed.parsedEveryTranslationUnit = false;
        }
        for (auto &unit : units) {
            unit->getDiagnostics().setClient(new clang::IgnoringDiagConsumer(), /*ShouldOwnClient=*/true);
            parsed.units.push_back(std::move(unit));
        }
    }
    return parsed;
}

} // namespace riktig
