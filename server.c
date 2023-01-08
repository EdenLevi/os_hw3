#include "segel.h"
#include "request.h"
#include <stdbool.h>

#define EMPTY -1
// 
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// HW3: Parse the new arguments too


pthread_cond_t master;
pthread_mutex_t m;
pthread_cond_t slave;

typedef enum {
    BLOCK, DT, DH, RANDOM, ERROR
} sched;

typedef struct list_t {
    Thread *threads;
    int first;
    int last;
    int size;
    int counter;

} *list;

typedef struct threadM_t {
    list run_list;
    list buffer_list;
    int id;
} *ThreadManager;

list createList(int size) {
    list l = malloc(sizeof(*l));
    if (l == NULL)
        return l;
    l->size = size;
    l->counter = 0;
    l->first = 0;
    l->last = 0;
    l->threads = malloc(sizeof(*l->threads) * (l->size));
    return l;
}

void listAdd(list l, Thread t) {
    if (l->counter == l->size) {
        return;
    }
    l->counter++;
    if (l->last == l->size - 1) {
        l->last = 0;
        l->threads[l->last] = t;
    } else {
        l->threads[++l->last] = t;
    }
}

void listRemove(list l, bool is_head) {
    l->counter--;
    if (l->counter == 0)
        return;
    if (is_head) {
        l->first = l->first == (l->size - 1) ? 0 : l->first + 1;
    } else {
        l->last = l->last == 0 ? l->size - 1 : l->last - 1;
    }
}


void deleteList(list l) {
    if (!l)
        return;
    if (l->first < l->last) {
        for (int i = l->first; i < l->last; i++) {
            Close((l->threads[i])->id);
            free(l->threads[i]);
        }
    } else {
        for (int i = l->first; i < l->size; i++) {
            Close((l->threads[i])->id);
            free(l->threads[i]);
        }
        for (int i = 0; i < l->last; i++) {
            Close((l->threads[i])->id);
            free(l->threads[i]);
        }
    }
    free(l->threads);
    free(l);
}


void *createThread(void *args) {
    struct stati_t stat;
    ThreadManager threadM = ((ThreadManager) args);
    stat.id=threadM->id;
    stat.stat_dyn=0;
    stat.stat_stc=0;
    while (1) {
        pthread_mutex_lock(&m);
        while (!threadM->buffer_list->counter) {
            pthread_cond_wait(&slave, &m);
        }

        Thread t = threadM->buffer_list->counter ? threadM->buffer_list->threads[threadM->buffer_list->first] : NULL;
        listRemove(threadM->buffer_list, 1);

        threadM->run_list->counter++;
        gettimeofday(&(t->free_time), NULL);
        pthread_mutex_unlock(&m);

        //maybe we will need to pass t and not t->id
        requestHandle(t, &stat);
        Close(t->id);
        free(t);

        pthread_mutex_lock(&m);
        threadM->run_list->counter--;
        pthread_cond_signal(&master);
        pthread_mutex_unlock(&m);
    }
    pthread_exit(NULL);
    return NULL;
}

void getargs(int *port, int *threads, int *queue_size, int *schedalg, int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    *port = atoi(argv[1]);
    *threads = atoi(argv[2]);
    *queue_size = atoi(argv[3]);

    if (!strcmp(argv[4], "block")) *schedalg = BLOCK;
    else if (!strcmp(argv[4], "dh")) *schedalg = DH;
    else if (!strcmp(argv[4], "dt")) *schedalg = DT;
    else if (!strcmp(argv[4], "random")) *schedalg = RANDOM;
    else *schedalg = ERROR;
}


int main(int argc, char *argv[]) {
    int listenfd, connfd, port, clientlen, size, threads, schedalg;
    struct sockaddr_in clientaddr;

    getargs(&port, &threads, &size, &schedalg, argc, argv);

    pthread_mutex_init(&m, NULL);
    pthread_cond_init(&master, NULL);
    pthread_cond_init(&slave, NULL);

    list run_list = createList(threads);
    list wait_list = createList(size);

    ThreadManager t_man = (ThreadManager *) malloc(threads * sizeof(*t_man));
    pthread_t *t = (pthread_t *) malloc(threads * sizeof(*t));

    if (!t_man || !t) return NULL;

    for (int i = 0; i < threads; i++) {
        t_man[i].buffer_list = wait_list;
        t_man[i].run_list = run_list;
        t_man[i].id = i;
        pthread_create(&t[i], NULL, createThread, (void *) &t_man[i]);
    }

    struct timeval time;


    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *) &clientaddr, (socklen_t * ) & clientlen);

        //
        // HW3: In general, don't handle the request in the main thread.
        // Save the relevant info in a buffer and have one of the worker threads
        // do the work.
        //

        Thread new_t = (Thread) malloc(sizeof(*new_t));
        if (!new_t) {
            return NULL;
        }
        new_t->id = connfd;
        gettimeofday(&(new_t->init_time), NULL);

        pthread_mutex_lock(&m);
        if (run_list->counter == run_list->size) {
            Close(connfd);
            free(new_t);
        } else if (wait_list->counter != 0 && ((wait_list->counter + run_list->counter) >= wait_list->size)) {
            int num_to_drop;
            Thread del;
            switch (schedalg) {
                case BLOCK:
                    while (wait_list->counter + run_list->counter >= size) {
                        pthread_cond_wait(&master, &m);
                    }
                    listAdd(wait_list, new_t);
                    break;

                case DT:
                    Close(new_t->id);
                    free(new_t);
                    break;

                case DH:
                    del = wait_list->counter ? wait_list->threads[wait_list->first] : NULL;
                    listRemove(wait_list, 1);
                    Close(del->id);
                    free(del);
                    listAdd(wait_list, new_t);
                    break;

                case RANDOM:
                    for (int i = 0; i < wait_list->counter / 2; i++) {
                        int random = rand() % 2;
                        del = random ? wait_list->threads[wait_list->first] : wait_list->threads[wait_list->last];
                        listRemove(wait_list, random);
                        Close(del->id);
                        free(del);
                    }
                    break;

                case ERROR:
                    Close(new_t->id);
                    free(new_t);
                    break;
            }
        } else {
            listAdd(wait_list, new_t);
        }
        pthread_cond_broadcast(&slave);
        pthread_mutex_unlock(&m);
    }
    deleteList(wait_list);
    deleteList(run_list);
    free(t);
    free(t_man);
    pthread_mutex_destroy(&m);
    pthread_cond_destroy(&master);
    pthread_cond_destroy(&slave);
    return 0;
}


    


 
