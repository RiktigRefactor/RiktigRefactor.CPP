extern "C" int /*|*/calculate(int value) { return value * 2; }

int main() { return calculate(21) == 42 ? 0 : 1; }
