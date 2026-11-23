int main() {
    int sum = 0;
    [](int &sum) {
        for (int i = 0; i < 100; ++i) {
            if (i % 2 == 0) {
                continue;
            }
            if (i > 9) {
                break;
            }
            sum += i;
        }
    }(sum);
    return sum == 25 ? 0 : 1;
}
