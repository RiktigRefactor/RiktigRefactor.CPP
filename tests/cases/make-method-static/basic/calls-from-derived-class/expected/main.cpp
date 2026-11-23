struct Base {
    int base = 4;
    static int scaled(const Base &self, int factor) { return self.base * factor; }
};

struct Derived : Base {
    int twice() const { return Base::scaled(*this, 2); }
};

int main() {
    Derived derived;
    return Base::scaled(derived, 3) == 12 && derived.twice() == 8 ? 0 : 1;
}
