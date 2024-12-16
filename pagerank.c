#include "xerrori.h"
#include "graph.h"

#define DEFAULT_DAMPING 0.9
#define DEFAULT_EPS 1.0e-7
#define DEFAULT_MAXITER 100
#define DEFAULT_T 3
#define DEFAULT_TOP_K 3

// Struttura per argomenti del thread per St
typedef struct {
    int start;       // Indice iniziale dei nodi da calcolare
    int end;         // Indice finale dei nodi da calcolare
    double *Xt;      // Vettore corrente Xt
    double *Yt;      // Vettore ausiliario Yt
    double *St;      // Somma parziale per i nodi dead end
    grafo *g;        // Grafo
    sem_t *sem;      // Semaforo per la sincronizzazione
} st_worker;

// Struttura per argomenti del thread per PageRank
typedef struct {
    int start;              // Indice iniziale dei nodi da calcolare
    int end;                // Indice finale dei nodi da calcolare
    double *Xt;             // Vettore corrente Xt
    double *Xt1;            // Vettore successivo Xt+1
    double d;               // Damping factor
    double St;              // Somma dei nodi dead-end
    double *Yt;             // Vettore ausiliario Yt
    grafo *g;               // Grafo
    sem_t *sem;             // Semaforo per la sincronizzazione
    pthread_mutex_t *mutex; // Mutex per la sincronizzazione dell'errore
    double *global_error;   // Puntatore alla variabile globale per l'errore
} pr_worker;

// Struct per gestore segnali
typedef struct {
    pthread_mutex_t *mutex; // Per proteggere accesso ai dati condivisi
    double *Xt;             // Vettore corrente PageRank
    int N;                  // Numero di nodi
    int *current_iter;      // Iterazione corrente
} signal_data;

// Prototipi funzioni pagerank
double *pagerank(grafo *g, double d, double eps, int maxiter, int taux, int *numiter);
void *pagerank_aux(void *arg);
void *st_yt_aux(void *arg);
// Funzioni ausiliarie
void *signal_handler_thread(void *arg);
void parse_args(int argc, char *argv[], int *K, int *M, double *D, double *E, int *T, char **input_file);
int compara_nodi(const void *a, const void *b);
void stampa_k(double *vett, int N, int k);

// Funzione principale
int main(int argc, char *argv[]) {
    int K = DEFAULT_TOP_K;
    int M = DEFAULT_MAXITER;
    double D = DEFAULT_DAMPING;
    double E = DEFAULT_EPS;
    int T = DEFAULT_T;
    char *input_file = NULL;

    // Parsing dei dati
    parse_args(argc, argv, &K, &M, &D, &E, &T, &input_file);

    // ------------ Inizializzazione Grafo ------------

    FILE *file = fopen(input_file, "r");
    if (!file)
        termina("Errore nell'apertura del file");

    char line[256];
    // Scansiono finché non trovo la prima riga non commento
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '%') continue;
        break;
    }

    int N, X, Y, dead_ends = 0;
    sscanf(line, "%d %d %d", &N, &X, &Y);

    if (N != X || N <= 0 || X <= 0 || Y <= 0) {
        fclose(file);
        termina("Errore: la matrice non è nel formato corretto");
    }

    grafo *g = crea_grafo(N);
    pc_buffer buf;
    init_buffer(&buf, BUFFER_SIZE);

    pthread_t threads[T];
    void *args[] = { &buf, g };
    for (int i = 0; i < T; i++) {
        xpthread_create(&threads[i], NULL, consumer, args, index);
    }

    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '%') continue;               //Ignoro commenti
        int i, j;
        sscanf(line, "%d %d", &i, &j);
        if (i<=N && j<=N && i > 0 && j > 0) producer(&buf, i - 1, j - 1);   //Guardo solo gli archi validi
    }

    fclose(file);

    // Aspetta i thread
    for (int i = 0; i < T; i++) {
        xsem_post(&buf.full, index); // Sblocca i consumatori
        xpthread_join(threads[i], NULL, index);
    }

    // Conta nodi dead_end
    for (int i = 0; i < g->N; i++) {
        if (g->out[i] == 0)
            dead_ends++;
    }

    // Output dei risultati grafo
    printf("Number of nodes: %d\n", g->N);
    printf("Number of dead-end nodes: %d\n", dead_ends);
    printf("Number of valid arcs: %d\n", g->num_archi);
    
    // ------------ Inizio Pagerank ------------
    int numiter;
    double *pr = pagerank(g, D, E, M, T, &numiter);

    // Output dei risultati pagerank
    if (numiter >= M) {
        printf("Did not converge after %d iterations\n", M);
    } else {
        printf("Converged after %d iterations\n", numiter);
    }

    // Somma dei rank
    double sum = 0.0;
    for (int i = 0; i < g->N; i++) {
        sum += pr[i];
    }
    printf("Sum of ranks: %.4f (should be 1)\n", sum);

    // Trova e stampa i top K nodi

    printf("Top %d nodes:\n",K);
    stampa_k(pr,g->N,K);

    // Libera memoria
    free(pr);
    free_grafo(g);
    destroy_buffer(&buf);

    return 0;
}

