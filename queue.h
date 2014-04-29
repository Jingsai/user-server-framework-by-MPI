//File:    queue.h
//
//This file contains the definition of the queue class implemented using

#ifndef _QUEUE_H_
#define _QUEUE_H_

typedef struct node   //a node in the Queue has this structure
{
    char * buf;
    int type;
    struct node* next;
}Node;

typedef Node* NodePtr;   //pointer to a node in the Queue

typedef struct
{
    NodePtr frontPtr;
    NodePtr rearPtr;
}Queue;

// default constructor
Queue* InitQueue();

//queue test method
int IsEmpty(Queue* queue);

// queue access methods 
void getQueue(Queue* queue, char* buf);
void putQueue(Queue* queue, char* buf, int type);

#endif

