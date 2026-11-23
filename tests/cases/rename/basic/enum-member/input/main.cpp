enum class Color { Red, /*|*/Lime };

int score(Color color) {
    using enum Color;
    switch (color) {
    case Red:
        return 1;
    case Lime:
        return 2;
    }
    return 0;
}

int main() { return score(Color::Lime) == 2 ? 0 : 1; }
