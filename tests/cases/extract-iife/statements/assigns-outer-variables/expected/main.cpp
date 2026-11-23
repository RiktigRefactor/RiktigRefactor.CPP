int main() {
    int a = 20;
    int b = 0;
    [](int &a, int &b) {
        a++;
        b = a * 2;
    }(a, b);
    return a == 21 && b == 42 ? 0 : 1;
}
