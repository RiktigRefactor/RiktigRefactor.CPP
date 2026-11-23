int main() {
    int base = 20;
    int total = [](int base) { return [sum = base + 1] { return sum * 2; }(); }(base);
    return total == 42 ? 0 : 1;
}
