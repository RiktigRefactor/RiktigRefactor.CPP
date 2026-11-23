struct Node;
int depthOf(const Node *node);

struct Node {
    int value = 0;
    Node *next = nullptr;
    int twice() const { return value * 2; }
    int /*|*/sum(int limit) const {
        if (limit == 0 || next == nullptr) {
            return twice();
        }
        auto add = [&](int extra) { return value + extra; };
        const Node &me = *this;
        const Node *rest = this->next;
        return add(rest->sum(limit - 1)) + depthOf(this) + me.value - value + (*this).value - value;
    }
    int sumAll() const { return sum(10); }
};

int depthOf(const Node *node) { return node->next ? 1 + depthOf(node->next) : 0; }

int main() {
    Node last{5};
    Node first{1, &last};
    return first.sumAll() == 1 + 10 + 1 ? 0 : 1;
}
