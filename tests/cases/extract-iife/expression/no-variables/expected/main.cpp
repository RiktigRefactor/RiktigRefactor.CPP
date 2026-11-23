int main() {
    int answer = []() { return 40 + 2; }();
    return answer == 42 ? 0 : 1;
}
