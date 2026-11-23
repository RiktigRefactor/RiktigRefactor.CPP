int picked = 0;

struct Order {
    static int /*|*/total() { return 3; }
};

struct Pricing {};

Order pick() {
    ++picked;
    return Order();
}

int main() { return pick().total() == 3 && picked == 1 ? 0 : 1; }
