SRCDIR = 
OBJDIR = 

SOURCES := $(wildcard *.c)
OBJECTS := $(patsubst %.c, %.o, $(SOURCES))
objs := $(OBJECTS)

libfont.a: $(objs)
	$(AR) -r -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<
%.o: %.S
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

clean:
	rm -rf *.o libfont.a