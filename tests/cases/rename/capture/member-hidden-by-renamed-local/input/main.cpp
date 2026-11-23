struct Scale {
    int factor = 3;
    int times(int value) const {
        int /*|*/offset = 1;
        return value * factor + offset;
    }
};

int main() { return Scale{}.times(2) == 7 ? 0 : 1; }
