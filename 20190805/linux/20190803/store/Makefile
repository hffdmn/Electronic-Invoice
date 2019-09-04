all:
	gcc -DDEBUG -fPIC -shared -o libsecstore.so *.c -L. -lSEC_converge_client -Wl,-rpath=./
	javac -cp .:secapi.jar Main.java

clean :
	rm libsecstore.so *.class
