namespace lib {
struct Job {};
int /*|*/process(Job) { return 1; }
} // namespace lib

template <class V> int work(V) { return 2; }

template <class U> int run(U job) { return work(job); }

int main() { return run(lib::Job{}) == 2 && lib::process(lib::Job{}) == 1 ? 0 : 1; }
