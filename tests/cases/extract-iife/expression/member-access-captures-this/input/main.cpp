struct Account {
    int balance = 40;
    int bonus() const { return 2; }
    int total() const { return /*[*/balance + bonus()/*]*/; }
};

int main() { return Account{}.total() == 42 ? 0 : 1; }
