#include "NameChecks.h"

#include "RawTokens.h"
#include "RefusalCodes.h"
#include "Results.h"

#include "clang/Basic/CharInfo.h"
#include "llvm/ADT/STLExtras.h"

namespace riktig {

namespace codes = refusal_codes;

bool isReservedIdentifier(llvm::StringRef name) {
    return name.contains("__") || (name.size() >= 2 && name[0] == '_' && clang::isUppercase(name[1]));
}

bool isValidIdentifier(llvm::StringRef name) {
    if (name.empty() || !clang::isAsciiIdentifierStart(name.front())) {
        return false;
    }
    return llvm::all_of(name, [](char character) { return clang::isAsciiIdentifierContinue(character); });
}

std::optional<RefusalReason> validateNewName(const std::string &oldName, const std::string &newName) {
    if (!isValidIdentifier(newName)) {
        return makeRefusal(codes::InvalidIdentifier, "`" + newName + "` is not a valid C++ identifier.");
    }
    if (isCppKeyword(newName)) {
        return makeRefusal(codes::InvalidIdentifier, "`" + newName + "` is a C++ keyword.");
    }
    if (isReservedIdentifier(newName)) {
        return makeRefusal(codes::InvalidIdentifier,
                           "`" + newName +
                               "` is reserved for the implementation (it contains `__` or starts with "
                               "`_` and an uppercase letter).");
    }
    if (newName == oldName) {
        return makeRefusal(codes::InvalidIdentifier, "The new name is the same as the old name.");
    }
    return std::nullopt;
}

std::optional<RefusalReason> checkEveryMentionIsUnderstood(const Workspace &workspace,
                                                           const std::vector<TranslationUnitBindings> &units,
                                                           const std::set<std::string> &names,
                                                           const FileOverlay &overlay) {
    std::set<std::string> files;
    for (const auto &unit : units) {
        files.insert(unit.workspaceFiles.begin(), unit.workspaceFiles.end());
    }
    for (const auto &file : files) {
        auto text = workspace.read(file, overlay);
        if (!text) {
            llvm::consumeError(text.takeError());
            continue;
        }
        std::optional<RefusalReason> refusal;
        forEachRawIdentifier(*text, [&](const RawIdentifier &identifier) {
            if (refusal || !names.count(identifier.text.str())) {
                return;
            }
            FileOffset location{file, identifier.offset};
            auto range = rangeOf(workspace, location, identifier.text.size(), overlay);
            if (identifier.directive == "include" || identifier.directive == "include_next" ||
                identifier.directive == "import") {
                return;
            }
            for (const auto &unit : units) {
                if (auto skipped = unit.skippedRanges.find(file); skipped != unit.skippedRanges.end()) {
                    for (const auto &[begin, end] : skipped->second) {
                        if (begin <= identifier.offset && identifier.offset < end) {
                            refusal = makeRefusal(codes::PreprocessorRegion,
                                                  "`" + identifier.text.str() +
                                                      "` appears in a region that conditional compilation skips, "
                                                      "where the tool cannot tell what it refers to.",
                                                  range);
                            return;
                        }
                    }
                }
            }
            if (!identifier.directive.empty()) {
                refusal = makeRefusal(codes::MacroInvolved,
                                      "`" + identifier.text.str() + "` appears in a `#" + identifier.directive.str() +
                                          "` directive.",
                                      range);
                return;
            }
            bool understood = false;
            for (const auto &unit : units) {
                understood =
                    understood || unit.namedAtToken.count(location) || unit.namedAtTokenInMacro.count(location);
            }
            for (const auto &unit : units) {
                if (understood) {
                    break;
                }
                if (auto expansions = unit.macroExpansionRanges.find(file);
                    expansions != unit.macroExpansionRanges.end()) {
                    for (const auto &[begin, end] : expansions->second) {
                        if (begin <= identifier.offset && identifier.offset < end) {
                            refusal = makeRefusal(codes::MacroInvolved,
                                                  "`" + identifier.text.str() +
                                                      "` is an argument of a macro, which may stringize it or "
                                                      "paste it into another token.",
                                                  range);
                            return;
                        }
                    }
                }
            }
            if (!understood) {
                refusal = makeRefusal(codes::UnsupportedReference,
                                      "The tool does not know what `" + identifier.text.str() +
                                          "` refers to here, for example because it is in a template that is never "
                                          "instantiated.",
                                      range);
            }
        });
        if (refusal) {
            return refusal;
        }
    }
    return std::nullopt;
}
} // namespace riktig
