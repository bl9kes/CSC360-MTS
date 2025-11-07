// CSC 360 - Operating Systems
// Blake Stewart - v00966622

// Build:       make
// Run:         ./mts input.txt
// Results:     Output.txt



#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

#define MAX_TRAINS 200 // Max number of trains
#define EAST 0
#define WEST 1
#define HIGH 1
#define LOW 0


// Create Train structure, one per input line. 
// Each train has its own thread.

typedef struct Train{
    int id;
    int direction;          // EAST = 0, WEST = 1
    int priority;           // HIGH = 1, LOW = 0
    int load_time;          // tenths of a second
    int cross_time;         // tenths of a second
    int loaded;             // 1 when ready to depart
    int granted;            // 1 when dispatcher has granted crossing
    double ready_time;      // time the train became ready
    pthread_t thread;       // POSIX thread handle
    pthread_cond_t cond_go; //per-train condvar
} Train;


typedef struct Node {
    Train *t;
    struct Node *next;
} Node;

typedef struct ready_queue {
    Node *head; // sorted by (ready_time, id)
} ready_queue;


static void rqueue_init(ready_queue *rqueue) { rqueue->head = NULL; }

static int rqueue_empty(ready_queue *rqueue) { return rqueue->head == NULL; }

// Insert train into ready queue.
static void rqueue_push_sorted(ready_queue *rqueue, Train *t) {
    Node *n = (Node *)malloc(sizeof(Node));
    if (!n) { perror("malloc"); exit(1); }
    n->t = t; n->next = NULL;
    if (!rqueue->head) { rqueue->head = n; return; }
    Node **cur = &rqueue->head;
    while (*cur) {
        Train *u = (*cur)->t;

        // t before u if earlier ready time
        if (t->ready_time < u->ready_time || (t->ready_time == u->ready_time && t->id < u->id)) break;
      cur = &((*cur)->next);
    }
    n->next = *cur; *cur = n;
}

// Pops the head of the ready queue, returns null if empty.
static Train *rqueue_pop(ready_queue *rqueue) {
    if (!rqueue->head) return NULL;
    Node *n = rqueue->head;
    rqueue->head = n->next;
    Train *t = n->t; free(n);
    return t;
}


// Global variables

static Train trains[MAX_TRAINS];
static int num_trains = 0;

// four queues total
static ready_queue q[2][2]; // [direction][priority]

static pthread_mutex_t mutex_ready = PTHREAD_MUTEX_INITIALIZER; // core scheduling
static pthread_mutex_t mutex_log = PTHREAD_MUTEX_INITIALIZER; // serialize logging
static pthread_cond_t cond_ready = PTHREAD_COND_INITIALIZER; // dispatcher

static int track_free = 1; // 1 if main track unblocked, 0 if blocked
static int remaining = 0; // number of trains waiting to depart
static int last_direction = -1; // -1 if none yet, else EAST/WEST
static int same_direction_streak = 0; // number of consecutive trains in last_direction

static FILE *log_file = NULL;

static const char* direction_str(int direction) {
    return direction == EAST ? "East" : "West";
}


// Monotonic time + logging

// returns seconds since first call t0.
static double now_secs(void) {
    static int init = 0;
    static struct timespec t0;
    if (!init) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        init = 1;
    } 
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double secs = (double)(t1.tv_sec - t0.tv_sec);
    double nsec = (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    return secs + nsec;
}

// write formatted log line to output.txt
static void log_event(const char *fmt, int id, int direction) {
    pthread_mutex_lock(&mutex_log);
    double t = now_secs();

    // convert to tenths of second with rounding.
    long long tenths_total = (long long)(t * 10.0 + 0.5);
    int hours = (int)(tenths_total / (10 * 3600));
    int mins = (int)((tenths_total / 10 % 3600) / 60);
    int secs = (int)((tenths_total / 10) % 60);
    int tenths = (int)(tenths_total % 10);

    if (log_file) {
      fprintf(log_file, "%02d:%02d:%02d.%1d ", hours, mins, secs, tenths);
      fprintf(log_file, fmt, id, direction_str(direction));
      fprintf(log_file, "\n");
      fflush(log_file);
    }
    pthread_mutex_unlock(&mutex_log);
}

// helper to ensure now_secs() has initialized t0.
static void time_init(void) {
    (void)now_secs(); // initializes t0
}

// Input parsing

// accepts one file with direction, load, and cross.
static void parse_input(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { perror("fopen"); exit(1); }
    
    char dir;
    int load_time, cross_time;
    int id = 0;
    
    while (fscanf(fp, " %c %d %d", &dir, &load_time, &cross_time) == 3) {
        if (id >= MAX_TRAINS) {
            fprintf(stderr, "Too many trains\n");
            exit(1);
        }
        Train *t = &trains[id];
        t->id = id;
        t->load_time = load_time;
        t->cross_time = cross_time;
        t->loaded = 0;
        t->granted = 0;
        t->ready_time = 0.0;
        pthread_cond_init(&t->cond_go, NULL);

        if (dir == 'E' || dir == 'e') {
            t->direction = EAST;
            t->priority = (dir == 'E') ? HIGH : LOW;
        } else if (dir == 'W' || dir == 'w') {
            t->direction = WEST;
            t->priority = (dir == 'W') ? HIGH : LOW;
        } else {
            fprintf(stderr, "Invalid direction: %c\n", dir);
            exit(1);
        }
        id++;
    }
    num_trains = id;
    fclose(fp);
  }


