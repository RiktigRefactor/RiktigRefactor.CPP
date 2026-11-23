int destroyed = 0;

struct Log {
    ~Log() { ++destroyed; }
};

int /*|*/run(Log log, int value) { return value; }

int main() {
    int result = run(Log(), 1);
    return result == 1 && destroyed >= 1 ? 0 : 1;
}
