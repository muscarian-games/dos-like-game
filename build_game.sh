clang -o meniskos.out source/meniskos.c source/dos.c `sdl2-config --libs --cflags` -lGLEW -framework OpenGL -lpthread