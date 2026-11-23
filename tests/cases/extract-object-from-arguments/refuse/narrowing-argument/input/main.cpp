int /*|*/quote(int price, int qty) { return price * qty; }

int main() { return quote(2.5, 3) == 6 ? 0 : 1; }
