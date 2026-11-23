int pick(int) { return 1; }
int pick(const char *) { return 2; }

int main() { return pick(5) + pick("x") == 3 ? 0 : 1; }
