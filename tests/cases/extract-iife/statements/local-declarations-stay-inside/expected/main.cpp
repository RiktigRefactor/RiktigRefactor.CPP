int main() {
    int result = 0;
    [](int &result) {
        int doubled = 21 * 2;
        // the answer
        result = doubled;
    }(result);
    return result == 42 ? 0 : 1;
}
