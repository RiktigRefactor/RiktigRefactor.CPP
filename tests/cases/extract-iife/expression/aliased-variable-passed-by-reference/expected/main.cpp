int bump(int *target) { return ++*target; }

int main() {
    int count = 1;
    int *alias = &count;
    int result = [](int *alias, int &count) { return bump(alias) + count; }(alias, count);
    return result == 4 ? 0 : 1;
}
