int trace[4];
int traced = 0;

int step(int value) {
    trace[traced++] = value;
    return value;
}

int main() {
    int first = step(1);
    int sum = first + []() { return (step(2), step(3)); }() + step(4);
    return sum == 8 && traced == 4 && trace[1] == 2 && trace[2] == 3 && trace[3] == 4 ? 0 : 1;
}
