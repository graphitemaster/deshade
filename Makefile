CXX := g++
CXXFLAGS := -fPIC -Wall -Wextra -O2 -std=c++11 -g
LDFLAGS := -shared
RM := rm -f
SRCS := gl.cpp vk.cpp log.cpp hash.cpp
OBJS := $(SRCS:.cpp=.o)
DEPS := $(SRCS:.cpp=.d)

.PHONY: all
all: deshade.so

deshade.so: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

$(DEPS):%.d:%.cpp
	$(CXX) $(CXXFLAGS) -MM $< > $@

include $(DEPS)

.PHONY: clean
clean:
	-$(RM) deshade.so $(OBJS) $(DEPS)

