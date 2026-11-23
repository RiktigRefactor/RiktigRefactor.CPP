int main() {
    int count = 0;
again:
    if (++count < 3) {
        goto again;
    }
    goto /*|*/done;
    count = 100;
done:
    return count == 3 ? 0 : 1;
}
