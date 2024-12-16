#include "xerrori.h"
#define BUFFER_SIZE 1024
#define index __LINE__,__FILE__ 

// Struttura per rappresentare gli insiemi di archi entranti
typedef struct {
    int *nodi;    // Array dinamico di nodi
    int dim;      // Numero di nodi nell'insieme
    int cap;      // Capacità dell'array
} inmap;

// Struttura per il grafo
typedef struct {
    int N;                        // Numero dei nodi
    int *out;                     // Array con il numero di archi uscenti da ogni nodo
    inmap *in;                    // Array con gli insiemi di archi entranti in ogni nodo
    pthread_mutex_t *node_mutex;  // Mutex per ogni nodo
    pthread_mutex_t global_mutex; // Mutex globale per contatori globali
    int num_archi;                 // Numero di archi validi
} grafo;

typedef struct{
    int i, j;
} coppia;

// Buffer per produttori-consumatori
typedef struct {
    coppia *buffer;         // Buffer per coppia di archi (i, j)
    int size;               // Dimensione del buffer
    int start;              // Indice di lettura
    int end;                // Indice di scrittura
    int count;              // Numero di elementi presenti
    sem_t empty;            // Semaforo spazi vuoti
    sem_t full;             // Semaforo elementi pieni
    pthread_mutex_t mutex;  // Mutex per accesso al buffer
} pc_buffer;

// Funzioni
grafo *crea_grafo(int N);
void free_grafo(grafo *g);
void add_arc(grafo *g, int i, int j);
void init_buffer(pc_buffer *buf, int size);
void destroy_buffer(pc_buffer *buf);
void producer(pc_buffer *buf, int i, int j);
void *consumer(void *args);