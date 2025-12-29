# File: gui_app/Makefile
CC = gcc
CFLAGS = `pkg-config --cflags gtk4`
LDFLAGS = `pkg-config --libs gtk4`

TARGET = mpu6050_monitor_gui
SOURCE = mpu6050_monitor_gui.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET) $(LDFLAGS)
	

clean:
	@echo "--- Đang dọn dẹp file rác ---"
	rm -f $(TARGET)
	@echo ">> Đã xóa sạch!"

.PHONY: all clean