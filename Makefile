OBJECTS = c_parser.o
TARGETS = c_parser innochecksum_changer
SRCS = include/mysql_def.h c_parser.c
INC_PATH = -I./include
LIBS = -pthread -lm
BINDIR = ./bin
OUTPUT = ./output

CC ?= gcc
INSTALL ?=install
YACC = bison
LEX = flex

CFLAGS += -D_FILE_OFFSET_BITS=64 -Wall -g -O3 -pipe
INSTALLFLAGS ?=-s
CentOS5 = $(findstring .el5,$(shell cat /proc/version))
ifeq ($(CentOS5), .el5)
CFLAGS += -DCentOS5
endif

all: $(TARGETS)
		rm -rf $(OUTPUT)
		mkdir -p $(OUTPUT)/bin
		mv c_parser $(OUTPUT)/bin/
		rm -rf $(OBJECTS)
		mkdir -p $(OUTPUT)/dumps
		mkdir -p $(OUTPUT)/data
		mkdir -p $(OUTPUT)/pages
		cp -r example/* $(OUTPUT)/data/

debug: LEX_DEBUG = -d
debug: YACC_DEBUG = -r all
debug: CFLAGS += -DDEBUG -DSTREAM_PARSER_DEBUG
debug: $(TARGETS)

stream_parser.o: stream_parser.c include/mysql_def.h
	$(CC) $(CFLAGS) $(INC_PATH) -c $<

#stream_parser: stream_parser.o
#	$(CC) $(CFLAGS) $(INC_PATH) $(LIB_PATH) $(LIBS) $(LDFLAGS) $< -o $@

sql_parser.o: sql_parser.c
	$(CC) $(CFLAGS) $(INC_PATH) -c $<

sql_parser.c: sql_parser.y lex.yy.c
	$(YACC) $(YACC_DEBUG) -o $@ $<

lex.yy.c: sql_parser.l
	$(LEX) $(LEX_DEBUG) $<

util.o: util.c
	$(CC) $(CFLAGS) -c $<

c_parser.o: c_parser.c
	$(CC) $(CFLAGS) $(INC_PATH) -c $<

tables_dict.o: tables_dict.c
	$(CC) $(CFLAGS) $(INC_PATH) -c $<

print_data.o: print_data.c
	$(CC) $(CFLAGS) $(INC_PATH) -c $<

check_data.o: check_data.c
	$(CC) $(CFLAGS) $(INC_PATH) -c $<

c_parser: sql_parser.o c_parser.o stream_parser.o  tables_dict.o print_data.o check_data.o util.o
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC_PATH) $(LIB_PATH) $^ -o $@ $(LIBS)

innochecksum_changer: innochecksum.c include/innochecksum.h
	$(CC) $(CFLAGS) $(LDFLAGS) $(INC_PATH) -o $@ $<

sys_parser: sys_parser.c
	@ which mysql_config || (echo "sys_parser needs mysql development package( either -devel or -dev)"; exit -1)
	$(CC) -o $@ $< `mysql_config --cflags` `mysql_config --libs`

install: $(TARGETS)
	$(INSTALL) $(INSTALLFLAGS) $(TARGETS) $(BINDIR)

clean:
	rm -f $(OBJECTS) $(TARGETS) lex.yy.c sql_parser.c sql_parser.output sys_parser
	rm -f *.o *.core
	rm -rf $(OUTPUT)

docker-start:
	@docker run \
        -v $(shell pwd):/undrop-for-innodb \
        -it \
        --rm \
        centos:7 bash -l
