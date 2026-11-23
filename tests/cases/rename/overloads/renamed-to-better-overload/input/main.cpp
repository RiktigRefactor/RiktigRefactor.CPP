int pick(int) { return 1; }
int /*|*/choose(long) { return 2; }

int main() { return choose(5) == 2 ? 0 : 1; }
