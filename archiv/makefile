
LIBFLAGS:= -L/usr/lib/x86_64-linux-gnu -lpq -lcurl -lpthread
CPPFLAGS:= --std=c++11 -Wall -I/usr/include/postgresql


driver:	SendCurl.o Pgsql.o driver.o  ./lib/SendCurl.h  ./lib/Pgsql.h
	$(CXX)  -o ./bin/PlayersInfo driver.o ./lib/SendCurl.o  ./lib/Pgsql.o $(LIBFLAGS)

SendCurl.o:	./lib/SendCurl.cpp   
	$(CXX) -c $(CPPFLAGS)  ./lib/SendCurl.cpp  -o ./lib/SendCurl.o

driver.o:	driver.cpp 
	$(CXX) -c $(CPPFLAGS) driver.cpp

Pgsql.o: ./lib/Pgsql.cpp
	$(CXX) -c $(CPPFLAGS)  ./lib/Pgsql.cpp  -o ./lib/Pgsql.o
 
