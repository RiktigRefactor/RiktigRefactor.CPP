int trace[2];
int traced = 0;

int step(int value) {
    trace[traced++] = value;
    return value;
}

int /*|*/quote(int price, int qty) { return price * qty; }

int main() { return quote(step(2), step(3)) == 6 && trace[0] == 2 ? 0 : 1; }
