opti9png: opti9png.c
	gcc -Wall -O2 -Os -s -o opti9png -I/home/cly/local/libpng/include -L/home/cly/local/libpng/lib opti9png.c -lpng -lz -lm
