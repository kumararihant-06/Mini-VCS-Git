# ─────────────────────────────────────────────────────────────────────────────
# Mini-VCS  Makefile
# ─────────────────────────────────────────────────────────────────────────────
CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Iincludes -I/opt/homebrew/opt/openssl@3/include
LDFLAGS  := -L/opt/homebrew/opt/openssl@3/lib -lz -lcrypto -lssl

TARGET   := mini_vcs

SRCS := src/utils.cpp       \
        src/objects.cpp     \
        src/index.cpp       \
        src/repository.cpp  \
        src/commit_strategy.cpp \
        src/repo_config.cpp \
        src/cmd_init.cpp    \
        src/cmd_add.cpp     \
        src/cmd_commit.cpp  \
        src/cmd_checkout.cpp\
        src/cmd_branch.cpp  \
        src/cmd_log.cpp     \
        src/cmd_status.cpp  \
        src/cmd_lifetime.cpp \
        main.cpp

OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
