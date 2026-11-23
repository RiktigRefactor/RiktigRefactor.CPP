int /*|*/area(int scale, int width = 2, int height = 3) { return scale * width * height; }

int main() { return area(1) == 6 && area(2, 5) == 30 && area(1, 1, 1) == 1 ? 0 : 1; }
