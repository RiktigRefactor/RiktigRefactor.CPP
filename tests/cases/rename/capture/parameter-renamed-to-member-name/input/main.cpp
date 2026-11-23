struct Offset {
    int base = 40;
    int apply(int /*|*/amount) const { return base + amount; }
};

int main() { return Offset{}.apply(2) == 42 ? 0 : 1; }
