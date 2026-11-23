int main() {
    int count = 2;
    int sum = /*[*/decltype(count){5} + count/*]*/;
    return sum == 7 ? 0 : 1;
}
