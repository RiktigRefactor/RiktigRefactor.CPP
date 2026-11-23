#pragma once

#include "Bindings.h"
#include "EditMap.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace riktig {

struct BindingDifference {
    FileOffset location; // in the edited files
    std::string description;
};

struct BindingExpectations {
    // Maps a declaration bound before the edits to the one the same reference must bind to after them.
    // By default, the same declaration at its moved location.
    std::function<std::optional<DeclIdentity>(const BindingKey &beforeKey, const DeclIdentity &before)> substitute;
    // Bindings after the edits that have no counterpart before, such as those in inserted text.
    std::function<bool(const BindingKey &afterKey)> isNew;
};

// The re-binding check: every binding recorded before the edits must be recorded after them, at the moved
// location, binding the same declarations, and no other binding may appear.
std::optional<BindingDifference> compareBindings(const std::vector<TranslationUnitBindings> &before,
                                                 const std::vector<TranslationUnitBindings> &after,
                                                 const EditMap &edits, const BindingExpectations &expectations);

DeclIdentity moved(const EditMap &edits, const DeclIdentity &identity);

} // namespace riktig
