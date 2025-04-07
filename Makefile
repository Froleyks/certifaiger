MAKEFLAGS += --no-print-directory
all: build/Makefile
	@cmake --build build --parallel
	@cmake --install build --prefix .
qbf: clean qbf_makefile all
qbf_makefile:
	cmake -DCMAKE_BUILD_TYPE=Release -DSTATIC=ON -DQBF=ON -B build
build/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -DSTATIC=ON -B build
docker: clean
	docker build -t certifaiger .
	docker run --rm -it certifaiger examples/uniqueness_model.aag examples/uniqueness_witness.aag --qbf
clean:
	rm -rf build bin
.PHONY: all qbf qbf_makefile docker clean
