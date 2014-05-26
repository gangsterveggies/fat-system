EXEC_NAME=vfs
CC=gcc

CFLAGS= -Wall -lreadline -lcurses

SRC = vfs.c
OBJ = ${SRC:.c=.o}

#------------------------------------------------------------

all: ${EXEC_NAME}

${EXEC_NAME}: ${OBJ}
	${CC} ${SRC} ${CFLAGS} -o ${EXEC_NAME}

clean:
	rm ${EXEC_NAME} *~ *# -rf

remove:
	rm disco