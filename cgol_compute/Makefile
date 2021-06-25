INC = -I../bx/include -I../glfw/include  -I../bgfx/include -I../bx/include/compat/osx
LIBS = libbgfx.a libbimg.a libbx.a libglfw.a -framework QuartzCore -framework Metal -framework Cocoa -framework IOKit -framework CoreVideo
CXXFLAGS += "-std=c++17"

output: main.o 
	clang++ main.o -o output.out -g $(CXXFLAGS) $(LIBS)

main.o: main.cpp
	clang++ -c main.cpp -g $(CXXFLAGS) $(INC)

shaders: v_simple.sc f_simple.sc
	../shaderc -f v_simple.sc -o v_simple.bin --type vertex --platform osx -p metal -i ../bgfx/src
	../shaderc -f f_simple.sc -o f_simple.bin --type fragment --platform osx -p metal -i ../bgfx/src
	../shaderc -f compute.comp -o compute.bin --type compute --platform osx -p metal -i ../bgfx/src
	../shaderc -f start.comp -o start.bin --type compute --platform osx -p metal -i ../bgfx/src
