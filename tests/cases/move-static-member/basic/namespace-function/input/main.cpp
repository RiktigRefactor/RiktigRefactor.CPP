namespace util {
struct Amount {
    int cents = 0;
};

int /*|*/twice(Amount amount) { return amount.cents * 2; }
} // namespace util

namespace shop {
int unrelated() { return 0; }
} // namespace shop

namespace util {
int quadruple(Amount amount) { return twice(amount) * 2; }
} // namespace util

int main() {
    util::Amount amount{3};
    return util::twice(amount) == 6 && twice(amount) == 6 && util::quadruple(amount) == 12 ? 0 : 1;
}
