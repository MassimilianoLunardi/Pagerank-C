# Progetto di laboratorio 2, Università di Pisa
# Utilizzo programma
./pagerank -d D -e E -m M -t T -k K  filename.mtx
## Parametri (opzionali):
- D: dumping factor (default 0.9)
- E: errore massimo (default 1.0e-7)
- M: n. massime iterazioni (default 100)
- T: n. di thread (default 3)
- K: top k ranks (default 3)

# Utilizzo dei Thread per parallelizzare l'algoritmo

## Lettura del file e creazione del grafo
Durante la lettura del file di input viene implementato un approccio produttore-consumatore. Questo approccio utilizza un buffer circolare protetto da mutex e semafori per garantire la corretta sincronizzazione tra i thread.

### Produttore
- La funzione producer legge ogni arco dal file e lo inserisce nel buffer.
- Utilizza un semaforo empty per verificare che ci sia spazio libero nel buffer e un mutex per gestire l'accesso concorrente.

### Consumatori

- Vengono creati più thread consumatori con la funzione consumer.
- Ogni consumatore preleva gli archi dal buffer (sfruttando il semaforo full per sapere quando sono disponibili dati) e li inserisce nel grafo utilizzando la funzione add_arc.
- L'accesso concorrente alle strutture del grafo è gestito tramite mutex per ogni nodo (node_mutex) e un mutex globale per il conteggio degli archi.

## Calcolo del PageRank
La parallelizzazione del calcolo del pagerank avviene nel seguente modo:

### Calcolo di St e Yt
- Questa fase individua la somma dei nodi "dead-end" (St) e calcola il vettore ausiliario Yt in parallelo.
- I nodi del grafo sono suddivisi tra i thread in base all'intervallo di indici (start e end) assegnato a ciascun thread.
- I risultati parziali di St sono aggregati in maniera thread-safe utilizzando un mutex.

### Aggiornamento di Xt+1

- I thread calcolano in parallelo i nuovi valori del vettore Xt+1 utilizzando il damping factor d, St, e il vettore Yt.
- Per garantire la correttezza, l'errore globale tra Xt e Xt+1 è accumulato in modo thread-safe con l'ausilio di un mutex.
