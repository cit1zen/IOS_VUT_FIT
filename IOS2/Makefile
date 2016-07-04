Preklad= gcc -std=gnu99 -Wall -Wextra -Werror -pedantic

all: h2o

h2o: h2o.o
	$(Preklad) -pthread  h2o.o -o h2o
h2o.o: h2o.c
	$(Preklad) -c h2o.c
