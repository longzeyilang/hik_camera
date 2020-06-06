#cd libjpeg
#rm -rf .deps
#rm -rf .libs
#rm -rf ./*.o
#rm -rf ./*.lo
#./configure
#make
#
#cd ..
g++ -std=c++11 -g -o image_direct_callback image_direct_callback2.cpp -I./include -I./libjpeg -Wl,-rpath=/opt/MVS/lib/64 -L-Wl,-rpath=/opt/MVS/lib/64 -L/opt/MVS/lib/64 -lMvCameraControl -lpthread -lhiredis -Wl,-rpath=/home/docker/IBM/camera/libjpeg/.libs -L/home/docker/IBM/camera/libjpeg/.libs -ljpeg 
