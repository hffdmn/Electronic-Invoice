all:
#	gcc -o db_serve *.c -I/usr/include/mysql -L/usr/lib64/mysql -lmysqlclient -lz -lm -lpthread -g
	gcc -DDEBUG -Wl,--rpath=./ -o db_serve *.c -I/usr/include/mysql -L. -lmysqlclient -lz -lm -lpthread -g