// ------------ FINE MAIN ------------

// Funzione aux per calcolare St e Yt
void *st_yt_aux(void *arg) {
    st_worker *xarg = (st_worker *)arg;
    grafo *g = xarg->g;
    int start = xarg->start, end = xarg->end;
    double *Xt = xarg->Xt, *Yt = xarg->Yt, partial_St = 0.0;

    for (int i = start; i < end; i++) {
        if (g->out[i] == 0)
            partial_St += Xt[i];  // Nodo dead-end
        else
            Yt[i] = Xt[i] / g->out[i];
    }

    // Aggiorna la somma St
    xpthread_mutex_lock(&g->node_mutex[0], index);  // Uso il primo mutex per proteggere St
    *(xarg->St) += partial_St;
    xpthread_mutex_unlock(&g->node_mutex[0], index);

    xsem_post(xarg->sem, index);    // Il thread ha finito
    return NULL;
}

// Funzione aux per il calcolo di PageRank
void *pagerank_aux(void *arg) {
    pr_worker *xarg = (pr_worker *)arg;
    grafo *g = xarg->g;
    double d = xarg->d, St = xarg->St;
    int start = xarg->start, end = xarg->end;
    double *Xt1 = xarg->Xt1, *Xt = xarg->Xt, *Yt = xarg->Yt;
    pthread_mutex_t *mutex = xarg->mutex;
    double *global_error = xarg->global_error;

    double local_error = 0.0;  // Errore calcolato dal thread

    for (int j = start; j < end; j++) {
        double sum = 0.0;
        inmap *in_nodes = &g->in[j];

        // Calcolo della somma per i nodi entranti
        for (int k = 0; k < in_nodes->dim; k++) {
            int i = in_nodes->nodi[k];
            sum += Yt[i];
        }

        // Aggiornamento della componente Xj(t+1)
        Xt1[j] = (1.0 - d) / g->N + d * (sum + St / g->N);

        // Calcolo dell'errore
        local_error += fabs(Xt1[j] - Xt[j]);
    }

    // Accumulare l'errore in modo thread-safe
    pthread_mutex_lock(mutex);
    *global_error += local_error;
    pthread_mutex_unlock(mutex);

    xsem_post(xarg->sem, index); // Fine del lavoro
    return NULL;
}

