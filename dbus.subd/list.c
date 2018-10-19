#include <stdlib.h>
#include "list.h"

struct list_t *list_create() {
	struct list_t *list;
	list = malloc(sizeof(struct list_t));
	if (list != NULL) {
		list->head = NULL;
		list->tail = NULL;
		list->current = NULL;
		list->size = 0;
	}

	return list;
}

void list_destroy(struct list_t *list) {
	struct node *save_next;
	list->current = list->head;
	while(list->current != NULL) {
		save_next = list->current->next;
		free(list->current->data);
		free(list->current);
		list->current = save_next;
	}
	free(list);
}

int list_append(struct list_t *list, void *data) {
	struct node *node;
	node = malloc(sizeof(struct node));
	if (node ==NULL) {
			return -1;
	}

	node->data = data;
	node->next = NULL;
	if (list->size == 0) {
		list->head = node;
		list->tail = node;
	} else {
		list->tail->next = node;
		list->tail = node;
	}
	list->size++;

	return 0;
}

