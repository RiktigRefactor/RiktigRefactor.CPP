int level = 1;

int main() {
    int /*|*/level = 2;
    level += 1;
    return level == 3 && ::level == 1 ? 0 : 1;
}
