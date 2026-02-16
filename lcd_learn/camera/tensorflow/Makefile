CROSS_COMPILE = arm-linux-gnueabihf-
CXX = $(CROSS_COMPILE)g++
CC  = $(CROSS_COMPILE)gcc
AR  = $(CROSS_COMPILE)ar

TFLITE_ROOT = /home/zyy/repo/tensorflow
TFLITE_BUILD = $(TFLITE_ROOT)/tensorflow/lite/build

ALL_LIBS := $(shell find $(TFLITE_BUILD) -name "*.a" | grep -v "fft4f2d")

INCLUDES = -I. -I$(TFLITE_ROOT) -I$(TFLITE_BUILD)/flatbuffers/include 

CXXFLAGS = -O2 -mfpu=neon-vfpv4 -mfloat-abi=hard
CFLAGS   = -O2 -mfpu=neon-vfpv4 -mfloat-abi=hard

EXTRA_LIBS = -lpxp
EXTRA_LIBS_PATH = /home/zyy/repo/embed_linux_tutorial/LinuxDriverLearn/lcd_learn/camera/pxp

LDFLAGS = -L$(EXTRA_LIBS_PATH) -Wl,--start-group $(ALL_LIBS) $(EXTRA_LIBS) -Wl,--end-group \
          -lpthread -ldl -lrt -lm -lstdc++

TARGET = imxinfer

CPPSRC = infer.cc
CSRC   = pxpv4l2vision.c

OBJS = infer.o pxpv4l2vision.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)
	cp $@ mobilenet_v2.tflite labels.txt ~/repo/embed_linux_tutorial/nfs_share

infer.o: $(CPPSRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

main.o: $(CSRC)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

lib: infer.o
	$(AR) rcs libinfer.a infer.o

infertest: infertest.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) infertest.cc -o infertest $(LDFLAGS)
	cp $@ ~/repo/embed_linux_tutorial/nfs_share

clean:
	rm -f $(TARGET) $(OBJS) libinfer.a