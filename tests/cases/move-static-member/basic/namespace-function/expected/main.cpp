namespace util {
struct Amount {
    int cents = 0;
};

} // namespace util

namespace shop {
int unrelated() { return 0; }
int twice(util::Amount amount) { return amount.cents * 2; }
} // namespace shop

namespace util {
int quadruple(Amount amount) { return shop::twice(amount) * 2; }
} // namespace util

int main() {
    util::Amount amount{3};
    return shop::twice(amount) == 6 && shop::twice(amount) == 6 && util::quadruple(amount) == 12 ? 0 : 1;
}
