pong : pong.c
	${CC} -o $@ ${CCFLAGS} $?

clean : pong
	unlink $?
