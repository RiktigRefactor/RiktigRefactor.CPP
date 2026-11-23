struct Wallet {
    int /*|*/value() const { return 40; }
};

struct Coin {
    int value() const { return 2; }
};

template <class T> int read(const T &holder) { return holder.value(); }

int main() { return read(Wallet{}) + read(Coin{}) == 42 ? 0 : 1; }
