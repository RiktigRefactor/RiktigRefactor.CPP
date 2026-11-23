#define TWICE(x) ((x) * 2)

int main() {
    int half = 21;
    int total = /*[*/TWICE(half)/*]*/;
    return total == 42 ? 0 : 1;
}
