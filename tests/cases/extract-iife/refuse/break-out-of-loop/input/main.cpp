int main() {
    int count = 0;
    while (true) {
        ++count;
        /*[*/if (count == 3) {
            break;
        }/*]*/
    }
    return count == 3 ? 0 : 1;
}
