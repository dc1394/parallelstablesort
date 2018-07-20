PROG  := parallelstablesort
PROG2 := makestablesortdata

SRCS  := parallelstablesort.cpp
SRCS2 := makestablesortdata.cpp

OBJS  = parallelstablesort.o
OBJS2 = makestablesortdata.o

DEPS  = parallelstablesort.d
DEPS2 = makestablesortdata.d

VPATH  = src/parallelstablesort src/makestablesortdata
CXX = icpc
CXXFLAGS = -Wall -Wextra -O3 -xHOST -ipo -pipe -std=c++14 -fopenmp
LDFLAGS = -L/home/dc1394/oss/tbb2018_20180411oss/lib/intel64/gcc4.7 -ltbb \
		  -L/home/dc1394/oss/boost_1_67_0/stage/icc/lib \
		  -lboost_filesystem -lboost_serialization -lboost_system -lboost_thread

all: $(PROG) $(PROG2) ;
#rm -f $(OBJS) $(DEPS)

-include $(DEPS)

$(PROG): $(OBJS)
		$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $^

$(PROG2): $(OBJS2)
		$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -MMD -MP -DDEBUG $<

clean:
		rm -f $(PROG) $(OBJS) $(DEPS)
		rm -f $(PROG2) $(OBJS2) $(DEPS2)
