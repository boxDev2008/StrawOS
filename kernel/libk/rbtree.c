static void rb_rotate_left(RbTree *tree, RbNode *x)
{
    RbNode *y = x->right;

    x->right = y->left;
    if (y->left)
        y->left->parent = x;

    y->parent = x->parent;
    if (!x->parent)
        tree->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;

    y->left = x;
    x->parent = y;
}

static void rb_rotate_right(RbTree *tree, RbNode *x)
{
    RbNode *y = x->left;

    x->left = y->right;
    if (y->right)
        y->right->parent = x;

    y->parent = x->parent;
    if (!x->parent)
        tree->root = y;
    else if (x == x->parent->right)
        x->parent->right = y;
    else
        x->parent->left = y;

    y->right = x;
    x->parent = y;
}

static bool rb_is_red(const RbNode *n)
{
    return n && n->red;
}

static RbNode *rb_min(RbNode *n)
{
    while (n->left)
        n = n->left;
    return n;
}

RbNode *rb_next(const RbNode *n)
{
    if (n->right)
        return rb_min(n->right);

    RbNode *p = n->parent;
    while (p && n == p->right)
    {
        n = p;
        p = p->parent;
    }
    return p;
}

void rb_insert(RbTree *tree, RbNode *node, RbLess less)
{
    RbNode **link = &tree->root;
    RbNode *parent = NULL;
    bool leftmost = true;

    while (*link)
    {
        parent = *link;
        if (less(node, parent))
        {
            link = &parent->left;
        }
        else
        {
            link = &parent->right;
            leftmost = false;
        }
    }

    node->parent = parent;
    node->left = NULL;
    node->right = NULL;
    node->red = true;
    *link = node;

    if (leftmost)
        tree->leftmost = node;

    RbNode *n = node;
    RbNode *p;
    while ((p = n->parent) && p->red)
    {
        RbNode *g = p->parent;

        if (p == g->left)
        {
            RbNode *u = g->right;
            if (rb_is_red(u))
            {
                p->red = false;
                u->red = false;
                g->red = true;
                n = g;
                continue;
            }
            if (n == p->right)
            {
                rb_rotate_left(tree, p);
                n = p;
                p = n->parent;
            }
            p->red = false;
            g->red = true;
            rb_rotate_right(tree, g);
        }
        else
        {
            RbNode *u = g->left;
            if (rb_is_red(u))
            {
                p->red = false;
                u->red = false;
                g->red = true;
                n = g;
                continue;
            }
            if (n == p->left)
            {
                rb_rotate_right(tree, p);
                n = p;
                p = n->parent;
            }
            p->red = false;
            g->red = true;
            rb_rotate_left(tree, g);
        }
    }

    tree->root->red = false;
}

static void rb_transplant(RbTree *tree, RbNode *u, RbNode *v)
{
    if (!u->parent)
        tree->root = v;
    else if (u == u->parent->left)
        u->parent->left = v;
    else
        u->parent->right = v;

    if (v)
        v->parent = u->parent;
}

static void rb_erase_fixup(RbTree *tree, RbNode *x, RbNode *xp)
{
    while (x != tree->root && !rb_is_red(x))
    {
        if (x == xp->left)
        {
            RbNode *w = xp->right;
            if (w->red)
            {
                w->red = false;
                xp->red = true;
                rb_rotate_left(tree, xp);
                w = xp->right;
            }
            if (!rb_is_red(w->left) && !rb_is_red(w->right))
            {
                w->red = true;
                x = xp;
                xp = x->parent;
            }
            else
            {
                if (!rb_is_red(w->right))
                {
                    w->left->red = false;
                    w->red = true;
                    rb_rotate_right(tree, w);
                    w = xp->right;
                }
                w->red = xp->red;
                xp->red = false;
                if (w->right)
                    w->right->red = false;
                rb_rotate_left(tree, xp);
                x = tree->root;
                xp = NULL;
            }
        }
        else
        {
            RbNode *w = xp->left;
            if (w->red)
            {
                w->red = false;
                xp->red = true;
                rb_rotate_right(tree, xp);
                w = xp->left;
            }
            if (!rb_is_red(w->left) && !rb_is_red(w->right))
            {
                w->red = true;
                x = xp;
                xp = x->parent;
            }
            else
            {
                if (!rb_is_red(w->left))
                {
                    w->right->red = false;
                    w->red = true;
                    rb_rotate_left(tree, w);
                    w = xp->left;
                }
                w->red = xp->red;
                xp->red = false;
                if (w->left)
                    w->left->red = false;
                rb_rotate_right(tree, xp);
                x = tree->root;
                xp = NULL;
            }
        }
    }

    if (x)
        x->red = false;
}

void rb_erase(RbTree *tree, RbNode *z)
{
    if (tree->leftmost == z)
        tree->leftmost = rb_next(z);

    RbNode *y = z;
    RbNode *x;
    RbNode *xp;
    bool y_was_red = y->red;

    if (!z->left)
    {
        x = z->right;
        xp = z->parent;
        rb_transplant(tree, z, z->right);
    }
    else if (!z->right)
    {
        x = z->left;
        xp = z->parent;
        rb_transplant(tree, z, z->left);
    }
    else
    {
        y = rb_min(z->right);
        y_was_red = y->red;
        x = y->right;

        if (y->parent == z)
        {
            xp = y;
        }
        else
        {
            xp = y->parent;
            rb_transplant(tree, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }

        rb_transplant(tree, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->red = z->red;
    }

    if (!y_was_red)
        rb_erase_fixup(tree, x, xp);

    z->parent = z->left = z->right = NULL;
}
