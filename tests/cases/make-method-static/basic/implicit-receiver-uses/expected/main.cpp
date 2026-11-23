struct Node;
int depthOf(const Node *node);

struct Node {
    int value = 0;
    Node *next = nullptr;
    int twice() const { return value * 2; }
    static int sum(const Node &self, int limit) {
        if (limit == 0 || self.next == nullptr) {
            return self.twice();
        }
        auto add = [&](int extra) { return self.value + extra; };
        const Node &me = self;
        const Node *rest = self.next;
        return add(Node::sum(*rest, limit - 1)) + depthOf(&self) + me.value - self.value + (self).value - self.value;
    }
    int sumAll() const { return Node::sum(*this, 10); }
};

int depthOf(const Node *node) { return node->next ? 1 + depthOf(node->next) : 0; }

int main() {
    Node last{5};
    Node first{1, &last};
    return first.sumAll() == 1 + 10 + 1 ? 0 : 1;
}
