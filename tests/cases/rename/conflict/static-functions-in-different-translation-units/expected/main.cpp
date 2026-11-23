int first();
static int helper() { return 2; }

int main() { return first() + helper() == 3 ? 0 : 1; }
