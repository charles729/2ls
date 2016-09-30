#include <stdlib.h>

extern int __VERIFIER_nondet_int(void);

struct node {
    struct node *next;
};

static struct node* alloc_node(void)
{
    struct node *ptr = malloc(sizeof *ptr);
    if (!ptr)
        abort();

    ptr->next = NULL;
    return ptr;
}

static void chain_node(struct node **ppnode)
{
    struct node *node = alloc_node();
    node->next = *ppnode;
    *ppnode = node;
}

static struct node* create_sll(const struct node **pp1, const struct node **pp2)
{
    *pp2 = NULL;

    do
        chain_node(pp2);
    while (__VERIFIER_nondet_int());

    *pp1 = *pp2;

    do
      chain_node(pp1);
    while (__VERIFIER_nondet_int());

    struct node *list = *pp1;

    do
        chain_node(&list);
    while (__VERIFIER_nondet_int());    

    return list;
}

void check_seq_next(const struct node *beg, const struct node *const end) {
    assert(beg != NULL);
    assert(end != NULL);

    for (beg = beg->next; end != beg; beg = beg->next)
        assert(beg != NULL);
}

void main()
{
  const struct node *p1, *p2;

  struct node *list = create_sll(&p1, &p2);
  check_seq_next(list, p1);
  check_seq_next(p1, p2);

}
