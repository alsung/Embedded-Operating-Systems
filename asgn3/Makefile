SUBDIRS=src tools

all:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir; \
	done

docs: DESIGN.pdc
	pandoc -f markdown -t latex -o DESIGN.pdf DESIGN.pdc

.PHONY: load unload clean format
load:
	$(MAKE) -C src load

unload:
	$(MAKE) -C src unload

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

#note: llvm provides clang-format 9.0, which doesn't recognize all of our options.
#instead we use llvm-devel pacakge
format:
	clang-format-devel -i **/*.c **/*.h
