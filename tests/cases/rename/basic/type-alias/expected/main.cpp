using Meters = int;

Meters twice(Meters value) { return value * 2; }

int main() {
    Meters length = 21;
    return twice(length) == 42 ? 0 : 1;
}
