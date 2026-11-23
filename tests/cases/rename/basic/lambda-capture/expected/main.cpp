int main() {
    int step = 2;
    auto add = [step](int value) { return value + step; };
    auto addByReference = [&step](int value) { return value + step; };
    return add(40) + addByReference(0) == 44 ? 0 : 1;
}
