//File:   queue.c
//
//This file contains the implementation of a Queue class using a linked list.

#include <stdlib.h>
#include <stdio.h>
#include "queue.h"

// This is the default constructor.
Queue* InitQueue()
{
    Queue* queue = (Queue*)malloc(sizeof(Queue));
    queue->frontPtr = NULL;
    queue->rearPtr  = NULL;
    return queue;
}

//tests for an empty queue.
int IsEmpty(Queue *queue) 
{
    return  queue->frontPtr == NULL;
} 

// This function gets an element from the front of the queue.
void getQueue(Queue *queue, char* buf)
{
  if (~IsEmpty(queue))
   { 
       NodePtr oldPtr = queue->frontPtr;
       queue->frontPtr = queue->frontPtr->next;
       sprintf(buf, oldPtr->buf);

       free(oldPtr->buf);
       free(oldPtr);
   }
} 

// This function adds an element to the rear of the queue.  
void putQueue(Queue *queue, char* buf, int type)
{
    NodePtr newPtr = (NodePtr)malloc(sizeof(Node));
    newPtr->buf = buf;
    newPtr->type = type;
    newPtr->next = NULL;
    
    if (IsEmpty(queue))      //Was the queue empty?
    {
        queue->frontPtr = newPtr;
        queue->rearPtr = newPtr; 
    }
    else
    {
        queue->rearPtr->next = newPtr;
        queue->rearPtr = newPtr;
    }
}

