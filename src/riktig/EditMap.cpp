#include "EditMap.h"

#include <algorithm>

namespace riktig {

bool EditMap::add(const std::string &file, Replacement replacement) {
    auto &replacements = byFile_[file];
    for (const auto &existing : replacements) {
        if (existing.offset == replacement.offset && existing.length == replacement.length &&
            existing.newText == replacement.newText) {
            return true;
        }
        bool disjoint = replacement.offset + replacement.length <= existing.offset ||
                        existing.offset + existing.length <= replacement.offset;
        bool bothInsertionsAtSameOffset =
            replacement.length == 0 && existing.length == 0 && replacement.offset == existing.offset;
        if (!disjoint || bothInsertionsAtSameOffset) {
            return false;
        }
    }
    replacements.push_back(std::move(replacement));
    std::sort(replacements.begin(), replacements.end(), [](const Replacement &left, const Replacement &right) {
        return std::pair(left.offset, left.length) < std::pair(right.offset, right.length);
    });
    return true;
}

unsigned EditMap::mapOffset(const std::string &file, unsigned offset) const {
    auto found = byFile_.find(file);
    if (found == byFile_.end()) {
        return offset;
    }
    long shift = 0;
    for (const auto &replacement : found->second) {
        if (replacement.offset > offset) {
            break;
        }
        if (replacement.offset == offset) {
            if (replacement.length == 0) {
                shift += static_cast<long>(replacement.newText.size());
                continue;
            }
            break;
        }
        shift += static_cast<long>(replacement.newText.size()) - static_cast<long>(replacement.length);
    }
    return static_cast<unsigned>(static_cast<long>(offset) + shift);
}

unsigned EditMap::unmapOffset(const std::string &file, unsigned editedOffset) const {
    auto found = byFile_.find(file);
    if (found == byFile_.end()) {
        return editedOffset;
    }
    long shift = 0;
    for (const auto &replacement : found->second) {
        long newStart = static_cast<long>(replacement.offset) + shift;
        long newEnd = newStart + static_cast<long>(replacement.newText.size());
        if (editedOffset < newStart) {
            break;
        }
        if (editedOffset < newEnd) {
            return replacement.offset;
        }
        shift += static_cast<long>(replacement.newText.size()) - static_cast<long>(replacement.length);
    }
    return static_cast<unsigned>(static_cast<long>(editedOffset) - shift);
}

std::string EditMap::apply(const std::string &file, const std::string &original) const {
    auto found = byFile_.find(file);
    if (found == byFile_.end()) {
        return original;
    }
    std::string result;
    unsigned copiedUpTo = 0;
    for (const auto &replacement : found->second) {
        result.append(original, copiedUpTo, replacement.offset - copiedUpTo);
        result.append(replacement.newText);
        copiedUpTo = replacement.offset + replacement.length;
    }
    result.append(original, copiedUpTo);
    return result;
}

} // namespace riktig
