CC=gcc
CFLAGS=-std=c11 -Wall -g -O -pthread
LDLIBS=-lm -lrt -pthread


EXECS=pagerank 


all: $(EXECS) 

pagerank: pagerank.o xerrori.o graph.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	$(RM) *.o 

%.o: %.c xerrori.h graph.h
	$(CC) $(CFLAGS) -c $<

clean: 
	rm -f *.o $(EXECS)
