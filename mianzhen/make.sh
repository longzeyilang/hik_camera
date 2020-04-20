g++ -std=c++11 -g -o image_direct_callback image_direct_callback.cpp -I./include -Wl,-rpath=/opt/MVS/lib/64 -L/opt/MVS/lib/64 -lMvCameraControl -lpthread -lhiredis

