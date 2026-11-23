#include "counter.h"

int destroyed = 0;

Tally::Tally(int start) : value(start) {}
Tally::~Tally() { ++destroyed; }
int Tally::next() { return ++value; }
