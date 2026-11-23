int /*|*/quote(int price, int qty) { return price * qty; }

int main() {
    int (*compute)(int, int) = &quote;
    return compute(2, 3) == 6 ? 0 : 1;
}
