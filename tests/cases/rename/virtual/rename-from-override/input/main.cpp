struct Animal {
    virtual int sound() const { return 0; }
    virtual ~Animal() = default;
};

struct Dog : Animal {
    int /*|*/sound() const override { return 1; }
};

struct Puppy : Dog {
    int sound() const override { return 2; }
};

int main() {
    Puppy puppy;
    const Animal &animal = puppy;
    Dog dog;
    return animal.sound() + dog.sound() + Animal{}.sound() == 3 ? 0 : 1;
}
