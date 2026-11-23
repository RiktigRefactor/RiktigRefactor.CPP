int main() {
    int offset = 2;
    auto add = [=](int /*|*/value) { return value + offset; };
    return add(40) == 42 ? 0 : 1;
}
