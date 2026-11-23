template <typename /*|*/T> struct Pair {
    T first;
    T second;
    T sum() const { return first + second; }
};

int main() { return Pair<int>{40, 2}.sum() == 42 ? 0 : 1; }
