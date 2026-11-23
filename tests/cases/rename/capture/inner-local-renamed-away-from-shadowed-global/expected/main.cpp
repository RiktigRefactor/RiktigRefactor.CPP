int level = 1;

int main() {
    int depth = 2;
    depth += 1;
    return depth == 3 && ::level == 1 ? 0 : 1;
}
