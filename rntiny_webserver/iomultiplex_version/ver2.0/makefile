VPATH = ./src:./header:./lib
para = -Wall -std=c++11
cc = g++ $(para)
obj = rntools.o rntiny_imp.o rntiny.o 
main.out: $(obj) rnhead.h rntiny_imp.h
	$(cc) $(obj) -o main.out -lrt -lpthread
rntools.o: rntools.h rnhead.h
	$(cc) -c rntools.cpp
rntiny.o: rntiny_imp.h rnhead.h
	$(cc) -c rntiny.cpp
rntiny_imp.o: rntiny_imp.h rnhead.h rntools.h
	$(cc) -c rntiny_imp.cpp

.PHONY: clean
clean:
	-rm main.out $(obj)
