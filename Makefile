prefix ?= $(STOW_DIR)/graft

graft : graft.cpp
	g++ graft.cpp -o graft -std=c++17 -Os -l stdc++fs -l fmt -Wall -Wextra -pedantic

.PHONY: install
install : graft
	mkdir -p $(prefix)/bin
	sudo cp graft $(prefix)/bin
	sudo chown root $(prefix)/bin/graft
	sudo chmod u+s $(prefix)/bin/graft
