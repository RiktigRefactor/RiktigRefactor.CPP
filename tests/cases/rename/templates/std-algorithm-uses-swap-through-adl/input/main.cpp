#include <algorithm>

namespace shop {
struct Item {
    int id;
};
int swaps = 0;
void /*|*/swap(Item &left, Item &right) {
    ++swaps;
    int id = left.id;
    left.id = right.id;
    right.id = id;
}
} // namespace shop

int main() {
    shop::Item items[2] = {{2}, {1}};
    std::iter_swap(items, items + 1);
    return shop::swaps == 1 && items[0].id == 1 ? 0 : 1;
}
