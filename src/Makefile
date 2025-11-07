CXX      := g++
CXXFLAGS := -std=c++17 -I. -O2 -Wall -Wextra
LDFLAGS  := -lpqxx -lpq -pthread

TARGET   := server
SOURCES  := server.cpp
OBJECTS  := $(SOURCES:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)

.PHONY: all clean