typedef struct RbNode RbNode;

struct RbNode
{
    RbNode *parent;
    RbNode *left;
    RbNode *right;
    bool    red;
};

typedef struct
{
    RbNode *root;
    RbNode *leftmost;
}
RbTree;

typedef bool (*RbLess)(const RbNode *a, const RbNode *b);

#define RB_ENTRY(node, type, member) \
    ((type *)((char *)(node) - offsetof(type, member)))

void    rb_insert(RbTree *tree, RbNode *node, RbLess less);
void    rb_erase(RbTree *tree, RbNode *node);
RbNode *rb_next(const RbNode *node);

ALWAYS_INLINE RbNode *rb_first(const RbTree *tree)
{
    return tree->leftmost;
}
