int /*|*/threshold = 10;

namespace config {
int limit = 99;
int read() { return threshold; }
} // namespace config

int main() { return config::read() == 10 && config::limit == 99 ? 0 : 1; }
