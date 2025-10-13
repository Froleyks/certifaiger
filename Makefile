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
docker: clean
	docker build -t certifaiger .
	docker run --rm -it certifaiger examples/latchToInput_model.aag examples/latchToInput_witness.aag
clean:
	rm -rf build bin
.PHONY: all docker clean proof
