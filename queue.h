//
// Created by sunlnx on 18-10-31.
//
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
typedef struct QueueNode{
    void* data;
    struct QueueNode *next;
    struct QueueNode *previous;
} QueueNode;


typedef struct Queue {
    QueueNode *head;
    QueueNode *tail;
    int size;
} Queue;

Queue* create_queue();
void push_queue(Queue*, void*);
void* pop_queue(Queue*);
bool is_empty_queue(Queue*);
void destroy_queue(Queue *queue);