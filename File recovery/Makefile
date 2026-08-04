# CC = gcc
# CFLAGS = -std=gnu17 -Wall -Wextra -g
# LDFLAGS = -lcrypto

# nyufile: nyufile.c
# 	$(CC) $(CFLAGS) -o nyufile nyufile.c $(LDFLAGS)

# clean:
# 	rm -f nyufile

CC = gcc
CFLAGS = -std=gnu17 -Wall -Wextra -g -I/opt/homebrew/opt/openssl@3/include
LDFLAGS = -L/opt/homebrew/opt/openssl@3/lib -lcrypto

nyufile: nyufile.c
	$(CC) $(CFLAGS) -o nyufile nyufile.c $(LDFLAGS)

clean:
	rm -f nyufile