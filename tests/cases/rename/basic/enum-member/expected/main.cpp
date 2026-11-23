enum class Color { Red, Green };

int score(Color color) {
    using enum Color;
    switch (color) {
    case Red:
        return 1;
    case Green:
        return 2;
    }
    return 0;
}

int main() { return score(Color::Green) == 2 ? 0 : 1; }
