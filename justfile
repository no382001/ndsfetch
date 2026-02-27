# your http server
server := "192.168.0.11:3923"

default: build

build: format
    docker compose run --rm blocksds make

clean:
    docker compose run --rm blocksds make clean

# assumes that anyone has rwmda on /nds
upload: build
    curl -X DELETE http://{{server}}/nds/ndsfetch.nds 2>/dev/null || true
    curl -T ndsfetch.nds http://{{server}}/nds/ndsfetch.nds

format:
    clang-format -i source/*.c
