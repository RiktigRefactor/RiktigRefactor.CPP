struct Counter {
    int count = 0;
    void /*|*/bumpTwice() {
        auto bump = [=]() { ++count; };
        bump();
        bump();
    }
};

int main() {
    Counter counter;
    counter.bumpTwice();
    return counter.count == 2 ? 0 : 1;
}
