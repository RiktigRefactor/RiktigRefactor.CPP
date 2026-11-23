template <class T> struct Crate {
    Crate(T value) : value(value) {}
    T value;
    int kind() const { return 1; }
};

template <class T> struct Crate<T *> {
    Crate(T *value) : value(value) {}
    T *value;
    int kind() const { return 2; }
};

template <> struct Crate<bool> {
    Crate(bool value) : value(value) {}
    bool value;
    int kind() const { return 3; }
};

Crate(const char *) -> Crate<const char *>;

int main() {
    Crate<int> number(1);
    int x = 0;
    Crate pointer(&x);
    Crate<bool> flag(true);
    Crate text("hi");
    return number.kind() + pointer.kind() + flag.kind() + text.kind() == 8 ? 0 : 1;
}
