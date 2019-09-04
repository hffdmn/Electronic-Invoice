all:
	gcc -DDEBUG -fPIC -shared -o libsecquery.so *.c -L. -lSEC_converge_client -Wl,-rpath=./
	javac -cp .:secapi.jar Main.java

clean :
	rm libsecquery.so *.class
