int first();
static int /*|*/local() { return 2; }

int main() { return first() + local() == 3 ? 0 : 1; }
