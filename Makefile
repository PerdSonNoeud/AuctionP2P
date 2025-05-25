
CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lpthread
LSSLFLAGS = -lssl -lcrypto

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INCLUDE_DIR = include

$(shell mkdir -p $(OBJ_DIR) $(BIN_DIR))

SRCS = $(wildcard $(SRC_DIR)/*.c)

OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

TARGET = $(BIN_DIR)/AuctionP2P

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS) $(LSSLFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@ 

clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/*

run : $(TARGET)
	./$(TARGET)