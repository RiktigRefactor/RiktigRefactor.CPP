int copies = 0;

struct Point {
    int x;
    int y;
    Point(int x, int y) : x(x), y(y) {}
    Point(const Point &other) : x(other.x), y(other.y) { ++copies; }
};

int main() {
    int x = 40;
    Point point = [](int x) { return Point(x, 2); }(x);
    return point.x + point.y == 42 && copies == 0 ? 0 : 1;
}
