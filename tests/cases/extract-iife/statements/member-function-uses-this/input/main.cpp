struct Counter {
    int count = 0;
    void bump() { ++count; }
    void twice() {
        /*[*/bump();
        bump();/*]*/
    }
};

int main() {
    Counter counter;
    counter.twice();
    return counter.count == 2 ? 0 : 1;
}
