int main() {
    int count = 41;
    int result = /*[*/++count/*]*/;
    return result == 42 && count == 42 ? 0 : 1;
}
