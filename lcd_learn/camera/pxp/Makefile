OBJ = pxp_lib.o

LIBNAME = libpxp
SONAMEVERSION=0
LFLAGS += -lpthread

all: $(LIBNAME).so $(LIBNAME).a

%.o: %.c
	$(CC) -Wall -fPIC $(CFLAGS) -c $^ -o $@

$(LIBNAME).so.$(SONAMEVERSION): $(OBJ)
	$(CC) -shared -nostartfiles -Wl,-soname,$@ $(LFLAGS) $^ -o $@ $(LDFLAGS)

$(LIBNAME).so: $(LIBNAME).so.$(SONAMEVERSION)
	ln -s $< $@

$(LIBNAME).a: $(OBJ)
	$(AR) -rc $@  $^

.PHONY: clean
clean:
	rm -f $(LIBNAME).* $(OBJ)
