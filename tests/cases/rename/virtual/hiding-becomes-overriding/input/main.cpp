struct Task {
    virtual int run() const { return 1; }
    virtual ~Task() = default;
};

struct Job : Task {
    int /*|*/execute() const { return 2; }
};

int main() {
    Job job;
    const Task &task = job;
    return task.run() == 1 && job.execute() == 2 ? 0 : 1;
}
