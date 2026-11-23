int main() {
    int count = 0;
again:
    if (++count < 3) {
        goto again;
    }
    goto finished;
    count = 100;
finished:
    return count == 3 ? 0 : 1;
}
