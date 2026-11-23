#include "counter.h"

int destroyed = 0;

Counter::Counter(int start) : value(start) {}
Counter::~Counter() { ++destroyed; }
int Counter::next() { return ++value; }
