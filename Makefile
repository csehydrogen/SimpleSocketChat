CXXFLAGS = -std=c++11 -g
LDFLAGS = -pthread

TARGETS = server client

all: $(TARGETS)

clean:
	rm -rf $(TARGETS)
