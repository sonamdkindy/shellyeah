EXEC = shellyeah
CC = gcc
ifeq ($(shell hostname),TheTsunami)
	CC = gcc-8
endif
CFLAGS = -Wall
VGFLAGS = --leak-check=full --show-reachable=yes

default: $(EXEC)

$(EXEC): $(EXEC).c
	$(CC) $(CFLAGS) $(EXEC).c -o $(EXEC)

clean:
	rm -f $(EXEC)

.PHONY: valgrind
valgrind:
	${CC} ${CFLAGS} -g $(EXEC).c -o ${EXEC}
	valgrind ${VGFLAGS} ${EXEC}
