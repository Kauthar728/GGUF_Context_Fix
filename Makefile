##FILE##[Makefile][type:build_config|authority:transformation|verb:compilation]

CC = gcc
CFLAGS = -Wall -Wextra -O2 `pkg-config --cflags gtk+-3.0`
LDFLAGS = `pkg-config --libs gtk+-3.0` -lsqlite3

# Source files
SRCS = CASCADE_EventBus.c \
       CASCADE_BackendEngine.c \
       CASCADE_PageModule.c \
       CASCADE_PageSearch.c \
       CASCADE_PageScan.c \
       CASCADE_PageConfig.c \
       CASCADE_PageLogs.c \
       CASCADE_PageRegistry.c \
       CASCADE_Coordinator.c \
       CASCADE_Main.c

# Object files
OBJS = $(SRCS:.c=.o)

# Target executable
TARGET = cascade_workstation

# Default target
all: $(TARGET)

# Link
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Compile
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	rm -f $(OBJS) $(TARGET)

# Run
run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
