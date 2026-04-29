.PHONY: setup-rel setup-dbg debug test

all: build build/debug test

setup-rel:
	cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
setup-dbg:
	cmake -G Ninja -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug

build: setup-rel
	cmake --build build

debug: build/debug

build/debug: setup-dbg
	cmake --build build/debug

test:
	ctest --test-dir build/tests --progress --stop-on-failure

clean:
	rm -rf build
