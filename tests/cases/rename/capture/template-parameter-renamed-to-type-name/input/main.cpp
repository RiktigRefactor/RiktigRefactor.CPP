struct Item {
    static int size() { return 7; }
};

template <class /*|*/T> int measure(T value) { return Item::size() + value; }

int main() { return measure(1) == 8 ? 0 : 1; }
