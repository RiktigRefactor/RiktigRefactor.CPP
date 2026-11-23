struct Wallet {
    int /*|*/value() const { return 42; }
};

template <class T> int read(const T &holder) { return holder.value(); }

int main() { return read(Wallet{}) == 42 ? 0 : 1; }
