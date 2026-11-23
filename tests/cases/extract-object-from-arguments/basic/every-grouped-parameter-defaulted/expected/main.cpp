struct Size {
    int width = 2;
    int height = 3;
};

int area(int scale, Size size = {}) { return scale * size.width * size.height; }

int main() { return area(1) == 6 && area(2, Size{.width = 5}) == 30 && area(1, Size{.width = 1, .height = 1}) == 1 ? 0 : 1; }
