struct Price {
    int cents = 1;
};

int /*|*/quote(int price) { return price; }

int main() { return quote(2) == 2 && Price().cents == 1 ? 0 : 1; }
