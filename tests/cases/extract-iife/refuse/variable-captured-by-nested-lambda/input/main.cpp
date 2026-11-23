int main() {
    int base = 40;
    auto add = /*[*/[&base](int value) { return base + value; }/*]*/;
    base = 41;
    return add(1) == 42 ? 0 : 1;
}
