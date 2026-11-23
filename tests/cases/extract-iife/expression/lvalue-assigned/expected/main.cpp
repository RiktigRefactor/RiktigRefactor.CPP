int main() {
    int values[3] = {1, 2, 3};
    int index = 1;
    [](int (&values)[3], int index) -> decltype(auto) { return (values[index]); }(values, index) = 42;
    return values[1] == 42 ? 0 : 1;
}
