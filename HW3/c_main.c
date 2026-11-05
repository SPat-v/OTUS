#include <stdio.h>
#include <stdlib.h>

typedef struct Node {
    long value;
    struct Node *next;
} Node;

/* add_element: создаёт новый узел и кладёт его в голову списка */
Node *add_element(long value, Node *next) {
    Node *node = malloc(sizeof(Node));
    if (!node) abort();
    node->value = value;
    node->next  = next;
    return node;
}

/* print_int */
void print_int(long x) {
    printf("%ld ", x);
    fflush(stdout);
}

/* p: предикат нечётности */
int p(long x) {
    return x & 1;
}

/* m: обход списка с вызовом функции для каждого значения */
void m(Node *list, void (*fn)(long)) {
    for (Node *n = list; n != NULL; n = n->next) {
        fn(n->value);
    }
}

/* f: фильтрация списка по предикату, возвращает новый список */
Node *f(Node *list, Node *acc, int (*pred)(long)) {
    if (list == NULL) {
        return acc;
    }
    if (pred(list->value)) {
        acc = add_element(list->value, acc);
    }
    return f(list->next, acc, pred);
}

int main(void) {
    long data[] = {4, 8, 15, 16, 23, 42};
    size_t n = sizeof(data) / sizeof(data[0]);

    /* Строим список: элементы добавляются в голову,
       поэтому идём по массиву с конца, чтобы получить порядок 4,8,15,16,23,42 */
    Node *list = NULL;
    for (size_t i = n; i-- > 0; ) {
        list = add_element(data[i], list);
    }

    m(list, print_int);
    puts("");

    Node *filtered = f(list, NULL, p);
    m(filtered, print_int);
    puts("");

    /* освобождение памяти  */
    while (list)     { Node *t = list->next;     free(list); list = t; }
    while (filtered) { Node *t = filtered->next; free(filtered); filtered = t; }

    return 0;
}