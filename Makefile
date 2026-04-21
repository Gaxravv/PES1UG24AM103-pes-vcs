CC = gcc
CFLAGS = -Wall -Wextra -O2

# ─── SOURCE FILES ─────────────────────────────

SRCS = pes.c index.c commit.c tree.c object.c

# ─── OBJECT FILES ─────────────────────────────

OBJS = $(SRCS:.c=.o)

# ─── MAIN EXECUTABLE ─────────────────────────

pes: $(OBJS)
	$(CC) $(CFLAGS) -o pes $(OBJS) -lcrypto

# ─── TEST TARGETS ─────────────────────────────

test_tree: test_tree.o tree.o object.o index.o
	$(CC) $(CFLAGS) -o test_tree test_tree.o tree.o object.o index.o -lcrypto

# (Optional future test hooks)
test_commit: test_commit.o commit.o object.o tree.o index.o
	$(CC) $(CFLAGS) -o test_commit test_commit.o commit.o object.o tree.o index.o -lcrypto

# ─── GENERIC BUILD RULE ───────────────────────

%.o: %.c
	$(CC) $(CFLAGS) -c $<

# ─── CLEAN ────────────────────────────────────

clean:
	rm -f *.o pes test_tree test_commit
