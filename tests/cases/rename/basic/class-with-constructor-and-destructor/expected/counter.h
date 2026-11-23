#pragma once

class Tally {
  public:
    explicit Tally(int start);
    ~Tally();
    int next();

  private:
    int value;
};
