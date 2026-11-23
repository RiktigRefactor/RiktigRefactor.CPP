int log[3];
int logged = 0;

struct Guard {
    ~Guard() { log[logged++] = 2; }
};

int main() {
    {
        /*[*/Guard guard;
        log[logged++] = 1;/*]*/
        log[logged++] = 3;
    }
    return log[0] == 1 && log[1] == 3 && log[2] == 2 ? 0 : 1;
}
