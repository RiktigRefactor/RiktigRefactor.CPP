struct Shape {
    virtual int /*|*/sides() const { return 0; }
    virtual ~Shape() = default;
};

struct Square : Shape {
    int sides() const override { return 4; }
};

int main() {
    Square square;
    const Shape &shape = square;
    return shape.sides() == 4 ? 0 : 1;
}
