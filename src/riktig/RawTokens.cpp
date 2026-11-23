#include "RawTokens.h"

#include "clang/Basic/CharInfo.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Lex/Lexer.h"

namespace riktig {

static clang::LangOptions cppLanguageOptions() {
    clang::LangOptions options;
    options.CPlusPlus = options.CPlusPlus11 = options.CPlusPlus14 = options.CPlusPlus17 = options.CPlusPlus20 = 1;
    options.LineComment = 1;
    options.Bool = 1;
    options.CXXOperatorNames = 1;
    options.Digraphs = 1;
    options.WChar = 1;
    options.Char8 = 1;
    options.Coroutines = 1;
    return options;
}

void forEachRawIdentifier(llvm::StringRef unterminatedSource, const std::function<void(const RawIdentifier &)> &visit) {
    // The lexer reads up to a terminating null character.
    std::string terminated = unterminatedSource.str();
    llvm::StringRef source(terminated.c_str(), terminated.size());
    auto options = cppLanguageOptions();
    // A valid file location whose raw encoding is 1, so each token's offset is its encoding minus 1.
    auto base = clang::SourceLocation::getFromRawEncoding(1);
    clang::Lexer lexer(base, options, source.begin(), source.begin(), source.end());
    lexer.SetCommentRetentionState(false);
    clang::Token token;
    bool onDirectiveLine = false;
    bool expectDirectiveName = false;
    llvm::StringRef directive;
    while (true) {
        lexer.LexFromRawLexer(token);
        if (token.is(clang::tok::eof)) {
            break;
        }
        unsigned offset = token.getLocation().getRawEncoding() - 1;
        if (token.isAtStartOfLine()) {
            onDirectiveLine = token.is(clang::tok::hash);
            expectDirectiveName = onDirectiveLine;
            directive = {};
            if (onDirectiveLine) {
                continue;
            }
        }
        if (token.is(clang::tok::raw_identifier)) {
            auto text = source.substr(offset, token.getLength());
            if (expectDirectiveName) {
                directive = text;
                expectDirectiveName = false;
                continue;
            }
            visit(RawIdentifier{offset, text, onDirectiveLine ? directive : llvm::StringRef()});
        }
        expectDirectiveName = false;
    }
}

static bool isIdentifierCharacter(char character) {
    return clang::isAsciiIdentifierContinue(character) || static_cast<unsigned char>(character) >= 0x80;
}

std::optional<std::string> identifierAt(llvm::StringRef source, unsigned offset) {
    if (offset >= source.size() || !clang::isAsciiIdentifierStart(source[offset])) {
        return std::nullopt;
    }
    unsigned end = offset;
    while (end < source.size() && isIdentifierCharacter(source[end])) {
        ++end;
    }
    return source.substr(offset, end - offset).str();
}

std::optional<unsigned> identifierStartAround(llvm::StringRef source, unsigned cursorOffset) {
    unsigned offset = cursorOffset;
    if (offset < source.size() && source[offset] == '~') {
        ++offset;
    }
    if (!(offset < source.size() && isIdentifierCharacter(source[offset]))) {
        if (offset > 0 && isIdentifierCharacter(source[offset - 1])) {
            --offset;
        } else {
            return std::nullopt;
        }
    }
    while (offset > 0 && isIdentifierCharacter(source[offset - 1])) {
        --offset;
    }
    if (!clang::isAsciiIdentifierStart(source[offset])) {
        return std::nullopt;
    }
    return offset;
}

bool hasLiteralSpanningLines(llvm::StringRef unterminatedSource) {
    std::string terminated = unterminatedSource.str();
    llvm::StringRef source(terminated.c_str(), terminated.size());
    auto options = cppLanguageOptions();
    auto base = clang::SourceLocation::getFromRawEncoding(1);
    clang::Lexer lexer(base, options, source.begin(), source.begin(), source.end());
    clang::Token token;
    while (true) {
        lexer.LexFromRawLexer(token);
        if (token.is(clang::tok::eof)) {
            return false;
        }
        if (clang::tok::isStringLiteral(token.getKind()) || token.is(clang::tok::char_constant) ||
            token.is(clang::tok::unknown)) {
            unsigned offset = token.getLocation().getRawEncoding() - 1;
            if (source.substr(offset, token.getLength()).contains('\n')) {
                return true;
            }
        }
    }
}

bool hasPreprocessorDirective(llvm::StringRef source) {
    llvm::SmallVector<llvm::StringRef> lines;
    source.split(lines, '\n');
    return llvm::any_of(lines, [](llvm::StringRef line) { return line.ltrim().starts_with("#"); });
}

bool isCppKeyword(llvm::StringRef name) {
    auto options = cppLanguageOptions();
    clang::IdentifierTable table(options);
    auto &info = table.get(name);
    return info.getTokenID() != clang::tok::identifier || info.isCPlusPlusOperatorKeyword();
}

} // namespace riktig
