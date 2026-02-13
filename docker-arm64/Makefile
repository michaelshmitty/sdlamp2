.PHONY: shell build clean

IMAGE_NAME = arm64-dev
PROJECT_DIR := $(shell cd .. && pwd)

CONTAINER_NAME = $(shell docker ps -f "ancestor=$(IMAGE_NAME)" --format "{{.Names}}")

.build: Dockerfile
	docker build --platform linux/arm64 -t $(IMAGE_NAME) .
	touch .build

ifeq ($(CONTAINER_NAME),)
shell: .build
	docker run --platform linux/arm64 -it --rm -v "$(PROJECT_DIR)":/workspace $(IMAGE_NAME) /bin/bash
else
shell:
	docker exec -it $(CONTAINER_NAME) /bin/bash
endif

build: .build
	docker run --platform linux/arm64 --rm -v "$(PROJECT_DIR)":/workspace $(IMAGE_NAME) make

clean:
	docker rmi $(IMAGE_NAME)
	rm -f .build
