int main() {
    int /*|*/delta = 2;
    auto add = [delta](int value) { return value + delta; };
    auto addByReference = [&delta](int value) { return value + delta; };
    return add(40) + addByReference(0) == 44 ? 0 : 1;
}
