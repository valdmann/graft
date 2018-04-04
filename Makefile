graft : graft.cpp
	g++ graft.cpp -o graft -std=c++17 -Os -l stdc++fs -Wall -Wextra -pedantic

install : graft
	sudo chown root graft
	sudo chmod u+s graft
	mv graft ~/bin/graft
