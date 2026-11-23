int destroyed = 0;

struct Tracer {
    ~Tracer() { ++destroyed; }
    int value() const { return 1; }
};

int main() {
    int seen = (/*[*/Tracer{}.value()/*]*/, destroyed);
    return seen == 0 ? 0 : 1;
}
