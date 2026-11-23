int /*|*/quote(int price, int code, int qty) { return price * qty + code; }

int main() { return quote(2, 1, 3) == 7 ? 0 : 1; }
