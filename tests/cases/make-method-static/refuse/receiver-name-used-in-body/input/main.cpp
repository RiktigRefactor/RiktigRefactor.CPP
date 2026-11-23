struct Order {
    int price = 2;
    int /*|*/total() const {
        int self = price;
        return self;
    }
};

int main() { return Order().total() == 2 ? 0 : 1; }
