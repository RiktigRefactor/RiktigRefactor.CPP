int main() {
    int base = 20;
    int total = /*[*/[sum = base + 1] { return sum * 2; }()/*]*/;
    return total == 42 ? 0 : 1;
}
