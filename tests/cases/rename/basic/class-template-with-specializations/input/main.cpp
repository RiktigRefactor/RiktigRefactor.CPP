template <class T> struct /*|*/Box {
    Box(T value) : value(value) {}
    T value;
    int kind() const { return 1; }
};

template <class T> struct Box<T *> {
    Box(T *value) : value(value) {}
    T *value;
    int kind() const { return 2; }
};

template <> struct Box<bool> {
    Box(bool value) : value(value) {}
    bool value;
    int kind() const { return 3; }
};

Box(const char *) -> Box<const char *>;

int main() {
    Box<int> number(1);
    int x = 0;
    Box pointer(&x);
    Box<bool> flag(true);
    Box text("hi");
    return number.kind() + pointer.kind() + flag.kind() + text.kind() == 8 ? 0 : 1;
}
