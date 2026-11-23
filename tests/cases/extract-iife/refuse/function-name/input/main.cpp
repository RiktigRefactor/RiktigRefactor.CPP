int main() {
    const char *name = /*[*/__func__/*]*/;
    return name[0] == 'm' ? 0 : 1;
}
