all: build/Makefile
	$(MAKE) -C build --no-print-directory
qbf: add_qbf_dependency all
build/Makefile: CMakeLists.txt
	cmake -DCMAKE_BUILD_TYPE=Release -B build
add_qbf_dependency:
	cmake -DQBF=ON -DCMAKE_BUILD_TYPE=Release -B build
docker: clean
	git submodule update --init --recursive
	docker build -t certifaiger .
	docker run certifaiger examples/01_model.aag examples/06_witness.aag
clean:
	rm -rf build
.PHONY: all qbf add_qbf_dependency docker clean
