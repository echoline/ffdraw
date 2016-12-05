CFLAGS=-g
LDFLAGS=-lavformat -lavcodec -lz -lm -ldts
OBJS=\
	main.o\
	draw.o\
	ff.o\

ffdraw: $(OBJS)
	$(CC) -o ffdraw $(OBJS) $(LDFLAGS)

clean:
	rm -f *.o ffdraw