// Funzione principale per calcolare PageRank
double *pagerank(grafo *g, double d, double eps, int maxiter, int taux, int *numiter) {
    int N = g->N;
    double *Xt = malloc(N * sizeof(double));                // Vettore Xt
    double *Xt1 = malloc(N * sizeof(double));               // Vettore Xt+1
    double *Yt = malloc(N * sizeof(double));                // Vettore ausiliario Yt
    double St = 0;  // Somma dei nodi dead end
    pthread_t threads[taux];    // Array dei thread
    st_worker st_args[taux];    // Argomenti per St/Yt
    pr_worker pr_args[taux];    // Argomenti per PageRank
    sem_t sem;  // Semaforo per la sincronizzazione
    pthread_mutex_t error_mutex = PTHREAD_MUTEX_INITIALIZER;
    double global_error = 0.0;

    if (!Xt || !Xt1|| !Yt)
        xtermina("Errore allocazione nel pagerank\n", index);

    int iter;   // Numero iterazioni

    // ----- Inizializzazione segnali
    pthread_t sig_thread;
    pthread_mutex_t sig_mutex = PTHREAD_MUTEX_INITIALIZER;
    signal_data sig_data = {&sig_mutex, Xt, g->N, &iter};

    struct sigaction sa;
    sa.sa_handler = SIG_IGN; // Ignorare il segnale per i thread standard
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);

    xpthread_create(&sig_thread, NULL, signal_handler_thread, &sig_data, index);

    // ----- Fine blocco segnali

    xsem_init(&sem, 0, 0, index);

    // Inizializzazione vettore Xt
    for (int i = 0; i < N; i++) {
        Xt[i] = 1.0 / N;
        Xt1[i] = 0.0;
    }

    for (iter = 0; iter < maxiter; iter++) {
        St = 0.0;
        global_error = 0.0;

        // Parallelizzazione di St e Yt
        int nodes_per_thread = (N + taux - 1) / taux;
        for (int t = 0; t < taux; t++) {
            st_args[t].start = t * nodes_per_thread;
            st_args[t].end = (t + 1) * nodes_per_thread > N ? N : (t + 1) * nodes_per_thread;
            st_args[t].Xt = Xt;
            st_args[t].Yt = Yt;
            st_args[t].St = &St;
            st_args[t].g = g;
            st_args[t].sem = &sem;

            xpthread_create(&threads[t], NULL, st_yt_aux, &st_args[t], index);
        }

        // Attesa dei thread
        for (int t = 0; t < taux; t++) {
            xsem_wait(&sem, index);
        }

        // Parallelizzazione del calcolo di PageRank
        for (int t = 0; t < taux; t++) {
            pr_args[t].start = t * nodes_per_thread;
            pr_args[t].end = (t + 1) * nodes_per_thread > N ? N : (t + 1) * nodes_per_thread;
            pr_args[t].Xt1 = Xt1;
            pr_args[t].Xt = Xt;
            pr_args[t].d = d;
            pr_args[t].St = St;
            pr_args[t].Yt = Yt;
            pr_args[t].g = g;
            pr_args[t].sem = &sem;
            pr_args[t].mutex = &error_mutex;
            pr_args[t].global_error = &global_error;

            xpthread_create(&threads[t], NULL, pagerank_aux, &pr_args[t], index);
        }

        // Attesa dei thread
        for (int t = 0; t < taux; t++) {
            xsem_wait(&sem, index);
        }

        if (global_error < eps)
            break;

        // Aggiornamento di Xt per la prossima iterazione
        for (int i = 0; i < N; i++) {
            Xt[i] = Xt1[i];
            Xt1[i] = 0.0; // Reset per la prossima iterazione
        }
    }

    for(int i=0; i<taux; i++)
        xpthread_join(threads[i], NULL, index);

    *numiter = iter + 1;

    xsem_destroy(&sem, index);
    xpthread_mutex_destroy(&error_mutex, index);
    pthread_cancel(sig_thread);
    xpthread_join(sig_thread, NULL, index);
    free(Yt);
    free(Xt1);

    return Xt;
}

// Funzione gestore segnali
void *signal_handler_thread(void *arg) {
    signal_data *data = (signal_data *)arg;

    while (1) {
        pause(); // Attende il segnale

        xpthread_mutex_lock(data->mutex, index);

        // Trova il nodo con il massimo PageRank
        int max_node = 0;
        double max_value = data->Xt[0];
        for (int i = 1; i < data->N; i++) {
            if (data->Xt[i] > max_value) {
                max_value = data->Xt[i];
                max_node = i;
            }
        }

        // Stampa su stderr
        fprintf(stderr, "Iterazione corrente: %d\n", *data->current_iter);
        fprintf(stderr, "Nodo con massimo PageRank: %d, valore: %.6f\n",
                max_node, max_value);

        xpthread_mutex_unlock(data->mutex, index);
    }

    return NULL;
}

// Funzione per il parsing degli argomenti
void parse_args(int argc, char *argv[], int *K, int *M, double *D, double *E, int *T, char **input_file) {
    int opt;
    while ((opt = getopt(argc, argv, "k:m:d:e:t:")) != -1) {
        switch (opt) {
            case 'k': *K = atoi(optarg); break;
            case 'm': *M = atoi(optarg); break;
            case 'd': *D = atof(optarg); break;
            case 'e': *E = atof(optarg); break;
            case 't': *T = atoi(optarg); break;
            default:
                fprintf(stderr, "Uso: %s [-k K] [-m M] [-d D] [-e E] [-t T] infile\n", argv[0]);
                exit(1);
        }
    }
    if (optind >= argc)
        termina("Errore: file di input mancante");
    *input_file = argv[optind];
}

void stampa_k(double *vett, int N, int k){
    while (k > 0) {
        double max = -1.0;
        int ind = -1;
        for (int i = 0; i < N; i++) {
            if (vett[i] > max) {  // Non serve confronto con prec
                max = vett[i];
                ind = i;
            }
        }
        if (ind != -1) {
            printf("  %d %.6f\n", ind, max);
            vett[ind] = -1.0;  // Segna come processato
            k--;
        }
    }
    return;
}