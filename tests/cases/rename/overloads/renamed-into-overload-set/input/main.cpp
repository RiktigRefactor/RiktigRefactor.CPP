int pick(int) { return 1; }
int /*|*/choose(const char *) { return 2; }

int main() { return pick(5) + choose("x") == 3 ? 0 : 1; }
