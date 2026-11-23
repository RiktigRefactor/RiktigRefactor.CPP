#pragma once

#include "Bindings.h"

#include <map>
#include <string>
#include <vector>

namespace riktig {

struct Replacement {
    unsigned offset = 0;
    unsigned length = 0;
    std::string newText;
};

// Non-overlapping replacements per file (absolute path), and the mapping of original offsets to edited ones.
class EditMap {
  public:
    // Returns false when the replacement overlaps one already added.
    bool add(const std::string &file, Replacement replacement);

    // An offset at the start of a replaced range maps to the start of its new text.
    unsigned mapOffset(const std::string &file, unsigned offset) const;
    FileOffset map(const FileOffset &location) const {
        return {location.file, mapOffset(location.file, location.offset)};
    }
    // The original offset of an edited one; offsets inside new text map to the start of the replaced range.
    unsigned unmapOffset(const std::string &file, unsigned editedOffset) const;
    FileOffset unmap(const FileOffset &location) const {
        return {location.file, unmapOffset(location.file, location.offset)};
    }
    std::string apply(const std::string &file, const std::string &original) const;

    const std::map<std::string, std::vector<Replacement>> &replacements() const { return byFile_; }
    bool empty() const { return byFile_.empty(); }

  private:
    std::map<std::string, std::vector<Replacement>> byFile_;
};

} // namespace riktig
