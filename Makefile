CXXFLAGS = -g
LDFLAGS = -pthread

TARGETS = server client

all: $(TARGETS)

clean:
	rm -rf $(TARGETS)
