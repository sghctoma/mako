#ifndef LIST_H
#define LIST_H

struct node {
	void *data;
	struct node *next;
};

struct list_t {
	struct node *head;
	struct node *tail;
	struct node *current;
	int size;
};

struct list_t *list_create();
void list_destroy(struct list_t *list);
int list_append(struct list_t *list, void *data);

#endif
