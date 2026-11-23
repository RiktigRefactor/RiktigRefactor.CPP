int main() {
    int a = 20;
    int b = 0;
    /*[*/a++;
    b = a * 2;/*]*/
    return a == 21 && b == 42 ? 0 : 1;
}
