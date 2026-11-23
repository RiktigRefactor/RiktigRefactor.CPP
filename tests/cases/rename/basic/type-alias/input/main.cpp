using /*|*/Length = int;

Length twice(Length value) { return value * 2; }

int main() {
    Length length = 21;
    return twice(length) == 42 ? 0 : 1;
}
