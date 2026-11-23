namespace lib {
struct Token {};
int describe(Token) { return 1; }
} // namespace lib

template <class T> int /*|*/show(T) { return 2; }

int main() { return show(lib::Token{}) == 2 ? 0 : 1; }
