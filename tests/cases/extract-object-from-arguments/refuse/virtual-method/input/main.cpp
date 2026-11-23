struct Shop {
    virtual int /*|*/quote(int price) const { return price; }
    virtual ~Shop() = default;
};

int main() { return Shop().quote(2) == 2 ? 0 : 1; }
