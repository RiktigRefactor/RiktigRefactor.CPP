#pragma once

class Counter {
  public:
    explicit Counter(int start);
    ~Counter();
    int next();

  private:
    int value;
};
