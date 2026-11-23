#include "counter.h"

extern int destroyed;

int main() {
    {
        Tally counter(41);
        if (counter.next() != 42) {
            return 1;
        }
        Tally *other = new Tally(1);
        other->~Tally();
        ::operator delete(other);
    }
    return destroyed == 2 ? 0 : 1;
}
