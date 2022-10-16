LDLIBS+= -lpthread

rebuild: clean all
all: twmailer-server twmailer-client


clean:
	clear
	rm -f twmailer-client twmailer-server

twmailer-server: TWMailerServer
	g++ -std=c++17 -Wall -Werror -o twmailer-server TWMailerServer.cpp -pthread
 
twmailer-client: TWMailerClient
	g++ -std=c++17 -Wall -Werror -o twmailer-client TWMailerClient.cpp

