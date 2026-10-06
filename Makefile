CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
TARGET   := search_engine
HEADERS  := $(wildcard src/*.hpp)

$(TARGET): src/main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp

run: $(TARGET)
	./$(TARGET) --csv data/articles.csv --dir data/articles

clean:
	rm -f $(TARGET)

.PHONY: run clean
