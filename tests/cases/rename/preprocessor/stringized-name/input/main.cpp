#define NAME_OF(x) #x

int main() {
    int /*|*/count = 0;
    const char *name = NAME_OF(count);
    return name[0] == 'c' && count == 0 ? 0 : 1;
}
