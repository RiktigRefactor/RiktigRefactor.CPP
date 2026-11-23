#include <exception>

struct Failure : std::exception {
    const char */*|*/what() const noexcept override { return "failure"; }
};

int main() {
    Failure failure;
    const std::exception &error = failure;
    return error.what()[0] == 'f' ? 0 : 1;
}
