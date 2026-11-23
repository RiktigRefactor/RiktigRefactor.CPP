struct Wallet {
    int amount() const { return 42; }
};

template <class T> int read(const T &holder) { return holder.amount(); }

int main() { return read(Wallet{}) == 42 ? 0 : 1; }
