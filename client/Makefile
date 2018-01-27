CC=/bin/gcc
RM=/bin/rm
CFLAGS = -Wall -Wextra -g -std=c++11 -pipe \
				 -Wno-unused-parameter -Wno-reorder -Wno-empty-body \
				 -lboost_system \
				 -lpthread \
				 -Ilib/socket.io-client-cpp/lib/rapidjson/include \
				 -Ilib/socket.io-client-cpp/lib/websocketpp \
				 -Ilib/socket.io-client-cpp/src

TARGETS = client.cc  lib/socket.io-client-cpp/src/*.cpp \
					lib/socket.io-client-cpp/src/internal/*.cpp

default: $(TARGETS)
	g++ $(CFLAGS) -o client $(TARGETS)

clean:
	$(RM) -f client
