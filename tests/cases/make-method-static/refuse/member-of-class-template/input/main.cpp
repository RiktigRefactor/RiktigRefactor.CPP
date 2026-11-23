template <typename T> struct Box {
    T value{};
    T /*|*/get() const { return value; }
};

int main() {
    Box<int> box{4};
    return box.get() == 4 ? 0 : 1;
}
