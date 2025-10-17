name = certifaiger

MAKEFLAGS += --no-print-directory
all: build/Makefile
	@cmake --build build --parallel
	@cmake --install build --prefix .
build/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -DSTATIC=ON -B build
proof: CMakeLists.txt
    # The latest release/master of CaDiCaL is missing important optimizations for certificate checking
	cmake -DCMAKE_BUILD_TYPE=Release -DSTATIC=ON -B build -DSAT=cadical -DCADICAL_GIT_TAG="development" -DPROOF=ON
	$(MAKE) all
container: clean
	podman build -t $(name) .
	podman create --name $(name) $(name)
	podman cp $(name):/$(name)/bin .
	podman rm $(name)
clean:
	rm -rf build bin
	-podman rmi $(name)
.PHONY: all proof container clean
