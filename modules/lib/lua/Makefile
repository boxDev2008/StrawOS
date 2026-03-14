CFLAGS := -O2 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -mno-red-zone -msoft-float -I../libc/include

.PHONY: all
all: liblua.a

override SRCFILES := $(shell find -L src -type f 2>/dev/null | LC_ALL=C sort)
override CFILES := $(filter %.c,$(SRCFILES))
override OBJ := $(addprefix obj/,$(CFILES:.c=.c.o))

liblua.a: $(OBJ)
	rm -f liblua.a
	ar rcs $@ $^

obj/%.c.o: %.c
	mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	rm -rf obj liblua.a