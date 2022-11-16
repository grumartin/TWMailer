LDLIBS+= -lpthread
LIBS=-lldap

rebuild: clean all
all: twmailer-server twmailer-client


clean:
	clear
	rm -f twmailer-client twmailer-server

twmailer-server: TWMailerServer
	g++ -w -std=c++14 -Wall -Werror -o twmailer-server TWMailerServer.cpp -pthread -lldap
 
twmailer-client: TWMailerClient
	g++ -std=c++17 -Wall -Werror -o twmailer-client TWMailerClient.cpp
