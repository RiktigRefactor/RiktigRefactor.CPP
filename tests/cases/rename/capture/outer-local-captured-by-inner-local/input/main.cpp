int main() {
    int /*|*/outer = 1;
    {
        int inner = 2;
        if (outer + inner != 3) {
            return 1;
        }
    }
    return 0;
}
