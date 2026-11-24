// begin-snippet: rename-after
struct Size {
    int width;
    int h;
};
// end-snippet

struct Frame {
    Frame() : w(3) {}
    int w;
};

int main() {
    Size size{.width = 2, .h = 5};
    Frame frame;
    int area = size.width * size.h;
    return area + frame.w == 13 ? 0 : 1;
}
