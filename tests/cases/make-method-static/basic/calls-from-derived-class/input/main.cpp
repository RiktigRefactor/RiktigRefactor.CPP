struct Base {
    int base = 4;
    int /*|*/scaled(int factor) const { return base * factor; }
};

struct Derived : Base {
    int twice() const { return Base::scaled(2); }
};

int main() {
    Derived derived;
    return derived.scaled(3) == 12 && derived.twice() == 8 ? 0 : 1;
}
