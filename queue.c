//
// Created by sunlnx on 18-10-31.
//
#include "queue.h"


Queue* create_queue() {
    Queue *queue = (Queue*)malloc(sizeof(Queue));
    QueueNode *head = (QueueNode*)malloc(sizeof(QueueNode));
    QueueNode *tail = (QueueNode*)malloc(sizeof(QueueNode));
    head->data = tail->data = NULL;
    head->next = tail;
    head->previous = NULL;
    tail->previous = head;
    tail->next = NULL;
    queue->head = head;
    queue->tail = tail;
    queue->size = 0;
    return queue;
}

void push_queue(Queue *queue, void *data) {
    QueueNode *node = (QueueNode*)malloc(sizeof(QueueNode));
    node->data = data;
    node->next = queue->tail;
    node->previous = queue->tail->previous;
    queue->tail->previous->next = node;
    queue->tail->previous = node;
    queue->size += 1;
}

void* pop_queue(Queue *queue) {
    if (!is_empty_queue(queue)) {
        QueueNode *node = queue->head->next;
        queue->head->next->next->previous = queue->head;
        queue->head->next = queue->head->next->next;
        queue->size -= 1;
        void* data = node->data;
        free(node);
        return data;
    }
    return NULL;
}

bool is_empty_queue(Queue *queue) {
    return queue->head->next == queue->tail;
}


void destroy_queue(Queue * queue) {
    while (!is_empty_queue(queue)) {
        QueueNode *node = pop_queue(queue);
        free(node);
    }
    free(queue->head);
    free(queue->tail);
    free(queue);
}
