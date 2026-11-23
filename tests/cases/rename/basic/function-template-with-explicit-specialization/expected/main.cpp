template <class T> int describe(T) { return 1; }

template <> int describe<double>(double) { return 2; }

int main() { return describe(1) + describe(1.0) + describe<double>(3) == 5 ? 0 : 1; }
