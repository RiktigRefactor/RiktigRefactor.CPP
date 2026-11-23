int classify(int value) {
    /*[*/if (value < 0) {
        return 1;
    }/*]*/
    return 0;
}

int main() { return classify(-5) == 1 && classify(5) == 0 ? 0 : 1; }
