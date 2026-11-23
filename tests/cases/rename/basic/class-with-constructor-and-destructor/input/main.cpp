#include "counter.h"

extern int destroyed;

int main() {
    {
        /*|*/Counter counter(41);
        if (counter.next() != 42) {
            return 1;
        }
        Counter *other = new Counter(1);
        other->~Counter();
        ::operator delete(other);
    }
    return destroyed == 2 ? 0 : 1;
}
