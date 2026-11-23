struct Animal {
    virtual int speak() const { return 0; }
    virtual ~Animal() = default;
};

struct Dog : Animal {
    int speak() const override { return 1; }
};

struct Puppy : Dog {
    int speak() const override { return 2; }
};

int main() {
    Puppy puppy;
    const Animal &animal = puppy;
    Dog dog;
    return animal.speak() + dog.speak() + Animal{}.speak() == 3 ? 0 : 1;
}