// Helpers for selection train

// atleast one train ready accross any queue?
static int any_ready(void) {
    return !rqueue_empty(&q[EAST][HIGH]) || !rqueue_empty(&q[EAST][LOW]) ||
           !rqueue_empty(&q[WEST][HIGH]) || !rqueue_empty(&q[WEST][LOW]);
}


// Core scheduling

// Enforces High over Low priority
// Prefers to send opposite of last direction
// After two in a row of same direction, offers priority to any from opposite direction

static Train* pick_next_train(void) {

    // fairness: two consec in same dir, send opposite if available
   if (last_direction != -1 && same_direction_streak >= 2) {
        int opposite = (last_direction == EAST) ? WEST : EAST;
        if (!rqueue_empty(&q[opposite][HIGH])) return rqueue_pop(&q[opposite][HIGH]);
        if (!rqueue_empty(&q[opposite][LOW]))  return rqueue_pop(&q[opposite][LOW]);
        // fall through if none opposite are ready
    }

    // prefer to send opposite dir if both sides exist
    // West first if none yet
    int preferred_direction = (last_direction == -1) ? WEST : (last_direction == EAST ? WEST : EAST);

    // HIGH first: preferred dir, then the other dir
    ready_queue *first = &q[preferred_direction][HIGH];
    ready_queue *other = &q[1 - preferred_direction][HIGH];


    if (!rqueue_empty(first) || !rqueue_empty(other)) {
        if (!rqueue_empty(first)) return rqueue_pop(first);
        if (!rqueue_empty(other)) return rqueue_pop(other);
    }

    // Try LOW: preferred dir, then the other dir
    first = &q[preferred_direction][LOW];
    other = &q[1 - preferred_direction][LOW];
    if (!rqueue_empty(first) || !rqueue_empty(other)) {
        if (!rqueue_empty(first)) return rqueue_pop(first);
        if (!rqueue_empty(other)) return rqueue_pop(other);
    }
    
    return NULL;
  }


// Train thread

static void *train_thread(void *arg) {
    Train *t = (Train *)arg;

    // simulate loading
    usleep(t->load_time * 100000); // tenths of a second

    // become ready and tell dispatch
    pthread_mutex_lock(&mutex_ready);
    t->loaded = 1;
    t->ready_time = now_secs();
    rqueue_push_sorted(&q[t->direction][t->priority], t);

    log_event("Train %2d is ready to go %4s", t->id, t->direction);
    pthread_cond_signal(&cond_ready);

    // wait for crossing selection by dispatcher
    while (!t->granted) {
        pthread_cond_wait(&t->cond_go, &mutex_ready);
    }
    // cross the track 
    pthread_mutex_unlock(&mutex_ready);
    
    log_event("Train %2d is ON the main track going %4s", t->id, t->direction);
    usleep(t->cross_time * 100000);
    log_event("Train %2d is OFF the main track after going %4s", t->id, t->direction);
    
           
    // after crossing update global metadata       
    pthread_mutex_lock(&mutex_ready);
    remaining--;
    if (last_direction == t->direction) {
        same_direction_streak++;
    } else {
        last_direction = t->direction;
        same_direction_streak = 1;
    }
    t->granted = 0; // reset
    track_free = 1; // updated track availability

    pthread_cond_signal(&cond_ready);
    pthread_mutex_unlock(&mutex_ready);
           
    return NULL;
}
            
// Dispatcher thread
            
static void *dispatcher_thread(void *arg) {
    (void)arg;
    pthread_mutex_lock(&mutex_ready);

    while (remaining > 0) {
        // sleep while track busy and no train ready
        while(!track_free || !any_ready()) {
            pthread_cond_wait(&cond_ready, &mutex_ready);
            if (remaining == 0) break;
        }
        if (remaining == 0) break;

        Train *next = pick_next_train();
        if (!next) continue;

        // occupy track
        track_free = 0;
        next->granted = 1;
        pthread_cond_signal(&next->cond_go);
        //loop
    }
    
    pthread_mutex_unlock(&mutex_ready);
    return NULL;
}
            

// Main
            
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input_file>\n", argv[0]);
        return(1);
    }

    // init queues
    rqueue_init(&q[EAST][HIGH]);
    rqueue_init(&q[EAST][LOW]);
    rqueue_init(&q[WEST][HIGH]);
    rqueue_init(&q[WEST][LOW]);

    // open output log
    log_file = fopen("output.txt", "w");
    if (!log_file) { perror("fopen output.txt"); return 1; }

    // initialize timing base
    time_init();

    // read trains from input
    parse_input(argv[1]);
    remaining = num_trains;

    // create dispatcher and train threads
    pthread_t dispatcher;
    if (pthread_create(&dispatcher, NULL, dispatcher_thread, NULL) != 0) {
        perror("pthread_create dispatcher");
        return 1;
    }
    
    // create N train threads
    for (int i = 0; i < num_trains; i++) {
        if (pthread_create(&trains[i].thread, NULL, train_thread, &trains[i]) != 0) {
            perror("pthread_create train");
            return 1;
        }
    }
        
    
    // join trains
    for (int i = 0; i < num_trains; i++) {
        pthread_join(trains[i].thread, NULL);
    }
        

    // wake dispatcher
    pthread_mutex_lock(&mutex_ready);
    pthread_cond_signal(&cond_ready);
    pthread_mutex_unlock(&mutex_ready);

    // join dispatcher
    pthread_join(dispatcher, NULL);

    fclose(log_file);
    return 0;
}

