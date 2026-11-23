int main() {
    int count = 41;
    int result = [](int &count) -> decltype(auto) { return (++count); }(count);
    return result == 42 && count == 42 ? 0 : 1;
}
