poll: json.h main.cpp
	g++ -std=c++17 -O3 main.cpp -o poll -I . -lcurl

.PHONY: clean

clean:
	rm poll
