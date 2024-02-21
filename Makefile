SRC = avdecode.c circbuf.c main.c rng.c
HDR = avdecode.h circbuf.h rng.h

main: $(SRC) $(HDR)
	gcc -o $@ $^ -O2 -pthread -lSDL2 -lavcodec -lavutil -lavformat -lm

.PHONY: clean
clean:
	rm -f main
