pong : pong.c
	${CC} -o $@ ${CCFLAGS} pong.c

clean : pong
	unlink pong

