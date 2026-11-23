// Runs one test case in the format of the specification's test-format.md.
// Usage: riktig-cpp-case-runner <case directory>
// RIKTIG_WRITE_EXPECTED=1 writes the expected/ directory from the output instead of comparing with it.
// RIKTIG_KEEP_TEMP=1 keeps the temporary directory for inspection.

#include "Naive.h"
#include "Rename.h"
#include "Results.h"
#include "SourceText.h"
#include "Workspace.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace riktig;
namespace fs = llvm::sys::fs;
namespace path = llvm::sys::path;

namespace {

struct Failure {
    std::string message;
};

struct CaseDefinition {
    std::string refactoring;
    llvm::json::Object options;
    std::optional<std::string> refuse;
    bool naiveShouldFail = false;
};

struct Markers {
    std::string file; // relative to the input directory
    unsigned start = 0;
    unsigned end = 0;
    bool found = false;
};

std::string readFile(const std::string &file) {
    auto buffer = llvm::MemoryBuffer::getFile(file);
    if (!buffer) {
        throw Failure{"Cannot read " + file};
    }
    return (*buffer)->getBuffer().str();
}

void writeFile(const std::string &file, const std::string &content) {
    fs::create_directories(path::parent_path(file));
    std::error_code error;
    llvm::raw_fd_ostream out(file, error);
    if (error) {
        throw Failure{"Cannot write " + file + ": " + error.message()};
    }
    out << content;
}

// Relative paths of the regular files under a directory, with `/` separators.
std::vector<std::string> listFiles(const std::string &directory) {
    std::vector<std::string> files;
    std::error_code error;
    for (fs::recursive_directory_iterator it(directory, error), end; it != end && !error; it.increment(error)) {
        if (it->type() == fs::file_type::regular_file) {
            llvm::SmallString<256> relative(llvm::StringRef(it->path()).drop_front(directory.size() + 1));
            path::native(relative, path::Style::posix);
            files.push_back(std::string(relative));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool isSourceFile(llvm::StringRef file) { return file.ends_with(".cpp") || file.ends_with(".h"); }

CaseDefinition readCase(const std::string &caseDirectory) {
    auto text = readFile(caseDirectory + "/case.json");
    auto parsed = llvm::json::parse(text);
    if (!parsed) {
        throw Failure{"case.json is not valid JSON: " + llvm::toString(parsed.takeError())};
    }
    auto *object = parsed->getAsObject();
    if (!object) {
        throw Failure{"case.json must hold an object"};
    }
    CaseDefinition definition;
    llvm::SmallVector<llvm::StringRef> components(path::begin(caseDirectory), path::end(caseDirectory));
    for (size_t index = 0; index + 1 < components.size(); ++index) {
        if (components[index] == "cases") {
            definition.refactoring = components[index + 1].str();
        }
    }
    if (auto *options = object->getObject("options")) {
        definition.options = *options;
    }
    if (auto refuse = object->getString("refuse")) {
        definition.refuse = refuse->str();
    }
    definition.naiveShouldFail = object->getBoolean("naiveShouldFail").value_or(false);
    auto *spec = object->getArray("spec");
    if (!spec || spec->empty()) {
        throw Failure{"case.json must cite the C++ standard sections it covers in `spec`"};
    }
    return definition;
}

// Copies the input files, removing the cursor or selection markers.
Markers copyInputWithoutMarkers(const std::string &inputDirectory, const std::string &workspaceDirectory) {
    Markers markers;
    for (const auto &file : listFiles(inputDirectory)) {
        auto text = readFile(inputDirectory + "/" + file);
        auto takeMarker = [&](llvm::StringRef marker) -> std::optional<unsigned> {
            auto at = text.find(marker.str());
            if (at == std::string::npos) {
                return std::nullopt;
            }
            text.erase(at, marker.size());
            return static_cast<unsigned>(at);
        };
        auto cursor = takeMarker("/*|*/");
        auto start = takeMarker("/*[*/");
        auto end = takeMarker("/*]*/");
        if (cursor || start || end) {
            if (markers.found) {
                throw Failure{"Only one file may hold markers"};
            }
            markers.found = true;
            markers.file = file;
            if (cursor) {
                markers.start = markers.end = *cursor;
            } else if (start && end) {
                markers.start = *start;
                markers.end = *end;
            } else {
                throw Failure{"A selection needs both /*[*/ and /*]*/"};
            }
        }
        writeFile(workspaceDirectory + "/" + file, text);
    }
    if (!markers.found) {
        throw Failure{"No /*|*/ cursor or /*[*/ ... /*]*/ selection in the input"};
    }
    return markers;
}

void writeCompileCommands(const std::string &workspaceDirectory) {
    llvm::json::Array commands;
    for (const auto &file : listFiles(workspaceDirectory)) {
        if (llvm::StringRef(file).ends_with(".cpp")) {
            commands.push_back(llvm::json::Object{
                {"directory", workspaceDirectory},
                {"arguments", llvm::json::Array{RIKTIG_TEST_CXX, "-std=c++20", "-c", file, "-o", "/dev/null"}},
                {"file", file}});
        }
    }
    std::string text;
    llvm::raw_string_ostream out(text);
    out << llvm::formatv("{0:2}", llvm::json::Value(std::move(commands)));
    writeFile(workspaceDirectory + "/compile_commands.json", out.str());
}

struct ProgramOutcome {
    bool compiled = false;
    bool passed = false;
    std::string log;
};

ProgramOutcome buildAndRun(const std::string &workspaceDirectory) {
    ProgramOutcome outcome;
    std::string executable = workspaceDirectory + ".program";
    std::string logFile = workspaceDirectory + ".log";
    std::vector<std::string> arguments{RIKTIG_TEST_CXX, "-std=c++20", "-w", "-o", executable};
    for (const auto &file : listFiles(workspaceDirectory)) {
        if (llvm::StringRef(file).ends_with(".cpp")) {
            arguments.push_back(workspaceDirectory + "/" + file);
        }
    }
    std::vector<llvm::StringRef> argumentRefs(arguments.begin(), arguments.end());
    std::optional<llvm::StringRef> redirects[] = {std::nullopt, llvm::StringRef(logFile), llvm::StringRef(logFile)};
    int compileStatus = llvm::sys::ExecuteAndWait(RIKTIG_TEST_CXX, argumentRefs, std::nullopt, redirects);
    outcome.log = readFile(logFile);
    if (compileStatus != 0) {
        return outcome;
    }
    outcome.compiled = true;
    int runStatus = llvm::sys::ExecuteAndWait(executable, {executable}, std::nullopt, redirects, /*SecondsToWait=*/10);
    outcome.log += readFile(logFile);
    outcome.passed = runStatus == 0;
    return outcome;
}

RefactoringResult runRename(const Workspace &workspace, const CaseDefinition &definition, const Markers &markers,
                            const std::string &workspaceDirectory, bool naive) {
    auto newName = definition.options.getString("newName");
    if (!newName) {
        throw Failure{"A rename case needs options.newName"};
    }
    auto text = readFile(workspaceDirectory + "/" + markers.file);
    RenameRequest request{markers.file, positionOfOffset(text, markers.start), newName->str()};
    return naive ? naiveRename(workspace, request) : rename(workspace, request);
}

RefactoringResult runRefactoring(const CaseDefinition &definition, const Markers &markers,
                                 const std::string &workspaceDirectory, bool naive) {
    auto workspace = Workspace::load(workspaceDirectory);
    if (!workspace) {
        throw Failure{llvm::toString(workspace.takeError())};
    }
    if (definition.refactoring == "rename") {
        return runRename(*workspace, definition, markers, workspaceDirectory, naive);
    }
    if (definition.refactoring == "extract-iife") {
        auto text = readFile(workspaceDirectory + "/" + markers.file);
        ExtractIifeRequest request{markers.file, positionOfOffset(text, markers.start),
                                   positionOfOffset(text, markers.end)};
        return naive ? naiveExtractIife(*workspace, request) : extractIife(*workspace, request);
    }
    auto text = readFile(workspaceDirectory + "/" + markers.file);
    auto cursor = positionOfOffset(text, markers.start);
    if (definition.refactoring == "make-method-static") {
        MakeMethodStaticRequest request{markers.file, cursor};
        if (auto receiverName = definition.options.getString("receiverName")) {
            request.receiverName = receiverName->str();
        }
        if (auto pass = definition.options.getString("pass")) {
            request.pass = pass->str();
        }
        return naive ? naiveMakeMethodStatic(*workspace, request) : makeMethodStatic(*workspace, request);
    }
    throw Failure{"Unknown refactoring " + definition.refactoring};
}

void applyResult(const RefactoringResult &result, const std::string &workspaceDirectory) {
    std::map<std::string, EditMap> byFile;
    for (const auto &edit : result.edits) {
        auto file = workspaceDirectory + "/" + edit.file;
        if (!byFile[file].add(file, Replacement{edit.startOffset, edit.endOffset - edit.startOffset, edit.newText})) {
            throw Failure{"The result has overlapping edits in " + edit.file};
        }
    }
    for (const auto &[file, edits] : byFile) {
        writeFile(file, edits.apply(file, readFile(file)));
    }
}

std::string describeRefusal(const RefactoringResult &result) {
    std::string text;
    for (const auto &reason : result.reasons) {
        text += reason.code + ": " + reason.message;
        if (reason.location) {
            text += " (" + reason.location->file + ":" + std::to_string(reason.location->start.line) + ":" +
                    std::to_string(reason.location->start.column) + ")";
        }
    }
    return text;
}

void compareWithExpected(const std::string &outputDirectory, const std::string &expectedDirectory) {
    std::vector<std::string> outputFiles;
    for (const auto &file : listFiles(outputDirectory)) {
        if (isSourceFile(file)) {
            outputFiles.push_back(file);
        }
    }
    if (std::getenv("RIKTIG_WRITE_EXPECTED")) {
        fs::remove_directories(expectedDirectory);
        for (const auto &file : outputFiles) {
            writeFile(expectedDirectory + "/" + file, readFile(outputDirectory + "/" + file));
        }
        return;
    }
    if (!fs::is_directory(expectedDirectory)) {
        throw Failure{"No expected/ directory; run with RIKTIG_WRITE_EXPECTED=1 and review the output"};
    }
    if (listFiles(expectedDirectory) != outputFiles) {
        throw Failure{"expected/ does not hold the same files as the output"};
    }
    for (const auto &file : outputFiles) {
        auto actual = readFile(outputDirectory + "/" + file);
        if (actual != readFile(expectedDirectory + "/" + file)) {
            throw Failure{"Output differs from expected/" + file + ". Actual output:\n" + actual};
        }
    }
}

void runCase(const std::string &caseDirectory, const std::string &temporaryDirectory) {
    auto definition = readCase(caseDirectory);
    auto workspaceDirectory = temporaryDirectory + "/workspace";
    auto markers = copyInputWithoutMarkers(caseDirectory + "/input", workspaceDirectory);
    writeCompileCommands(workspaceDirectory);

    auto input = buildAndRun(workspaceDirectory);
    if (!input.passed) {
        throw Failure{"The input program does not pass its own assertion, so the test is wrong:\n" + input.log};
    }

    auto naiveDirectory = temporaryDirectory + "/naive";
    copyInputWithoutMarkers(caseDirectory + "/input", naiveDirectory);
    writeCompileCommands(naiveDirectory);

    auto result = runRefactoring(definition, markers, workspaceDirectory, /*naive=*/false);
    if (definition.refuse) {
        if (result.succeeded) {
            throw Failure{"Expected refusal " + *definition.refuse + ", but the refactoring succeeded"};
        }
        if (result.reasons.empty() || result.reasons.front().code != *definition.refuse) {
            throw Failure{"Expected refusal " + *definition.refuse + ", but got " + describeRefusal(result)};
        }
        if (fs::exists(caseDirectory + "/expected")) {
            throw Failure{"A case that expects a refusal has no expected/ directory"};
        }
    } else {
        if (!result.succeeded) {
            throw Failure{"Expected success, but the refactoring refused: " + describeRefusal(result)};
        }
        applyResult(result, workspaceDirectory);
        compareWithExpected(workspaceDirectory, caseDirectory + "/expected");
        auto output = buildAndRun(workspaceDirectory);
        if (!output.passed) {
            throw Failure{"CORRECTNESS BUG: the refactored program does not pass its assertion:\n" + output.log};
        }
    }

    if (definition.naiveShouldFail) {
        auto naiveResult = runRefactoring(definition, markers, naiveDirectory, /*naive=*/true);
        if (!naiveResult.succeeded) {
            throw Failure{"The naive implementation could not run, so the case does not show that its assertion "
                          "detects the problem: " +
                          describeRefusal(naiveResult)};
        }
        applyResult(naiveResult, naiveDirectory);
        auto naive = buildAndRun(naiveDirectory);
        if (naive.passed) {
            throw Failure{"The naive implementation passes this edge case, so its assertion cannot detect the "
                          "problem"};
        }
    }
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        llvm::errs() << "Usage: riktig-cpp-case-runner <case directory>\n";
        return 2;
    }
    llvm::SmallString<256> caseDirectory(argv[1]);
    fs::make_absolute(caseDirectory);
    path::remove_dots(caseDirectory, true);
    llvm::SmallString<256> temporaryDirectory;
    if (fs::createUniqueDirectory("riktig-case", temporaryDirectory)) {
        llvm::errs() << "Cannot create a temporary directory\n";
        return 2;
    }
    int status = 0;
    try {
        runCase(std::string(caseDirectory), std::string(temporaryDirectory));
        llvm::outs() << "PASS " << caseDirectory << "\n";
    } catch (const Failure &failure) {
        llvm::errs() << "FAIL " << caseDirectory << "\n" << failure.message << "\n";
        status = 1;
    }
    if (std::getenv("RIKTIG_KEEP_TEMP")) {
        llvm::outs() << "Kept " << temporaryDirectory << "\n";
    } else {
        fs::remove_directories(temporaryDirectory);
    }
    return status;
}
