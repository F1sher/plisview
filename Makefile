plis3:
	g++ `pkg-config --cflags gtk+-3.0` -o plisview3.o plisview3.c -L ./lib/ -l cyusb -lm `pkg-config --libs gtk+-3.0`
	
	
plis6ftdi: plisview6ftdi.o plot.o plot.h
	gcc `pkg-config --cflags gtk+-3.0` -o plisview6ftdi plisview6ftdi.o plot.o `pkg-config --libs gtk+-3.0` -lm -L. -lftd2xx -Wl,-rpath /usr/local/lib
	
plot.o: plot.c plot.h
	g++ `pkg-config --cflags gtk+-3.0` -c plot.c `pkg-config --cflags --libs gtk+-3.0` -lm

plisview6ftdi.o: plisview6ftdi.c plot.h
	gcc `pkg-config --cflags gtk+-3.0` -c plisview6ftdi.c `pkg-config --cflags --libs gtk+-3.0` -L. -lftd2xx -Wl,-rpath /usr/local/lib -lm
	

plis10cy: plisview10cy.o plot.o plot.h
	g++ `pkg-config --cflags gtk+-3.0` -o plisview10cy plisview10cy.o plot.o -L ./lib/ -l cyusb -lm `pkg-config --libs gtk+-3.0`
	
plisview10cy.o: plisview10cy.c plot.h
	g++ `pkg-config --cflags gtk+-3.0` -c plisview10cy.c `pkg-config --libs gtk+-3.0` -L ./lib/ -l cyusb -lm
	
	
plis11cy: plisview11cy.o plot.o plot.h
	g++ `pkg-config --cflags gtk+-3.0` -o plisview11cy plisview11cy.o plot.o -L ./lib/ -l cyusb -lm `pkg-config --libs gtk+-3.0`
	
plisview11cy.o: plisview11cy.c plot.h
	g++ `pkg-config --cflags gtk+-3.0` -c plisview11cy.c `pkg-config --libs gtk+-3.0` -L ./lib/ -l cyusb -lm
	
	
plis12cy: plisview12cy.o plot.o plot.h
	g++ `pkg-config --cflags gtk+-3.0` -o plisview12cy plisview12cy.o plot.o -L ./lib/ -l cyusb -lm `pkg-config --libs gtk+-3.0`
	
plisview12cy.o: plisview12cy.c plot.h
	g++ `pkg-config --cflags gtk+-3.0` -c plisview12cy.c `pkg-config --libs gtk+-3.0` -L ./lib/ -l cyusb -lm

