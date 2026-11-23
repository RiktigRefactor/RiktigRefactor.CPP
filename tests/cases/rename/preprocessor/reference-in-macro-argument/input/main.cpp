#define TWICE(x) ((x) * 2)

int main() {
    int /*|*/count = 21;
    return TWICE(count) == 42 ? 0 : 1;
}
