#include "Verification.h"

namespace riktig {

DeclIdentity moved(const EditMap &edits, const DeclIdentity &identity) {
    if (identity.location.file.empty()) {
        return identity;
    }
    return DeclIdentity{edits.map(identity.location), identity.tag};
}

static std::string describe(const std::set<DeclIdentity> &identities) {
    std::string result = "{";
    for (const auto &identity : identities) {
        if (result.size() > 1) {
            result += ", ";
        }
        result += identity.describe();
    }
    return result + "}";
}

static std::optional<BindingDifference> compareTranslationUnit(const TranslationUnitBindings &before,
                                                               const TranslationUnitBindings &after,
                                                               const EditMap &edits,
                                                               const BindingExpectations &expectations) {
    auto mapLocation = [&](const FileOffset &location) {
        if (expectations.relocate) {
            if (auto relocated = expectations.relocate(location)) {
                return *relocated;
            }
        }
        return edits.map(location);
    };
    BindingTable expected;
    for (const auto &[key, identities] : before.bindings) {
        BindingKey movedKey{mapLocation(key.location), key.role};
        for (const auto &identity : identities) {
            if (expectations.isDropped && expectations.isDropped(key, identity)) {
                continue;
            }
            auto &expectedIdentities = expected[movedKey];
            std::optional<DeclIdentity> substitute;
            if (expectations.substitute) {
                substitute = expectations.substitute(key, identity);
            }
            if (!substitute && !identity.location.file.empty()) {
                substitute = DeclIdentity{mapLocation(identity.location), identity.tag};
            }
            expectedIdentities.insert(substitute ? *substitute : identity);
        }
    }
    for (const auto &[key, identities] : after.bindings) {
        auto found = expected.find(key);
        if (found == expected.end()) {
            if (expectations.isNew && expectations.isNew(key)) {
                continue;
            }
            if (expectations.isExpectedNew && expectations.isExpectedNew(key, identities)) {
                continue;
            }
            return BindingDifference{key.location, "a new " + key.role + " binding to " + describe(identities)};
        }
        if (found->second != identities) {
            return BindingDifference{key.location, "the " + key.role + " binding changes from " +
                                                       describe(found->second) + " to " + describe(identities)};
        }
        expected.erase(found);
    }
    if (!expected.empty()) {
        const auto &[key, identities] = *expected.begin();
        return BindingDifference{key.location,
                                 "the " + key.role + " binding to " + describe(identities) + " disappears"};
    }
    return std::nullopt;
}

std::optional<BindingDifference> compareBindings(const std::vector<TranslationUnitBindings> &before,
                                                 const std::vector<TranslationUnitBindings> &after,
                                                 const EditMap &edits, const BindingExpectations &expectations) {
    for (const auto &beforeUnit : before) {
        const TranslationUnitBindings *afterUnit = nullptr;
        for (const auto &candidate : after) {
            if (candidate.mainFile == beforeUnit.mainFile) {
                afterUnit = &candidate;
            }
        }
        if (!afterUnit) {
            return BindingDifference{FileOffset{beforeUnit.mainFile, 0}, "the translation unit was not parsed again"};
        }
        if (auto difference = compareTranslationUnit(beforeUnit, *afterUnit, edits, expectations)) {
            return difference;
        }
    }
    return std::nullopt;
}

} // namespace riktig
