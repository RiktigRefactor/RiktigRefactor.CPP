template <typename Element> struct Pair {
    Element first;
    Element second;
    Element sum() const { return first + second; }
};

int main() { return Pair<int>{40, 2}.sum() == 42 ? 0 : 1; }
