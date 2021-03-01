UFL    := ../ufl

CFLAGS += -Wall
CFLAGS += -g -MMD -MP

CFLAGS += -I$(UFL)
CFLAGS += -DJPFS_TEST
CFLAGS += -DUFL_IMPL='"flashsimul.h"'
CFLAGS += -DJPFS_IMPL='"jpfs_posix.h"'

CFLAGS += -fprofile-arcs -ftest-coverage -g -O0
LDLIBS += -lcriterion -lgcov

VPATH  += $(UFL)

OBJS   := jpfs.o flashsimul.o

runtest: jpfs
	rm -f *.gcda
	./jpfs --verbose -j1
	gcovr -e flashsimul.c -s --html --html-details --output coverage.html

jpfs: $(OBJS)

clean:
	rm -f *.o *.d jpfs
	rm -f *.gc* coverage*.html

.PHONY: clean runtest

-include $(OBJS:.o=.d)
