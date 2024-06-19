all: build/Makefile
	$(MAKE) -C build --no-print-directory
qbf: add_qbf_dependency all
build/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -B build
add_qbf_dependency:
	cmake -DQBF=ON -DCMAKE_BUILD_TYPE=Release -B build
clean:
	rm -rf build
.PHONY: all qbf add_qbf_dependency clean
