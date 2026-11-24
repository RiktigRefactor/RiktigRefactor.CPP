// begin-snippet: rename-before
struct Size {
    int /*|*/w;
    int h;
};
// end-snippet

struct Frame {
    Frame() : w(3) {}
    int w;
};

int main() {
    Size size{.w = 2, .h = 5};
    Frame frame;
    int area = size.w * size.h;
    return area + frame.w == 13 ? 0 : 1;
}
