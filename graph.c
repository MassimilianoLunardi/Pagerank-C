#include "graph.h"

// Creazione del grafo
grafo *crea_grafo(int N) {
    grafo *g = (grafo *)malloc(sizeof(grafo));
    g->N = N;
    g->num_archi = 0;
    g->out = (int *)calloc(N, sizeof(int));
    g->in = (inmap *)malloc(N * sizeof(inmap));
    g->node_mutex = (pthread_mutex_t *)malloc(N * sizeof(pthread_mutex_t));
    xpthread_mutex_init(&g->global_mutex, NULL, index);

    if(!g || !g->in|| !g->out|| !g->node_mutex)
        xtermina("Errore: Allocazione Grafo fallita", index);

    for (int i = 0; i < N; i++) {
        g->in[i].nodi = NULL;
        g->in[i].dim = 0;
        g->in[i].cap = 0;
        xpthread_mutex_init(&g->node_mutex[i], NULL, index);
    }
    return g;
}

// Libera la memoria del grafo
void free_grafo(grafo *g) {
    for (int i = 0; i < g->N; i++) {
        free(g->in[i].nodi);
        xpthread_mutex_destroy(&g->node_mutex[i], index);
    }
    free(g->in);
    free(g->out);
    free(g->node_mutex);
    xpthread_mutex_destroy(&g->global_mutex, index);
    free(g);
}

// Aggiunge un arco al grafo
void add_arc(grafo *g, int i, int j) {

    if (i == j) return;
    
    xpthread_mutex_lock(&g->node_mutex[j], index);

    // Controlla se l'arco è duplicato
    for (int k = 0; k < g->in[j].dim; k++) {
        if (g->in[j].nodi[k] == i) {
            xpthread_mutex_unlock(&g->node_mutex[j], index);
            return; // Arco già presente
        }
    }

    // Aggiunge il nodo entrante
    if (g->in[j].dim == g->in[j].cap) {
        g->in[j].cap = g->in[j].cap == 0 ? 8 : g->in[j].cap * 2;
        g->in[j].nodi = (int *)realloc(g->in[j].nodi, g->in[j].cap * sizeof(int));

        if (!g->in[j].nodi)
            xtermina("Errore: realloc dei nodi fallita", index);
    }
    g->in[j].nodi[g->in[j].dim++] = i;

    xpthread_mutex_unlock(&g->node_mutex[j], index);

    // Incrementa il conteggio degli archi uscenti
    xpthread_mutex_lock(&g->global_mutex,index);
    g->out[i]++;
    g->num_archi++;
    xpthread_mutex_unlock(&g->global_mutex, index);
}

// Inizializza il buffer
void init_buffer(pc_buffer *buf, int size) {
    buf->buffer = (coppia *)malloc(size * sizeof(coppia));

    if (!buf->buffer)
        xtermina("Errore: realloc di buf fallita", index);

    buf->size = size;
    buf->start = buf->end = buf->count = 0;
    xsem_init(&buf->empty, 0, size, index);
    xsem_init(&buf->full, 0, 0, index);
    xpthread_mutex_init(&buf->mutex, NULL, index);
}

// Distrugge il buffer
void destroy_buffer(pc_buffer *buf) {
    free(buf->buffer);
    xsem_destroy(&buf->empty, index);
    xsem_destroy(&buf->full, index);
    xpthread_mutex_destroy(&buf->mutex, index);
}

// Produttore
void producer(pc_buffer *buf, int i, int j) {
    xsem_wait(&buf->empty, index);
    xpthread_mutex_lock(&buf->mutex, index);

    buf->buffer[buf->end].i = i;
    buf->buffer[buf->end].j = j;
    buf->end = (buf->end + 1) % buf->size;
    buf->count++;

    xpthread_mutex_unlock(&buf->mutex, index);
    xsem_post(&buf->full, index);
}

// Consumatore
void *consumer(void *args) {
    pc_buffer *buf = ((pc_buffer **)args)[0];
    grafo *g = ((grafo **)args)[1];

    while (1) {
        xsem_wait(&buf->full, index);
        xpthread_mutex_lock(&buf->mutex, index);

        if (buf->count == 0) {
            xpthread_mutex_unlock(&buf->mutex, index);
            xsem_post(&buf->full, index);
            break; // Fine del lavoro
        }

        int i = buf->buffer[buf->start].i;
        int j = buf->buffer[buf->start].j;
        buf->start = (buf->start + 1) % buf->size;
        buf->count--;

        xpthread_mutex_unlock(&buf->mutex, index);
        xsem_post(&buf->empty, index);

        add_arc(g, i, j);
    }
    return NULL;
}
