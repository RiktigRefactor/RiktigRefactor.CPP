struct Counter {
    int count = 0;
    int /*|*/bump() { return ++count; }
};

int main() { return Counter().bump() == 1 ? 0 : 1; }
