int bump(int *target) { return ++*target; }

int main() {
    int count = 1;
    int *alias = &count;
    int result = /*[*/bump(alias) + count/*]*/;
    return result == 4 ? 0 : 1;
}
