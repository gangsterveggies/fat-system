EXEC_NAME=vfs
CC=gcc

CFLAGS= -Wall -lreadline -lcurses -g

SRC = vfs.c
OBJ = ${SRC:.c=.o}

#------------------------------------------------------------

all: ${EXEC_NAME}

${EXEC_NAME}: ${OBJ}
	${CC} ${OBJ} ${CFLAGS} -o ${EXEC_NAME}

%.o: %.c
	${CC} ${CFLAGS} -c -o $@ $+

clean:
	rm ${EXEC_NAME} *~ *# -rf

remove:
	rm disco