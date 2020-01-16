pong : pong.c
	${CC} -o $@ ${CCFLAGS} $?

debug : pong.c
	${CC} -g -o $@ ${CCFLAGS} $?

clean : pong
	unlink $?
