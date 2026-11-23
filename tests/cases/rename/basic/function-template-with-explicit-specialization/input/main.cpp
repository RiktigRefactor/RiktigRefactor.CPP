template <class T> int /*|*/name(T) { return 1; }

template <> int name<double>(double) { return 2; }

int main() { return name(1) + name(1.0) + name<double>(3) == 5 ? 0 : 1; }
