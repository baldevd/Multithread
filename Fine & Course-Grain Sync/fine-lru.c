/* -*- mode:c; c-file-style:"k&r"; c-basic-offset: 4; tab-width:4; indent-tabs-mode:nil; mode:auto-fill; fill-column:78; -*- */
/* vim: set ts=4 sw=4 et tw=78 fo=cqt wm=0: */

/* @* Place your name here, and any other comments *@
 * @* that deanonymize your work inside this syntax *@
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "lru.h"

/* Define the simple, singly-linked list we are going to use for tracking lru */
struct list_node {
    struct list_node* next;
    int key;
    int refcount;
    // Protects this node's contents
    pthread_mutex_t mutex;
};

static struct list_node* list_head = NULL;

/* A static mutex; protects the count and head.
 * XXX: We will have to tolerate some lag in updating the count to avoid
 * deadlock. */
static pthread_mutex_t mutex;
static int count = 0;
static pthread_cond_t cv_low, cv_high;

static volatile int done = 0;

/* Initialize the mutex. */
int init (int numthreads) {
    /* Your code here */
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cv_low, NULL);
    pthread_cond_init(&cv_high, NULL);

    /* Temporary code to suppress compiler warnings about unused variables.
     * You should remove the following lines once this file is implemented.
     */
    (void)list_head;
    (void)mutex;
    (void)count;
    (void)cv_low;
    (void)cv_high;
    /* End temporary code */
    return 0;
}

/* Return 1 on success, 0 on failure.
 * Should set the reference count up by one if found; add if not.*/
int reference (int key) {
    int found = 0;
    struct list_node* cursor = list_head;
    struct list_node* last = NULL;
    int i = 0;

    while(cursor) {
        if (cursor->key < key) {
            
            // Checks if past first iteration (last no longer NULL)
            if(i > 0) pthread_mutex_unlock(&(last->mutex)); 
            else pthread_mutex_lock(&(cursor->mutex));

            last = cursor;
            cursor = cursor->next;
            
            // Checks if we have traversed to end of list
            if (cursor == NULL) {
                pthread_mutex_unlock(&(last->mutex));
                break;
            }
            
            pthread_mutex_lock(&(cursor->mutex));
        } else {
            if (cursor->key == key) {
                cursor->refcount++;
                found++;
            }
            if (i > 0)pthread_mutex_unlock(&(last->mutex));
            pthread_mutex_unlock(&(cursor->mutex));
            break;
        }
        i++;
    }

    if (!found) {
        // Handle 2 cases: the list is empty/we are trying to put this at the
        // front, and we want to insert somewhere in the middle or end of the
        // list.
        pthread_mutex_lock(&mutex);

        if (count >= HIGH_WATER_MARK) {
            pthread_cond_wait(&cv_high, &mutex);
        }

        struct list_node* new_node = malloc(sizeof(struct list_node));
        if (!new_node) return 0;
        count++;
        new_node->key = key;
        new_node->refcount = 1;
        new_node->next = cursor;
        pthread_mutex_init(&(new_node->mutex), NULL);

        if (last == NULL)
            list_head = new_node;
        else
            last->next = new_node;

        
        if (count >= LOW_WATER_MARK) {
            pthread_cond_signal(&cv_low);
        }

        pthread_mutex_unlock(&mutex);

    }

    return 1;
}

/* Do a pass through all elements, either decrement the reference count,
 * or remove if it hasn't been referenced since last cleaning pass.
 *
 * check_water_mark: If 1, block until there are more elements in the cache
 * than the LOW_WATER_MARK.  This should only be 0 during self-testing or in
 * single-threaded mode.
 */
void clean(int check_water_mark) {
    struct list_node* cursor = list_head;
    struct list_node* last = NULL;
    int i = 0;

    while(cursor) {
        if (i == 0) pthread_mutex_lock(&(cursor->mutex));
        cursor->refcount--;
        if (cursor->refcount == 0) {
            pthread_mutex_lock(&mutex);

            if (count < LOW_WATER_MARK) {
                pthread_cond_wait(&cv_low, &mutex);
            }   

            struct list_node* tmp = cursor;
            if (last) {
                last->next = cursor->next;
            } else {
                 list_head = cursor->next;
            }
            tmp = cursor->next;
            free(cursor);
            if (tmp == NULL) {
                if (last) pthread_mutex_unlock(&(last->mutex));
            }
            cursor = tmp;
            count--;

            if (count <= HIGH_WATER_MARK) {
                pthread_cond_signal(&cv_high);
            }

            pthread_mutex_unlock(&mutex);
        } else {
            if (last) pthread_mutex_unlock(&(last->mutex));

            last = cursor;
            cursor = cursor->next;
            
            if (cursor == NULL) {
                pthread_mutex_unlock(&(cursor->mutex));
                pthread_mutex_unlock(&mutex);
                break;
            }

            pthread_mutex_lock(&(cursor->mutex));
        }
        i++;
    }
    if (last) pthread_mutex_unlock(&(last->mutex));
}


/* Optional shut-down routine to wake up blocked threads.
   May not be required. */
void shutdown_threads (void) {
    if (done == 1) {
        exit(0);
    }
    
    pthread_cond_broadcast(&cv_low);
    pthread_cond_broadcast(&cv_high);
    done = 1;

    return;
}

/* Print the contents of the list.  Mostly useful for debugging. */
void print (void) {
    printf("=== Starting list print ===\n");
    printf("=== Total count is %d ===\n", count);
    struct list_node* cursor = list_head;
    while(cursor) {
        printf ("Key %d, Ref Count %d\n", cursor->key, cursor->refcount);
        cursor = cursor->next;
    }
    printf("=== Ending list print ===\n");
}
