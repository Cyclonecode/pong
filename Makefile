pong : pong.c
	cc -o pong pong.c

clean : pong
	unlink pong

